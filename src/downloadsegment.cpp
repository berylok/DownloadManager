#include "downloadsegment.h"
#include "downloadworker.h"
#include <QThread>
#include <QDir>
#include <QDebug>
#include <QMutexLocker>

typedef QPair<qint64, qint64> BlockPair;

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
    , m_waitingForReply(false)
{
    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(WATCHDOG_TIMEOUT_MS);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, &DownloadSegment::onWatchdogTimeout);
}

DownloadSegment::~DownloadSegment()
{
    qDebug() << "DownloadSegment destructor for segment" << m_id;
    if (m_watchdog)
        m_watchdog->stop();
    if (m_file.isOpen())
        m_file.close();
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

    if (!openFile()) {
        // 打开文件失败，将块放回并继续
        m_worker->returnBlock(m_currentStart, m_currentEnd);
        locker.unlock();
        fetchNextBlock();
        return;
    }

    // 发射信号，请求 Worker 开始下载该块
    emit requestDownloadBlock(m_id, m_currentStart, m_currentEnd, m_url, m_currentTempFile);
    m_waitingForReply = true;
    m_watchdog->start();
}

void DownloadSegment::onDataReceived(int segmentId, const QByteArray &data)
{
    if (segmentId != m_id) return;
    if (m_canceled.load()) return;

    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) return;

    qint64 written = m_file.write(data);
    if (written > 0) {
        m_downloaded += written;
        emit progress(m_id, written);
        m_watchdog->start(); // 收到数据，重置看门狗
    } else {
        qWarning() << "Segment" << m_id << "write error:" << m_file.errorString();
        emit error(m_id, QString("写入文件失败: %1").arg(m_file.errorString()));
        // 这里不立即取消，让后续的 onBlockDownloadFinished 处理失败
    }
}

void DownloadSegment::onBlockDownloadFinished(int segmentId, bool success, const QString &errorMsg)
{
    if (m_canceled.load()) return;
    if (segmentId != m_id) return;
    m_waitingForReply = false;
    m_watchdog->stop();
    closeFile();

    if (success) {
        qint64 fileSize = m_file.size();
        qint64 expectedSize = m_currentEnd - m_currentStart + 1;
        if (fileSize >= expectedSize) {
            qDebug() << "Segment" << m_id << "block finished, size:" << fileSize;
            emit blockFinished(m_id, m_currentStart, m_currentEnd);
            // 继续取下一个块（必须在解锁后调用，避免死锁）
            QMetaObject::invokeMethod(this, "fetchNextBlock", Qt::QueuedConnection);
        } else {
            emit error(m_id, QString("块下载不完整，预期 %1 字节，实际 %2 字节")
                                 .arg(expectedSize).arg(fileSize));
            returnBlockAndContinue();
        }
    } else {
        emit error(m_id, errorMsg);
        returnBlockAndContinue();
    }
}

void DownloadSegment::cancelDownload()
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_canceled.load()) return;
        m_canceled.store(true);
        m_watchdog->stop();

        if (m_file.isOpen())
            m_file.close();

        if (!m_currentTempFile.isEmpty() && QFile::exists(m_currentTempFile)) {
            QFile::remove(m_currentTempFile);
        }
        emit canceled(m_id);
    }

    // 关键：通知 worker abort 这个 segment 的网络请求
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "abortBlock",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, m_id));
    }
    // 不要再 QThread::currentThread()->quit()
}

void DownloadSegment::onWatchdogTimeout()
{
    if (!m_canceled.load() && !m_finished.load() && m_waitingForReply) {
        qWarning() << "Segment" << m_id << "watchdog timeout, retrying...";
        emit error(m_id, tr("下载超时，正在重试..."));
        cleanupForRetry();
        returnBlockAndContinue();
    }
}

void DownloadSegment::cleanupForRetry()
{
    m_waitingForReply = false;
    // 注意：不需要主动 abort 网络请求，因为 Worker 会在 onBlockDownloadFinished 中处理超时失败
    if (m_file.isOpen())
        m_file.close();
}

void DownloadSegment::returnBlockAndContinue()
{
    // 通知 Worker 减去已下载但无效的进度
    if (m_downloaded > 0)
        emit progressSubtract(m_id, m_downloaded);
    if (m_currentStart >= 0 && m_currentEnd >= 0) {
        // 通知 worker 放回并拆分
        QMetaObject::invokeMethod(m_worker, "returnBlock",
                                  Qt::QueuedConnection,
                                  Q_ARG(qint64, m_currentStart),
                                  Q_ARG(qint64, m_currentEnd),
                                  Q_ARG(bool, true));   // 拆分失败块
    }

    closeFile();
    if (!m_currentTempFile.isEmpty() && QFile::exists(m_currentTempFile))
        QFile::remove(m_currentTempFile);

    m_downloaded = 0;
    m_currentStart = 0;
    m_currentEnd = -1;

    // 继续取下一个块（可能是放回的这个块）
    fetchNextBlock();
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
        if (existingSize > 0 && existingSize < (m_currentEnd - m_currentStart + 1)) {
            // 续传：调整起始位置并追加
            m_currentStart += existingSize;
            m_downloaded = existingSize;
            mode = QIODevice::Append;
            qDebug() << "Segment" << m_id << "resuming block from byte" << m_currentStart
                     << ", existing size:" << existingSize;
        } else if (existingSize >= (m_currentEnd - m_currentStart + 1)) {
            // 文件已经完整（可能之前下载完成但未通知），直接返回成功
            qDebug() << "Segment" << m_id << "temp file already complete, reusing";
            return true;
        } else {
            // 文件存在但大小异常，删除重建
            qWarning() << "Segment" << m_id << "invalid existing temp file, removing";
            m_file.remove();
            mode = QIODevice::WriteOnly;
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

