#include "downloaditem.h"
#include "downloadworker.h"
#include "downloadutils.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QThread>
#include <QFileInfo>
#include <QDebug>
#include <preferences.h>

DownloadItem::DownloadItem(const QUrl &url, const QString &savePath, QWidget *parent)
    : QWidget(parent)
    , m_url(url)
    , m_savePath(savePath)

    , m_worker(nullptr)      // 初始化指针
    , m_workerThread(nullptr)
    , m_averageSpeed(0)
    , m_totalTimeMs(0)
{
    setupUI();

    m_worker = new DownloadWorker();
    m_workerThread = new QThread();
    // 将 worker 移动到子线程
    m_worker->moveToThread(m_workerThread);
    // --- 新增：连接线程结束信号，确保 worker 安全删除 ---
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    m_workerThread->start();



    connect(m_worker, &DownloadWorker::progressUpdated,
            this, &DownloadItem::onWorkerProgress, Qt::QueuedConnection);
    connect(m_worker, &DownloadWorker::finished,
            this, &DownloadItem::onWorkerFinished, Qt::QueuedConnection);
    connect(m_worker, &DownloadWorker::errorOccurred,
            this, &DownloadItem::onWorkerError, Qt::QueuedConnection);
    connect(m_worker, &DownloadWorker::canceled,
            this, &DownloadItem::onWorkerCanceled, Qt::QueuedConnection);

    connect(m_worker, &DownloadWorker::mergeStarted,
            this, [this]() {
                m_statusLabel->setText("文件合并中...");
            }, Qt::QueuedConnection);

    // 合并开始
    connect(m_worker, &DownloadWorker::mergeStarted, this, [this]() {
        m_statusLabel->setText("合并中...");
        m_progressBar->setValue(0);
        m_progressBar->setFormat("合并中... 0%");
        // 设置合并时的样式表
        m_progressBar->setStyleSheet(
            "QProgressBar {"
            "   background-color: #C8E6C9;"    /* 背景：浅绿色（未完成部分） */
            "   border: 1px solid #81C784;"
            "   border-radius: 4px;"
            "   text-align: center;"
            "}"
            "QProgressBar::chunk {"
            "   background-color: #A5D6A7;"    /* 进度条前景：淡绿 */
            "}"
            );
    }, Qt::QueuedConnection);

    // 合并进度更新（保持样式不变）
    connect(m_worker, &DownloadWorker::mergeProgress, this, [this](int percent) {
        m_progressBar->setValue(percent);
        m_progressBar->setFormat(QString("合并中... %1%").arg(percent));
        m_speedLabel->setText("速度: -");
        m_timeLabel->setText("剩余时间: -");
    }, Qt::QueuedConnection);

    // // 下载完成时恢复默认样式（可选）
    // connect(m_worker, &DownloadWorker::finished, this, [this]() {
    //     // 清除样式表，恢复系统默认或下载完成时的样式
    //     m_progressBar->setStyleSheet("");
    //     // ... 其他已完成状态的更新
    // }, Qt::QueuedConnection);

}

DownloadItem::~DownloadItem()
{
    if (m_workerThread && m_workerThread->isRunning()) {
        // 1. 通知 worker 取消下载
        QMetaObject::invokeMethod(m_worker, "cancelDownload", Qt::QueuedConnection);

        // 2. 停止线程事件循环
        m_workerThread->quit();

        // 3. 等待线程结束 (最多等 3 秒)
        if (!m_workerThread->wait(3000)) {
            qWarning() << "Worker thread did not finish in time, terminating...";
            m_workerThread->terminate();
            m_workerThread->wait();
        }

        // 4. 清理工作
        // 注意：因为连接了 finished -> deleteLater，worker 和 thread 会自动删除
        // 但为了保险起见，如果 wait 成功，这里手动置空即可
        m_worker = nullptr;
        m_workerThread = nullptr;
    }
}

void DownloadItem::setupUI()
{
    m_nameLabel = new QLabel(fileName(), this);
    m_nameLabel->setStyleSheet("font-weight: bold;");

    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(true);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);

    m_statusLabel = new QLabel("等待中...", this);
    m_speedLabel = new QLabel("速度: 0 B/s", this);
    m_sizeLabel = new QLabel("  |大小: 计算中...", this);
    m_timeLabel = new QLabel("  |剩余时间: 未知", this);
    m_cancelBtn = new QPushButton("取消", this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &DownloadItem::cancel);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->addWidget(m_nameLabel);
    topLayout->addStretch();
    topLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_progressBar);

    QHBoxLayout *infoLayout = new QHBoxLayout();
    infoLayout->addWidget(m_sizeLabel);
    infoLayout->addWidget(m_speedLabel);
    infoLayout->addWidget(m_timeLabel);
    infoLayout->addStretch();
    mainLayout->addLayout(infoLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);
}

void DownloadItem::start()
{
    if (!m_worker) return;
    int threadCount = Preferences::getDefaultThreadCount();
    qDebug() << "DownloadItem start with threadCount:" << threadCount;
    QMetaObject::invokeMethod(m_worker, "startDownload", Qt::QueuedConnection,
                              Q_ARG(QUrl, m_url),
                              Q_ARG(QString, m_savePath),
                              Q_ARG(int, threadCount),
                              Q_ARG(qint64, 0));
    m_statusLabel->setText("下载中...");
}


void DownloadItem::cancel()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "cancelDownload", Qt::QueuedConnection);
    m_statusLabel->setText("已取消");
    m_progressBar->setValue(0);
    m_speedLabel->setText("  |速度: 0 B/s");
    m_timeLabel->setText("  |剩余时间: 已取消");
}

QString DownloadItem::fileName() const
{
    QString path = m_url.path();
    if (path.isEmpty()) return "未知文件";
    return QFileInfo(path).fileName();
}

QString DownloadItem::status() const { return m_statusLabel->text(); }
int DownloadItem::progress() const { return m_progressBar->value(); }
QString DownloadItem::fileSize() const { return m_sizeLabel->text().mid(3); } // 去掉“大小: ”


bool DownloadItem::isFinished() const { return m_worker && m_worker->isFinished(); }

QString DownloadItem::speed() const
{
    if (isFinished() && m_averageSpeed > 0) {
        return DownloadUtils::formatSpeed(m_averageSpeed);
    }
    return m_speedLabel->text().mid(3); // 去掉“速度: ”
}

QString DownloadItem::timeRemaining() const
{
    if (isFinished()) {
        return DownloadUtils::formatTimeFromMs(m_totalTimeMs);
    }
    return m_timeLabel->text().mid(5); // 去掉“剩余时间: ”
}

void DownloadItem::onWorkerProgress(qint64 downloaded, qint64 total, qint64 speed)
{
    //qDebug() << "onWorkerProgress: speed=" << speed;

    if (total > 0) {
        int percent = static_cast<int>((downloaded * 100) / total);
        m_progressBar->setValue(percent);
        m_progressBar->setFormat(QString("%1% (%2/%3)")
                                     .arg(percent)
                                     .arg(DownloadUtils::formatFileSize(downloaded))
                                     .arg(DownloadUtils::formatFileSize(total)));
        m_sizeLabel->setText(QString("大小: %1").arg(DownloadUtils::formatFileSize(total)));
    } else {
        m_sizeLabel->setText("大小: 未知");
    }

    m_speedLabel->setText(QString("  |速度: %1").arg(DownloadUtils::formatSpeed(speed)));

    if (speed > 0 && total > 0) {
        qint64 remaining = total - downloaded;
        int seconds = static_cast<int>(remaining / speed);
        m_timeLabel->setText(QString("  |剩余时间: %1").arg(DownloadUtils::formatTimeFromSeconds(seconds)));
    } else {
        m_timeLabel->setText("  |剩余时间: 计算中...");
    }

    emit progressChanged();
}

void DownloadItem::onWorkerFinished()
{
    qDebug() << "DownloadItem::onWorkerFinished() called for" << fileName();
    m_statusLabel->setText("已完成");
    m_progressBar->setValue(100);

    if (m_worker) {
        m_totalTimeMs = m_worker->totalTimeMs();
        qint64 totalBytes = m_worker->fileSize();
        if (m_totalTimeMs > 0 && totalBytes > 0) {
            m_averageSpeed = totalBytes * 1000 / m_totalTimeMs; // 字节/秒
        }
    }

    emit finished(this);
}

void DownloadItem::onWorkerError(const QString &errorMsg)  // 将参数名改为 errorMsg
{
    m_statusLabel->setText("下载失败");
    m_speedLabel->setText("  |速度: 0 B/s");
    m_timeLabel->setText("  |剩余时间: 失败");
    QMessageBox::warning(this, "下载错误", errorMsg);
    emit error(this, errorMsg);  // 这里就不会冲突了
}

void DownloadItem::onWorkerCanceled()
{
    m_statusLabel->setText("已取消");
    m_progressBar->setValue(0);
    m_speedLabel->setText("  |速度: 0 B/s");
    m_timeLabel->setText("  |剩余时间: 已取消");
    emit canceled(this);
}
