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
#include <QHash>
#include <QElapsedTimer>

#include <utility>   // for std::as_const

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
    , m_fileSize(0)
    , m_finished(false)
    , m_downloadedSize(0)
    , m_speed(0)
    , m_threadCount(1)
    , m_finishedSegments(0)
    , m_maxRetries(MAX_RETRIES)
    , m_speedUpdateTimer(new QTimer(this))
    , m_totalBlocks(0)
{
    m_elapsedTimer.invalidate();

    // 创建共享的 QNetworkAccessManager 并限制并发连接数
    m_sharedManager = new QNetworkAccessManager(this);


    connect(m_speedUpdateTimer, &QTimer::timeout, this, &DownloadWorker::updateSpeed);
    m_speedUpdateTimer->setInterval(1000);
    m_speedUpdateTimer->setSingleShot(false);
}

DownloadWorker::~DownloadWorker()
{
    qDebug() << "DownloadWorker destructor for" << m_fileName;
    cleanupTemp();
    if (m_downloading && !m_finished) {
        cancelDownload();
    }
}

void DownloadWorker::startDownload(const QUrl &url, const QString &savePath,
                                   int threadCount, qint64 blockSize)
{
    m_useSingleThreadFallback = false;
    m_consecutive403Errors = 0;

    QMutexLocker locker(&m_mutex);
    if (m_downloading || m_finished) {
        qWarning() << "Download already in progress or finished";
        return;
    }

    // ===== ① 重置状态 =====
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

    // ===== ② 只初始化范围，不算块大小 =====
    m_minBlockSize = 64 * 1024;
    m_maxBlockSize = 8 * 1024 * 1024;
    m_currentBlockSize = m_minBlockSize;   // 临时值

    // ===== ③ 文件名 =====
    QString path = url.path();
    m_fileName = DownloadUtils::sanitizeFileName(QFileInfo(path).fileName());
    if (m_fileName.isEmpty()) m_fileName = "download_file";

    // ===== ④ 保存目录 =====
    QDir saveDir(savePath);
    if (!saveDir.exists() && !saveDir.mkpath(".")) {
        locker.unlock();
        emit errorOccurred(QString("无法创建保存目录：%1").arg(savePath));
        return;
    }

    locker.unlock();

    // ===== ⑤ 获取文件大小（重试）——fetchFileSize 只在这里调 =====
    int maxRetries = 3;
    int retryCount = 0;
    bool sizeFetched = false;
    while (!sizeFetched && retryCount < maxRetries) {
        if (retryCount > 0) {
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

    // ===== ⑥ ★块大小计算放这里★ =====
    if (blockSize > 0) {
        m_currentBlockSize = qBound(m_minBlockSize, blockSize, m_maxBlockSize);
    } else {
        qint64 target = m_fileSize / qMax(1, m_threadCount * 15);
        m_currentBlockSize = qBound(m_minBlockSize, target, m_maxBlockSize);
    }

    // ===== ⑦ 临时目录 =====
    m_tempDir = QString("%1/%2_blocks").arg(QDir::tempPath(), m_fileName);
    QDir td(m_tempDir);
    if (!td.exists() && !td.mkpath(".")) {
        locker.unlock();
        emit errorOccurred(QString("无法创建临时目录：%1").arg(m_tempDir));
        return;
    }

    // ===== ⑧ 初始化块生成器 =====
    m_nextStartPos = 0;
    {
        QMutexLocker failedLocker(&m_failedMutex);
        m_failedBlocks.clear();
    }
    m_totalBlocks = (m_fileSize + m_currentBlockSize - 1) / m_currentBlockSize;
    qDebug() << "Estimated total blocks:" << m_totalBlocks
             << "block size:" << m_currentBlockSize;

    m_segmentProgress.clear();
    m_segmentProgress.resize(m_threadCount, 0);

    m_speedUpdateTimer->start();

    // 创建指定数量的 DownloadSegment
    for (int i = 0; i < m_threadCount; ++i) {
        DownloadSegment *seg = new DownloadSegment(i, m_url, m_tempDir, this);
        m_segments.append(QPointer<DownloadSegment>(seg));
        QThread *thread = new QThread();
        seg->moveToThread(thread);

        // 跨线程信号连接
        connect(thread, &QThread::started, seg, &DownloadSegment::fetchNextBlock, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::progress, this, &DownloadWorker::onSegmentProgress, Qt::QueuedConnection);

        connect(seg, &DownloadSegment::error, this, &DownloadWorker::onSegmentError, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::finished, this, &DownloadWorker::onSegmentFinished, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::canceled, this, &DownloadWorker::onSegmentCanceled, Qt::QueuedConnection);
        connect(seg, &DownloadSegment::progressSubtract, this, &DownloadWorker::onSegmentProgressSubtract, Qt::QueuedConnection);

        // 请求下载块
        connect(seg, &DownloadSegment::requestDownloadBlock,
                this, &DownloadWorker::onSegmentRequestDownload, Qt::QueuedConnection);

        // 回传数据和完成状态
        connect(this, &DownloadWorker::dataToSegment, seg, &DownloadSegment::onDataReceived, Qt::QueuedConnection);
        connect(this, &DownloadWorker::finishToSegment, seg, &DownloadSegment::onBlockDownloadFinished, Qt::QueuedConnection);

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
    //request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Qt Download Manager)");

    QNetworkReply *reply = manager.head(request);
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(10000);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished()) {
        timeoutTimer.stop();
        reply->abort();
        qWarning() << "HEAD request timeout after 10 seconds";
        reply->deleteLater();
        return false;
    }
    timeoutTimer.stop();

    if (reply->error() == QNetworkReply::NoError) {
        // 成功处理...
        QString acceptRanges = reply->rawHeader("Accept-Ranges");
        if (acceptRanges != "bytes") {
            qDebug() << "Server does not support range requests, fallback to single thread";
            m_threadCount = 1;
        }
        m_fileSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        reply->deleteLater();
        return m_fileSize > 0;
    } else {
        qWarning() << "HEAD request error:" << reply->error() << reply->errorString();
        reply->deleteLater();
    }

    // 降级尝试 GET Range:0-0
    qDebug() << "HEAD request failed, trying GET with Range: bytes=0-0";
    request.setRawHeader("Range", "bytes=0-0");
    QNetworkReply *getReply = manager.get(request);
    QTimer timeoutTimer2;
    timeoutTimer2.setSingleShot(true);
    connect(&timeoutTimer2, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer2.start(10000);
    connect(getReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (!getReply->isFinished()) {
        timeoutTimer2.stop();
        getReply->abort();
        qWarning() << "GET request timeout after 10 seconds";
        getReply->deleteLater();
        return false;
    }
    timeoutTimer2.stop();

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
    } else {
        qWarning() << "GET request error:" << getReply->error() << getReply->errorString();
        getReply->deleteLater();
        return false;
    }
}

void DownloadWorker::onSegmentProgress(int id, qint64 bytesReceived)
{
    QMutexLocker locker(&m_mutex);
    if (id < 0 || id >= m_segmentProgress.size()) return;
    m_segmentProgress[id] += bytesReceived;
    m_downloadedSize += bytesReceived;
    locker.unlock();
    emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
}

void DownloadWorker::onSegmentFinished(int id)
{
    qDebug() << "Segment thread" << id << "finished (no more blocks)";

    int count = 0;
    int total = 0;
    {
        QMutexLocker locker(&m_mutex);
        count = ++m_finishedSegments;
        total = m_segments.size();
    }

    if (total > 0 && count >= total) {
        qDebug() << "All segments finished, merging...";
        mergeFiles();      // 锁外调，避免 emit 期间持锁
    }
}

void DownloadWorker::onSegmentCanceled(int id)
{
    qDebug() << "Segment" << id << "canceled";
    QMutexLocker locker(&m_mutex);
    if (id >= 0 && id < m_threads.size()) {
        QThread *thread = m_threads[id].data();
        if (thread && thread->isRunning())
            thread->quit();
    }
}

void DownloadWorker::onSegmentError(int id, const QString &error)
{
    qDebug() << "Segment" << id << "error:" << error;
    // 错误已由 Segment 内部处理（重试或放回队列），Worker 只需记录
    // 保留原有重试逻辑的简化版：仅当重试次数耗尽时可能需要额外动作
    QMutexLocker locker(&m_mutex);
    if (m_canceled.load()) return;

    int retries = m_retryCounts.value(id, 0);
    if (retries < m_maxRetries) {
        m_retryCounts[id] = retries + 1;
        // Segment 自己会重试，无需额外操作
    } else {
        qWarning() << "Segment" << id << "failed after max retries";
        // 可以在此处通知 UI 某个块失败，但不中断整个下载
    }
}

void DownloadWorker::updateSpeed()
{
    if (m_finished) return;   // ★ 已完成，不再发进度
    QMutexLocker locker(&m_mutex);
    qint64 bytesDiff = m_downloadedSize - m_lastSpeedBytes;
    m_speed = bytesDiff;
    m_lastSpeedBytes = m_downloadedSize;
    locker.unlock();
    emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
}

QPair<qint64, qint64> DownloadWorker::takeBlock()
{
    // 优先处理失败队列
    {
        QMutexLocker locker(&m_failedMutex);
        if (!m_failedBlocks.isEmpty()) {
            auto block = m_failedBlocks.dequeue();
            qDebug() << "takeBlock from failed queue:" << block.first << "-" << block.second;
            return block;
        }
    }

    // 动态生成新块
    qint64 start = m_nextStartPos.fetch_add(m_currentBlockSize);
    if (start >= m_fileSize)
        return qMakePair(-1LL, -1LL);
    qint64 end = qMin(start + m_currentBlockSize - 1, m_fileSize - 1);
    qDebug() << "takeBlock new block:" << start << "-" << end;
    return qMakePair(start, end);
}

void DownloadWorker::returnBlock(qint64 start, qint64 end)
{
    QMutexLocker locker(&m_taskMutex);
    m_tasks.enqueue(qMakePair(start, end));
    qDebug() << "Block returned to queue:" << start << "-" << end;
}

void DownloadWorker::cancelDownload()
{
    bool expected = false;
    if (!m_canceled.compare_exchange_strong(expected, true))
        return;

    qDebug() << "Canceling download for" << m_fileName;
    m_speedUpdateTimer->stop();

    // 中止所有正在进行的网络请求
    for (auto reply : m_replyToSegment.keys()) {
        reply->abort();
    }

    if (m_fallbackReply) {                     // ← 新增
        if (m_fallbackReply->isRunning())
            m_fallbackReply->abort();
    }


    for (const auto &ptr : std::as_const(m_segments)) {
        DownloadSegment *seg = ptr.data();
        if (seg) {
            QMetaObject::invokeMethod(seg, "cancelDownload", Qt::BlockingQueuedConnection);
        }
    }

    for (const auto &ptr : std::as_const(m_threads)) {
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
    emit mergeStarted();

    QString finalPath = QString("%1/%2").arg(m_savePath, m_fileName);
    if (QFile::exists(finalPath))
        QFile::remove(finalPath);

    QFile outFile(finalPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("无法创建输出文件：%1 (%2)").arg(finalPath, outFile.errorString()));
        return;
    }

    QDir dir(m_tempDir);
    QStringList filters;
    filters << "block_*.tmp";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        qint64 startA = a.baseName().section('_', 1, 1).toLongLong();
        qint64 startB = b.baseName().section('_', 1, 1).toLongLong();
        return startA < startB;
    });

    const qint64 bufSize = 1024 * 1024;
    QByteArray buffer;
    buffer.reserve(bufSize);

    int totalFiles = files.size();
    int processed = 0;

    for (const QFileInfo &fi : files) {
        if (m_canceled.load()) {
            outFile.close();
            QFile::remove(finalPath);
            return;
        }

        emit mergeProgress((totalFiles > 0) ? (processed * 100 / totalFiles) : 100);

        qint64 start = fi.baseName().section('_', 1, 1).toLongLong();
        qint64 end = fi.baseName().section('_', 2, 2).toLongLong();
        qint64 expectedSize = end - start + 1;
        if (fi.size() != expectedSize) {
            qWarning() << "Block file" << fi.fileName() << "size mismatch: expected" << expectedSize << "actual" << fi.size();
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
        processed++;
    }

    emit mergeProgress(100);
    outFile.close();
    cleanupTemp();
    qDebug() << "Merged file:" << finalPath;

    m_downloading = false;
    m_finished = true;

    m_speedUpdateTimer->stop();
    m_totalTimeMs = m_elapsedTimer.elapsed();
    emit finished();
}

void DownloadWorker::cleanupTemp()
{
    if (!m_tempDir.isEmpty()) {
        QDir td(m_tempDir);
        if (td.exists())
            td.removeRecursively();
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

void DownloadWorker::onSegmentProgressSubtract(int id, qint64 bytes)
{
    QMutexLocker locker(&m_mutex);
    m_downloadedSize -= bytes;
    if (m_downloadedSize < 0) m_downloadedSize = 0;
    qDebug() << "Segment" << id << "subtracted" << bytes << "bytes, new total:" << m_downloadedSize;
    emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
}

// ========== 新增网络请求处理 ==========
void DownloadWorker::onSegmentRequestDownload(int segmentId, qint64 start, qint64 end,
                                              const QUrl &url, const QString &tempFile)
{
    if (m_canceled.load()) return;
    if (m_useSingleThreadFallback) return; // 已降级，不再接受分块请求

    Q_UNUSED(tempFile);
    QNetworkRequest request(url);
    QString range = QString("bytes=%1-%2").arg(start).arg(end);
    request.setRawHeader("Range", range.toLatin1());
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Qt Download Manager)");
    // request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = m_sharedManager->get(request);
    m_replyToSegment[reply] = segmentId;

    connect(reply, &QNetworkReply::readyRead, this, &DownloadWorker::onReplyReadyRead);
    connect(reply, &QNetworkReply::finished, this, &DownloadWorker::onReplyFinished);
    connect(reply, &QNetworkReply::errorOccurred, this, &DownloadWorker::onReplyError);

    QElapsedTimer t; t.start();
    m_replyTimers[reply] = t;
}

void DownloadWorker::onReplyReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    int segmentId = m_replyToSegment.value(reply, -1);
    if (segmentId == -1) return;

    QByteArray data = reply->readAll();
    if (!data.isEmpty()) {
        emit dataToSegment(segmentId, data);
    }
}

void DownloadWorker::onReplyFinished()
{


    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // ★ 打印本块耗时
    if (m_replyTimers.contains(reply)) {
        qint64 ms = m_replyTimers.take(reply).elapsed();
        int segId = m_replyToSegment.value(reply, -1);
        qDebug() << "reply finished seg" << segId << "耗时" << ms << "ms"
                 << "Connection头" << reply->rawHeader("Connection");
    }

    int segmentId = m_replyToSegment.value(reply, -1);
    if (segmentId != -1) {
        m_replyToSegment.remove(reply);
    }

    bool success = (reply->error() == QNetworkReply::NoError);
    QString errorMsg = success ? QString() : reply->errorString();

    // 检测 403 Forbidden
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403) {
        m_consecutiveEmptyBlocks++;
        if (m_consecutiveEmptyBlocks >= 3 && m_threadCount > 1) {
            qDebug() << "Too many 403, falling back to single thread";
            startSingleThreadFallback();
            reply->deleteLater();
            return;
        }
    } else {
        m_consecutiveEmptyBlocks = 0;
    }

    if (segmentId != -1) {
        emit finishToSegment(segmentId, success, errorMsg);
    }
    reply->deleteLater();
}

void DownloadWorker::onReplyError(QNetworkReply::NetworkError code)
{
    qDebug() << "Network error occurred:" << code;
}

void DownloadWorker::returnBlock(qint64 start, qint64 end, bool needSplit)
{
    if (!needSplit) {
        QMutexLocker locker(&m_failedMutex);
        m_failedBlocks.enqueue(qMakePair(start, end));
        qDebug() << "Block returned to queue (no split):" << start << "-" << end;
        return;
    }

    qint64 size = end - start + 1;
    if (size > m_minBlockSize * 2) {
        qint64 mid = start + size / 2;
        QMutexLocker locker(&m_failedMutex);
        m_failedBlocks.enqueue(qMakePair(start, mid - 1));
        m_failedBlocks.enqueue(qMakePair(mid, end));
        qDebug() << "Split block" << start << "-" << end << "into"
                 << start << "-" << mid-1 << "and" << mid << "-" << end;
        // 降低全局块大小（避免后续新块过大）
        m_currentBlockSize = qMax(m_minBlockSize, m_currentBlockSize / 2);
    } else {
        QMutexLocker locker(&m_failedMutex);
        m_failedBlocks.enqueue(qMakePair(start, end));
        qDebug() << "Block too small to split, returning as is:" << start << "-" << end;
    }
}

void DownloadWorker::startSingleThreadFallback()
{
    if (m_useSingleThreadFallback) return;
    m_useSingleThreadFallback = true;

    // 停分段但不设 m_canceled
    for (auto reply : m_replyToSegment.keys())
        if (reply->isRunning()) reply->abort();
    for (const auto &ptr : std::as_const(m_segments))
        if (auto *seg = ptr.data())
            QMetaObject::invokeMethod(seg, "cancelDownload", Qt::BlockingQueuedConnection);
    for (const auto &ptr : std::as_const(m_threads)) {
        QThread *t = ptr.data();
        if (t && t->isRunning()) { t->quit(); t->wait(2000); }
    }

    // 重新开始单线程下载
    qDebug() << "Starting single-thread fallback download for" << m_fileName;
    QString finalPath = QString("%1/%2").arg(m_savePath, m_fileName);
    m_fallbackFile.setFileName(finalPath);
    if (!m_fallbackFile.open(QIODevice::WriteOnly)) {
        emit errorOccurred("无法创建文件用于单线程下载");
        return;
    }

    QNetworkRequest request(m_url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Qt Download Manager)");
    // 不设置 Range 头，请求整个文件
    m_fallbackReply = m_sharedManager->get(request);
    connect(m_fallbackReply, &QNetworkReply::readyRead, this, &DownloadWorker::onFallbackReadyRead);
    connect(m_fallbackReply, &QNetworkReply::finished, this, &DownloadWorker::onFallbackFinished);
    connect(m_fallbackReply, &QNetworkReply::errorOccurred, this, &DownloadWorker::onFallbackError);
}

void DownloadWorker::onFallbackReadyRead()
{
    if (!m_fallbackReply || m_canceled.load()) return;
    QByteArray data = m_fallbackReply->readAll();
    if (!data.isEmpty()) {
        m_fallbackFile.write(data);
        m_downloadedSize += data.size();
        emit progressUpdated(m_downloadedSize, m_fileSize, m_speed);
    }
}

void DownloadWorker::onFallbackFinished()
{
    if (!m_fallbackReply) return;
    bool success = (m_fallbackReply->error() == QNetworkReply::NoError);
    m_fallbackFile.close();
    if (success) {
        qDebug() << "Single-thread fallback download finished";
        m_finished = true;
        m_totalTimeMs = m_elapsedTimer.elapsed();
        m_speedUpdateTimer->stop();
        emit finished();
    } else {
        if (!m_canceled.load()) {
            emit errorOccurred(QString("单线程下载失败: %1").arg(m_fallbackReply->errorString()));
        }
    }
    m_fallbackReply->deleteLater();
    m_fallbackReply = nullptr;
}

void DownloadWorker::onFallbackError(QNetworkReply::NetworkError code)
{
    qWarning() << "Fallback download error:" << code;
}

void DownloadWorker::startFallbackSingleThread() {
    m_fallbackActive = true;
    cancelDownload(); // 停止所有分块线程

    // 重置速度相关的变量
    m_lastSpeedBytes = 0;
    m_downloadedSize = 0;   // 注意：如果已有部分分块数据，可能需要保留，但降级后是全新下载，可以清零
    m_speed = 0;

    // 重新启动速度定时器
    m_speedUpdateTimer->start();

    // 重新打开最终文件（覆盖）
    QString finalPath = QString("%1/%2").arg(m_savePath, m_fileName);
    m_fallbackFile.setFileName(finalPath);
    if (!m_fallbackFile.open(QIODevice::WriteOnly)) { /* 错误处理 */ }
    QNetworkRequest request(m_url);
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    m_fallbackReply = m_sharedManager->get(request);
    connect(m_fallbackReply, &QNetworkReply::readyRead, this, &DownloadWorker::onFallbackReadyRead);
    connect(m_fallbackReply, &QNetworkReply::finished, this, &DownloadWorker::onFallbackFinished);
}


void DownloadWorker::abortBlock(int segmentId)
{
    QNetworkReply *target = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_replyToSegment.constBegin(); it != m_replyToSegment.constEnd(); ++it) {
            if (it.value() == segmentId) {
                target = it.key();
                break;
            }
        }
    }
    if (target && target->isRunning()) {
        target->abort();
    }
}
