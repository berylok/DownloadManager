#include "downloadworker.h"
#include "downloadsegment.h"
#include "downloadutils.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <QDateTime>
#include <QEventLoop>
#include <QMutexLocker>
#include <QDebug>
#include <QPointer>

// 防止 Preferences 缺失导致编译错误
#ifdef HAVE_PREFERENCES
#include "preferences.h"
#define MAX_RETRIES Preferences::getMaxRetries()
#else
#define MAX_RETRIES 3
#endif


DownloadWorker::DownloadWorker(QObject *parent)
    : QObject(parent)
    , m_canceled(false)
    , m_downloading(false)
    , m_finished(false)
    , m_fileSize(0)
    , m_downloadedSize(0)
    , m_speed(0)
    , m_finishedSegments(0)
    , m_threadCount(1)
    , m_maxRetries(MAX_RETRIES)
    , m_speedUpdateTimer(new QTimer(this))
    , m_totalBlocks(0)
{
    m_elapsedTimer.invalidate();

    connect(m_speedUpdateTimer, &QTimer::timeout, this, &DownloadWorker::updateSpeed);
    m_speedUpdateTimer->setInterval(1000);  // 每秒更新一次速度
    m_speedUpdateTimer->setSingleShot(false);
}


DownloadWorker::~DownloadWorker()
{
    qDebug() << "DownloadWorker destructor for" << m_fileName;
    if (m_downloading && !m_finished) {
        cancelDownload();
    }
}


void DownloadWorker::startDownload(const QUrl &url, const QString &savePath,
                                   int threadCount, qint64 blockSize)
{
    QMutexLocker locker(&m_mutex);
    if (m_downloading || m_finished) {
        qWarning() << "Download already in progress or finished";
        return;
    }

    // 重置状态
    m_url = url;
    m_savePath = savePath;
    m_threadCount = qMax(1, threadCount);
    m_fileSize = 0;
    m_downloadedSize = 0;
    m_speed = 0;
    m_finishedSegments = 0;
    m_canceled.store(false);
    m_downloading = true;
    m_finished = false;
    m_retryCounts.clear();
    m_startTime = QDateTime::currentDateTime();
    m_totalTimeMs = 0;
    m_elapsedTimer.start();
    m_lastSpeedBytes = 0;

    // 设置块大小（默认 256KB）
    qint64 actualBlockSize = (blockSize > 0) ? blockSize : 256 * 1024;

    // 从 URL 提取文件名
    QString path = url.path();
    m_fileName = DownloadUtils::sanitizeFileName(QFileInfo(path).fileName());
    if (m_fileName.isEmpty()) m_fileName = "download_file";

    // 确保保存目录存在
    QDir saveDir(savePath);
    if (!saveDir.exists() && !saveDir.mkpath(".")) {
        locker.unlock();
        emit errorOccurred(QString("无法创建保存目录：%1").arg(savePath));
        return;
    }

    // 获取文件大小（带重试）
    locker.unlock();

    int maxRetries = 3;
    int retryCount = 0;
    bool sizeFetched = false;

    while (!sizeFetched && retryCount < maxRetries) {
        if (retryCount > 0) {
            // 重试前等待，采用指数退避：1s, 2s, 4s
            int delay = 1000 * (1 << (retryCount - 1));
            QThread::msleep(delay);
            qDebug() << "Retrying fetchFileSize, attempt" << retryCount + 1;
        }
        sizeFetched = fetchFileSize();
        retryCount++;
    }

    if (!sizeFetched) {
        emit errorOccurred("无法获取文件大小，请检查网络或URL");
        return;
    }

    locker.relock();

    // 创建临时目录（用于存放所有块的临时文件）
    m_tempDir = QString("%1/%2_blocks").arg(QDir::tempPath(), m_fileName);
    QDir td(m_tempDir);
    if (!td.exists() && !td.mkpath(".")) {
        locker.unlock();
        emit errorOccurred(QString("无法创建临时目录：%1").arg(m_tempDir));
        return;
    }

    // 生成任务队列（每个块为 [start, end]）
    m_tasks.clear();
    for (qint64 pos = 0; pos < m_fileSize; pos += actualBlockSize) {
        qint64 end = qMin(pos + actualBlockSize - 1, m_fileSize - 1);
        m_tasks.enqueue(qMakePair(pos, end));
    }
    m_totalBlocks = m_tasks.size();
    m_completedBlocks = 0;  // 直接赋值，QAtomicInt 支持隐式转换
    qDebug() << "Total blocks:" << m_totalBlocks << "block size:" << actualBlockSize;

    // 重置分段进度数组（用于整体速度计算）
    m_segmentProgress.clear();
    m_segmentProgress.resize(m_threadCount, 0);

    // 启动速度更新定时器
    m_speedUpdateTimer->start();

    // 创建指定数量的 DownloadSegment
    for (int i = 0; i < m_threadCount; ++i) {
        // 注意：DownloadSegment 构造函数已修改为接受 worker 指针和 tempDir
        DownloadSegment *seg = new DownloadSegment(i, m_url, m_tempDir, this);
        m_segments.append(QPointer<DownloadSegment>(seg));
        QThread *thread = new QThread();
        seg->moveToThread(thread);

        // 跨线程信号连接
        connect(thread, &QThread::started, seg, &DownloadSegment::fetchNextBlock, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::progress, this, &DownloadWorker::onSegmentProgress, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::blockFinished, this, &DownloadWorker::onBlockFinished, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::error, this, &DownloadWorker::onSegmentError, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::finished, this, &DownloadWorker::onSegmentFinished, Qt::QueuedConnection); // 分段无更多块时发出

        // 线程结束后自动清理对象
        connect(thread, &QThread::finished, seg, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        m_threads.append(QPointer<QThread>(thread));
        thread->start();
    }
}


bool DownloadWorker::fetchFileSize()
{
    QNetworkAccessManager manager;
    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Qt Download Manager)");

    // HEAD 请求
    QNetworkReply *reply = manager.head(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() == QNetworkReply::NoError) {
        QString acceptRanges = reply->rawHeader("Accept-Ranges");
        if (acceptRanges != "bytes") {
            qDebug() << "Server does not support range requests, fallback to single thread";
            m_threadCount = 1;
        }
        m_fileSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        reply->deleteLater();
        return m_fileSize > 0;
    }

    // 尝试 Range: bytes=0-0 的 GET 请求
    qDebug() << "HEAD request failed, trying GET with Range: bytes=0-0";
    request.setRawHeader("Range", "bytes=0-0");
    QNetworkReply *getReply = manager.get(request);
    QEventLoop loop2;
    connect(getReply, &QNetworkReply::finished, &loop2, &QEventLoop::quit);
    loop2.exec();

    if (getReply->error() == QNetworkReply::NoError) {
        QString range = getReply->rawHeader("Content-Range");
        if (!range.isEmpty()) {
            QString sizeStr = range.split('/').last();
            m_fileSize = sizeStr.toLongLong();
        } else {
            m_fileSize = getReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            m_threadCount = 1;
        }
        getReply->deleteLater();
        return m_fileSize > 0;
    }

    getReply->deleteLater();
    return false;
}


void DownloadWorker::onSegmentProgress(int id, qint64 bytesReceived)
{
    QMutexLocker locker(&m_mutex);
    if (id < 0 || id >= m_segmentProgress.size()) return;

    m_segmentProgress[id] += bytesReceived;
    m_downloadedSize += bytesReceived;
    locker.unlock();

    // 发射进度信号（速度由定时器更新）
    emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
}


void DownloadWorker::onSegmentFinished(int id)
{
    qDebug() << "Segment thread" << id << "finished (no more blocks)";
    QMutexLocker locker(&m_mutex);
    m_finishedSegments++;
    // 不在此处触发合并，合并由 onBlockFinished 完成
}


void DownloadWorker::onSegmentCanceled(int id)
{
    qDebug() << "Segment" << id << "canceled";
    QMutexLocker locker(&m_mutex);
    if (id >= 0 && id < m_threads.size()) {
        QThread *thread = m_threads[id].data();
        if (thread && thread->isRunning()) {
            thread->quit();
        }
    }
}


void DownloadWorker::onSegmentError(int id, const QString &error)
{
    qDebug() << "Segment" << id << "error:" << error;

    QMutexLocker locker(&m_mutex);
    if (m_canceled.load()) return;

    int retries = m_retryCounts.value(id, 0);
    if (retries < m_maxRetries) {
        m_retryCounts[id] = retries + 1;
        locker.unlock();
        if (id >= 0 && id < m_segments.size()) {
            DownloadSegment *seg = m_segments[id].data();
            if (seg) {
                QMetaObject::invokeMethod(seg, "resumeDownload", Qt::QueuedConnection);
            }
        }
        return;
    }

    // 重试耗尽：将当前块放回队列，并唤醒/创建线程
    qWarning() << "Segment" << id << "failed after max retries, re-enqueuing block";

    if (id >= 0 && id < m_segments.size()) {
        DownloadSegment *seg = m_segments[id].data();
        if (seg) {
            QPair<qint64, qint64> block = seg->currentBlock();
            if (block.first >= 0 && block.second >= 0) {
                QMutexLocker taskLocker(&m_taskMutex);
                m_tasks.enqueue(block);
                qDebug() << "Re-enqueued block:" << block.first << "-" << block.second;
            }
            // 清理当前分片状态
            QMetaObject::invokeMethod(seg, "cleanupReply", Qt::BlockingQueuedConnection);
        }
    }

    // 延迟检查并确保有线程处理队列
    QTimer::singleShot(50, this, [this]() {
        QMutexLocker locker(&m_mutex);
        if (m_canceled.load()) return;

        // 检查是否有活跃线程
        bool hasActive = false;
        for (const auto &ptr : m_threads) {
            QThread *t = ptr.data();
            if (t && t->isRunning()) {
                hasActive = true;
                break;
            }
        }

        QMutexLocker taskLocker(&m_taskMutex);
        if (!m_tasks.isEmpty() && !hasActive) {
            qDebug() << "All threads idle but tasks remain, starting new thread";
            // 创建一个新线程来处理剩余任务
            int newId = m_segments.size();
            DownloadSegment *seg = new DownloadSegment(newId, m_url, m_tempDir, this);
            m_segments.append(QPointer<DownloadSegment>(seg));
            QThread *thread = new QThread();
            seg->moveToThread(thread);
            // 连接信号（与原有逻辑一致）
            connect(thread, &QThread::started, seg, &DownloadSegment::fetchNextBlock, Qt::QueuedConnection);
            connect(seg, &DownloadSegment::progress, this, &DownloadWorker::onSegmentProgress, Qt::QueuedConnection);
            connect(seg, &DownloadSegment::blockFinished, this, &DownloadWorker::onBlockFinished, Qt::QueuedConnection);
            connect(seg, &DownloadSegment::error, this, &DownloadWorker::onSegmentError, Qt::QueuedConnection);
            connect(seg, &DownloadSegment::finished, this, &DownloadWorker::onSegmentFinished, Qt::QueuedConnection);
            connect(thread, &QThread::finished, seg, &QObject::deleteLater);
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            m_threads.append(QPointer<QThread>(thread));
            thread->start();
        }
    });
}

void DownloadWorker::onBlockFinished(int id, qint64 start, qint64 end)
{
    Q_UNUSED(id);
    Q_UNUSED(start);
    Q_UNUSED(end);
    int completed = ++m_completedBlocks;  // 原子递增
    qDebug() << "Block finished, completed:" << completed << "/" << m_totalBlocks;
    if (completed == m_totalBlocks) {
        qDebug() << "All blocks finished, merging...";
        mergeFiles();
    }
}


void DownloadWorker::updateSpeed()
{
    QMutexLocker locker(&m_mutex);
    qint64 bytesDiff = m_downloadedSize - m_lastSpeedBytes;
    // 定时器间隔 1000ms，所以速度 = 字节差
    m_speed = bytesDiff;
    m_lastSpeedBytes = m_downloadedSize;
    locker.unlock();

    emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
}


QPair<qint64, qint64> DownloadWorker::takeBlock()
{
    QMutexLocker locker(&m_taskMutex);
    qDebug() << "takeBlock called, queue size:" << m_tasks.size();
    if (m_tasks.isEmpty())
        return qMakePair(-1LL, -1LL);
    auto block = m_tasks.dequeue();
    qDebug() << "returning block:" << block.first << block.second << ", remaining:" << m_tasks.size();
    return block;
}


void DownloadWorker::cancelDownload()
{
    bool expected = false;
    if (!m_canceled.compare_exchange_strong(expected, true)) {
        return;
    }

    qDebug() << "Canceling download for" << m_fileName;

    m_speedUpdateTimer->stop();

    // 通知所有分片取消
    for (const auto &ptr : qAsConst(m_segments)) {
        DownloadSegment *seg = ptr.data();
        if (seg) {
            QMetaObject::invokeMethod(seg, "cancelDownload", Qt::BlockingQueuedConnection);
        }
    }

    // 退出所有分片线程并等待
    for (const auto &ptr : qAsConst(m_threads)) {
        QThread *thread = ptr.data();
        if (thread && thread->isRunning()) {
            thread->quit();
            if (!thread->wait(2000)) {
                thread->terminate();
                thread->wait();
            }
        }
    }

    {
        QMutexLocker locker(&m_mutex);
        m_downloading = false;
        m_threads.clear();
        m_segments.clear();
    }

    cleanupTemp();
    emit canceled();
}


void DownloadWorker::mergeFiles()
{
    qDebug() << "Merging files for" << m_fileName;
    emit mergeStarted();   // 通知 UI 开始合并

    QString finalPath = QString("%1/%2").arg(m_savePath, m_fileName);
    if (QFile::exists(finalPath))
        QFile::remove(finalPath);

    QFile outFile(finalPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("无法创建输出文件：%1 (%2)").arg(finalPath, outFile.errorString()));
        return;
    }

    // 获取所有块临时文件
    QDir dir(m_tempDir);
    QStringList filters;
    filters << "block_*.tmp";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    // 按起始偏移数值排序
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        qint64 startA = a.baseName().section('_', 1, 1).toLongLong();
        qint64 startB = b.baseName().section('_', 1, 1).toLongLong();
        return startA < startB;
    });

    const qint64 bufSize = 1024 * 1024;
    QByteArray buffer;
    buffer.reserve(bufSize);

    // 合并相关
    int totalFiles = files.size();
    int processed = 0;

    for (const QFileInfo &fi : files) {
        if (m_canceled.load()) {
            outFile.close();
            QFile::remove(finalPath);
            return;
        }


        // **发射合并进度**
        int percent = (totalFiles > 0) ? (processed * 100 / totalFiles) : 100;
        emit mergeProgress(percent);


        // 验证文件名中的预期大小与实际文件大小是否一致
        qint64 start = fi.baseName().section('_', 1, 1).toLongLong();
        qint64 end = fi.baseName().section('_', 2, 2).toLongLong();
        qint64 expectedSize = end - start + 1;
        if (fi.size() != expectedSize) {
            qWarning() << "Block file" << fi.fileName() << "size mismatch: expected" << expectedSize << "actual" << fi.size();
            // 可以选择继续合并（可能导致文件损坏）或报错停止。此处选择继续并警告。
        }

        QFile inFile(fi.absoluteFilePath());
        if (!inFile.open(QIODevice::ReadOnly)) {
            outFile.close();
            emit errorOccurred(QString("无法打开块文件：%1").arg(fi.absoluteFilePath()));
            return;
        }

        qint64 remaining = inFile.size();
        while (remaining > 0) {
            qint64 chunk = qMin(bufSize, remaining);
            buffer = inFile.read(chunk);
            if (buffer.size() != chunk) {
                outFile.close();
                emit errorOccurred("读取块文件失败");
                return;
            }
            if (outFile.write(buffer) != chunk) {
                outFile.close();
                emit errorOccurred("写入输出文件失败");
                return;
            }
            remaining -= chunk;
        }
        inFile.close();

        processed++;//合并进度++
    }

    // 最后确保100%
    emit mergeProgress(100);

    outFile.close();    //确保输出文件关闭
    cleanupTemp();
    qDebug() << "Merged file:" << finalPath;

    m_downloading = false;
    m_finished = true;
    emit finished();
}


void DownloadWorker::cleanupTemp()
{
    if (!m_tempDir.isEmpty()) {
        QDir td(m_tempDir);
        if (td.exists()) {
            td.removeRecursively();
        }
    }
}


int DownloadWorker::progress() const
{
    return m_fileSize > 0 ? static_cast<int>((m_downloadedSize * 100) / m_fileSize) : 0;
}


QString DownloadWorker::speed() const
{
    return DownloadUtils::formatSpeed(m_speed);
}


QString DownloadWorker::timeRemaining() const
{
    if (m_speed <= 0 || m_downloadedSize >= m_fileSize)
        return QStringLiteral("未知");

    qint64 remaining = m_fileSize - m_downloadedSize;
    int seconds = static_cast<int>(remaining / m_speed);
    return DownloadUtils::formatTimeFromSeconds(seconds);
}


qint64 DownloadWorker::totalTimeMs() const
{
    return m_totalTimeMs;
}
