#include "downloadmanager.h"
#include "downloaditem.h"
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_queueWatcher(new QTimer(this))
    , m_maxConcurrentDownloads(3) // 默认同时下载 3 个，可在头文件修改
{
    connect(m_queueWatcher, &QTimer::timeout, this, &DownloadManager::processQueue);
    m_queueWatcher->start(1000); // 每秒检查一次队列
}

DownloadManager::~DownloadManager()
{
    m_queueWatcher->stop();
    cleanupAllDownloads();
}

void DownloadManager::addDownload(const QUrl &url, const QString &savePath)
{
    // 注意：DownloadItem 是 QWidget，这里创建时没有父 widget，
    // 需要依靠 MainWindow 接收到 downloadAdded 信号后将其加入布局
    DownloadItem *item = new DownloadItem(url, savePath);

    // 存入列表和队列
    m_downloads.append(item);
    m_waitingQueue.enqueue(item);

    // 连接信号
    connect(item, &DownloadItem::progressChanged, this, &DownloadManager::onDownloadProgress);
    connect(item, &DownloadItem::finished, this, &DownloadManager::onDownloadFinished);
    connect(item, &DownloadItem::error, this, &DownloadManager::onDownloadError);
    connect(item, &DownloadItem::canceled, this, &DownloadManager::onDownloadCanceled);

    // 重要：当 item 被销毁时，自动从列表中移除，防止野指针
    connect(item, &QObject::destroyed, this, [this, item]() {
        m_downloads.removeAll(item);
        m_waitingQueue.removeAll(item);
    });

    emit downloadAdded(item);
    qDebug() << "Download added to queue:" << url.toString();

    // 立即尝试处理队列，不用等 timer
    processQueue();
}

void DownloadManager::processQueue()
{
    int activeCount = 0;

    // 统计当前正在下载的任务数
    // 优化：不再依赖 status 文字，而是利用逻辑状态
    for (DownloadItem *item : m_downloads) {
        if (item) {
            // 如果不在等待队列中，且未完成、未暂停，则认为正在下载
            bool isInQueue = m_waitingQueue.contains(item);
            bool isFinished = item->isFinished();


            if (!isInQueue && !isFinished ) {
                activeCount++;
            }
        }
    }

    // 启动等待队列中的任务
    while (activeCount < m_maxConcurrentDownloads && !m_waitingQueue.isEmpty()) {
        DownloadItem *item = m_waitingQueue.dequeue();
        if (item && !item->isFinished() ) {
            item->start();
            activeCount++;
            qDebug() << "Started download from queue:" << item->fileName();
        }
    }
}


void DownloadManager::cancelAllDownloads()
{
    m_waitingQueue.clear();
    for (DownloadItem *item : m_downloads) {
        if (item) item->cancel();
    }
}

// 注意：此函数实际上是“取消所有并等待完成”，用于程序退出前
void DownloadManager::waitForAll(int timeoutMs)
{
    if (m_downloads.isEmpty()) return;

    QList<DownloadItem*> activeItems;
    for (DownloadItem *item : m_downloads) {
        if (item && !item->isFinished()) activeItems.append(item);
    }
    if (activeItems.isEmpty()) return;

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);

    QAtomicInt pending = activeItems.size();

    // 定义完成回调
    auto onItemDone = [&]() {
        if (--pending <= 0) {
            QMetaObject::invokeMethod(&loop, "quit", Qt::QueuedConnection);
        }
    };

    for (DownloadItem *item : activeItems) {
        // 连接信号，当任务结束（无论成功、失败、取消）时计数
        connect(item, &DownloadItem::finished, this, [onItemDone]() { onItemDone(); }, Qt::QueuedConnection);
        connect(item, &DownloadItem::error, this, [onItemDone]() { onItemDone(); }, Qt::QueuedConnection);
        connect(item, &DownloadItem::canceled, this, [onItemDone]() { onItemDone(); }, Qt::QueuedConnection);

        // 这里主动取消，确保程序能退出
        QMetaObject::invokeMethod(item, "cancel", Qt::QueuedConnection);
    }

    loop.exec();

    // 如果超时后仍有未完成的，强制清理
    if (pending > 0) {
        qWarning() << "WaitForAll timeout, forcing cleanup...";
        for (DownloadItem *item : activeItems) {
            if (item) {
                item->blockSignals(true);
                item->deleteLater();
            }
        }
        QThread::msleep(100);
        QCoreApplication::processEvents();
    }
}

double DownloadManager::getOverallProgress() const
{
    if (m_downloads.isEmpty()) return 0.0;
    double total = 0.0;
    int count = 0;
    for (DownloadItem *item : m_downloads) {
        if (item) {
            total += item->progress();
            count++;
        }
    }
    return count > 0 ? total / count : 0.0;
}

void DownloadManager::onDownloadProgress()
{
    emit downloadProgressChanged();
}

void DownloadManager::onDownloadFinished(DownloadItem *item)
{
    Q_UNUSED(item);
    emit downloadProgressChanged();
    // 任务完成后，检查队列是否有新任务可以启动
    QTimer::singleShot(100, this, [this]() { processQueue(); });
}

void DownloadManager::onDownloadError(DownloadItem *item, const QString &error)
{
    Q_UNUSED(item);
    Q_UNUSED(error);
    emit downloadProgressChanged();
    // 出错后也释放一个并发槽位
    QTimer::singleShot(100, this, [this]() { processQueue(); });
}

void DownloadManager::onDownloadCanceled(DownloadItem *item)
{
    Q_UNUSED(item);
    emit downloadProgressChanged();
    QTimer::singleShot(100, this, [this]() { processQueue(); });
}

void DownloadManager::cleanupAllDownloads()
{
    m_waitingQueue.clear();
    // 复制一份列表，防止遍历时修改容器
    QList<DownloadItem*> copies = m_downloads;
    for (DownloadItem *item : copies) {
        if (item) {
            item->blockSignals(true); // 防止清理过程中触发信号
            item->cancel();
            item->deleteLater();
        }
    }
    m_downloads.clear();
}

void DownloadManager::removeItems(const QList<DownloadItem*> &items)
{
    for (DownloadItem *item : items) {
        if (!item) continue;

        // 1. 取消下载
        item->cancel();

        // 2. 从容器中移除
        m_downloads.removeAll(item);
        m_waitingQueue.removeAll(item);

        // 3. 异步删除 (让事件循环处理，避免析构冲突)
        item->deleteLater();
    }
}
