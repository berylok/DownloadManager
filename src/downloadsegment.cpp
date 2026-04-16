#include "downloadsegment.h"
#include "downloadworker.h"
#include <QThread>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QMutexLocker>

typedef QPair<qint64, qint64> BlockPair;

// 构造函数
DownloadSegment::DownloadSegment(int id, const QUrl &url, const QString &tempDir,
                                 DownloadWorker *worker, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_url(url)
    , m_tempDir(tempDir)
    , m_worker(worker)
    , m_currentStart(0)
    , m_currentEnd(-1)
    , m_downloaded(0)
    , m_canceled(false)
    , m_finished(false)
    , m_manager(new QNetworkAccessManager(this))
    , m_reply(nullptr)
{
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(WATCHDOG_TIMEOUT_MS);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, &DownloadSegment::onWatchdogTimeout);
}

DownloadSegment::~DownloadSegment()
{
    qDebug() << "DownloadSegment destructor for segment" << m_id;
    cleanupReply();
    if (m_watchdog) {
        m_watchdog->stop();
    }
    if (m_file.isOpen()) {
        m_file.close();
    }
}

void DownloadSegment::fetchNextBlock()
{
    qDebug() << "Segment" << m_id << "fetchNextBlock called";
    QMutexLocker locker(&m_mutex);
    if (m_canceled.load() || m_finished.load()) {
        qDebug() << "Segment" << m_id << "fetchNextBlock: canceled or finished, returning";
        return;
    }

    BlockPair block = m_worker->takeBlock();

    if (block.first == -1) {
        qDebug() << "Segment" << m_id << "no more blocks, finishing thread";
        m_finished.store(true);
        emit finished(m_id);
        QThread::currentThread()->quit();
        return;
    }

    qDebug() << "Segment" << m_id << "got block:" << block.first << "-" << block.second;

    m_currentStart = block.first;
    m_currentEnd = block.second;
    m_currentTempFile = QString("%1/block_%2_%3.tmp").arg(m_tempDir).arg(m_currentStart).arg(m_currentEnd);
    m_downloaded = 0;

    qDebug() << "Segment" << m_id << "starting block:" << m_currentStart << "-" << m_currentEnd;

    if (!openFile())
        return;

    setupRequest(m_currentStart, m_currentEnd);
    m_watchdog->start();
}





void DownloadSegment::cancelDownload()
{
    QMutexLocker locker(&m_mutex);
    if (m_canceled.load())
        return;

    qDebug() << "Segment" << m_id << "canceling";
    m_canceled.store(true);
    m_watchdog->stop();


    cleanupReply();

    if (m_file.isOpen())
        m_file.close();

    // 删除当前块的临时文件
    if (!m_currentTempFile.isEmpty() && QFile::exists(m_currentTempFile)) {
        QFile::remove(m_currentTempFile);
        qDebug() << "Segment" << m_id << "temp file removed";
    }

    emit canceled(m_id);
    QThread::currentThread()->quit();
}

void DownloadSegment::onReadyRead()
{
    QMutexLocker locker(&m_mutex);

    m_watchdog->start(); // 收到数据，重置看门狗

    QByteArray data = m_reply->readAll();
    if (!data.isEmpty()) {
        qint64 written = m_file.write(data);
        if (written > 0) {
            m_downloaded += written;
            emit progress(m_id, written);
        } else {
            qDebug() << "Segment" << m_id << "write error:" << m_file.errorString();
            emit error(m_id, QString("写入文件失败: %1").arg(m_file.errorString()));
            // 不立即取消，让重试机制处理
        }
    }
}

void DownloadSegment::onFinished()
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }

    if (!m_reply)
        return;

    if (m_reply->error() == QNetworkReply::NoError) {
        qint64 fileSize = m_file.size();
        qint64 expectedSize = m_currentEnd - m_currentStart + 1;
        if (fileSize >= expectedSize) {
            m_watchdog->stop();
            qDebug() << "Segment" << m_id << "block finished, size:" << fileSize;

            // 发出块完成信号，通知 worker 更新计数
            emit blockFinished(m_id, m_currentStart, m_currentEnd);

            // 继续取下一个块（注意：这里需要解锁，因为 fetchNextBlock 会再次加锁）
            locker.unlock();
            fetchNextBlock();
            return; // 注意：不退出线程，继续循环
        } else {
            emit error(m_id, QString("块下载不完整，预期 %1 字节，实际 %2 字节")
                                 .arg(expectedSize).arg(fileSize));
        }
    } else if (m_reply->error() == QNetworkReply::OperationCanceledError) {
        qDebug() << "Segment" << m_id << "operation canceled";
    } else {
        emit error(m_id, QString("网络错误: %1").arg(m_reply->errorString()));
    }

    cleanupReply();
}

void DownloadSegment::onError(QNetworkReply::NetworkError code)
{
    QMutexLocker locker(&m_mutex);
    if (!m_reply) return;

    if (code != QNetworkReply::OperationCanceledError) {
        emit error(m_id, QString("网络错误 %1: %2").arg(code).arg(m_reply->errorString()));
    }
}

bool DownloadSegment::openFile()
{
    QFileInfo fi(m_currentTempFile);
    QDir dir = fi.dir();
    if (!dir.exists() && !dir.mkpath(".")) {
        emit error(m_id, QString("无法创建临时目录: %1").arg(dir.absolutePath()));
        return false;
    }

    m_file.setFileName(m_currentTempFile);

    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (m_file.exists()) {
        qint64 existingSize = m_file.size();
        if (existingSize > 0) {
            m_currentStart += existingSize; // 调整当前块的起始位置（用于续传）
            m_downloaded = existingSize;
            mode = QIODevice::Append;
            qDebug() << "Segment" << m_id << "resuming block from byte" << m_currentStart
                     << ", existing size:" << existingSize;
        }
    }

    if (!m_file.open(mode)) {
        emit error(m_id, QString("无法打开文件 %1: %2")
                             .arg(m_currentTempFile, m_file.errorString()));
        return false;
    }

    return true;
}

void DownloadSegment::closeFile()
{
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
}

void DownloadSegment::setupRequest(qint64 start, qint64 end)
{
    if (!m_manager) {
        m_manager = new QNetworkAccessManager(this);
    }

    QNetworkRequest request(m_url);
    QString range = QString("bytes=%1-%2").arg(start).arg(end);
    request.setRawHeader("Range", range.toLatin1());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Qt Download Manager)");
    request.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, true);

    qDebug() << "Segment" << m_id << "requesting range:" << range;

    cleanupReply();

    m_reply = m_manager->get(request);

    // 使用直接连接，因为我们在同一个线程中
    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadSegment::onReadyRead, Qt::DirectConnection);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadSegment::onFinished, Qt::DirectConnection);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &DownloadSegment::onError, Qt::DirectConnection);
}

void DownloadSegment::cleanupReply()
{
    if (m_reply) {
        m_reply->disconnect();
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void DownloadSegment::onWatchdogTimeout()
{
    if (!m_canceled.load() && !m_finished.load()) {
        qWarning() << "Segment" << m_id << "watchdog timeout, retrying...";
        emit error(m_id, tr("下载超时，正在重试..."));
    }
}


void DownloadSegment::resumeDownload()
{
    QMutexLocker locker(&m_mutex);
    if (m_canceled.load() || m_finished.load()) return;
    if (m_currentStart < 0 || m_currentEnd < 0) {
        locker.unlock();
        fetchNextBlock();
        return;
    }
    cleanupReply();
    if (!openFile()) {
        emit error(m_id, "无法重新打开文件进行续传");
        return;
    }
    setupRequest(m_currentStart, m_currentEnd);
    m_watchdog->start();
}
