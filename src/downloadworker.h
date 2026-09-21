#ifndef DOWNLOADWORKER_H
#define DOWNLOADWORKER_H

#include "downloadsegment.h"
#include <QObject>
#include <QUrl>
#include <QString>
#include <QMutex>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QElapsedTimer>
#include <QQueue>
#include <QPair>
#include <QAtomicInt>
#include <atomic>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class DownloadSegment;
class QThread;

class DownloadWorker : public QObject
{
    Q_OBJECT

public:
    explicit DownloadWorker(QObject *parent = nullptr);
    ~DownloadWorker();

    Q_INVOKABLE void startDownload(const QUrl &url, const QString &savePath,
                                   int threadCount = 4, qint64 blockSize = 0);
    Q_INVOKABLE void cancelDownload();

    bool isFinished() const { return m_finished; }
    bool isDownloading() const { return m_downloading; }
    qint64 fileSize() const { return m_fileSize; }
    qint64 downloadedSize() const { return m_downloadedSize; }
    int progress() const;
    QString speed() const;
    QString timeRemaining() const;
    qint64 totalTimeMs() const;

    Q_INVOKABLE QPair<qint64, qint64> takeBlock();
    Q_INVOKABLE void returnBlock(qint64 start, qint64 end);
    bool hasResumableCache(const QUrl &url, const QString &savePath);

signals:
    void progressUpdated(qint64 downloaded, qint64 total, qint64 speed);
    void finished();
    void errorOccurred(const QString &error);
    void canceled();
    void mergeStarted();
    void mergeProgress(int percent);

    // 新增信号：用于向 Segment 回传网络数据及完成状态（跨线程）
    void dataToSegment(int segmentId, const QByteArray &data);
    void finishToSegment(int segmentId, bool success, const QString &errorMsg);

private slots:
    void onSegmentProgress(int id, qint64 delta);
    void onSegmentFinished(int id);
    void onSegmentError(int id, const QString &error);
    void onSegmentCanceled(int id);
    void onSegmentProgressSubtract(int id, qint64 bytes);   // 已有声明，需实现
    void updateSpeed();

    // 新增槽：处理 Segment 发起的下载请求
    void onSegmentRequestDownload(int segmentId, qint64 start, qint64 end,
                                  const QUrl &url, const QString &tempFile);
    // 新增槽：处理 QNetworkReply 的信号
    void onReplyReadyRead();
    void onReplyFinished();
    void onReplyError(QNetworkReply::NetworkError code);

private:
    bool fetchFileSize();
    void mergeFiles();
    void cleanupTemp();
    bool resumeFromCache();

    // 网络相关（共享）
    QNetworkAccessManager *m_sharedManager = nullptr;
    QMap<QNetworkReply*, int> m_replyToSegment;          // reply -> segmentId
    QMap<QNetworkReply*, QPair<qint64, qint64>> m_replyToRange; // 可选，用于校验

    // 下载参数
    QUrl m_url;
    QString m_savePath;
    QString m_fileName;
    QString m_tempDir;
    qint64 m_fileSize = 0;
    qint64 m_downloadedSize = 0;
    qint64 m_lastDownloadedSize = 0;
    qint64 m_speed = 0;
    int m_threadCount = 4;
    int m_finishedSegments = 0;

    std::atomic<bool> m_canceled{false};
    bool m_downloading = false;
    bool m_finished = false;

    QList<QPointer<DownloadSegment>> m_segments;
    QList<QPointer<QThread>> m_threads;
    QList<qint64> m_segmentProgress;
    QMap<int, int> m_retryCounts;

    QTimer *m_speedUpdateTimer;
    QDateTime m_startTime;
    QElapsedTimer m_elapsedTimer;
    qint64 m_lastSpeedBytes = 0;
    qint64 m_totalTimeMs = 0;
    int m_maxRetries = 3;

    mutable QMutex m_mutex;
    QMutex m_taskMutex;
    QQueue<QPair<qint64, qint64>> m_tasks;
    int m_totalBlocks = 0;
    QAtomicInt m_completedBlocks;

    bool m_isResuming = false;
    QAtomicInt m_resumedBlocks;

private:
    Q_INVOKABLE void returnBlock(qint64 start, qint64 end, bool needSplit);

    // 添加成员变量
private:
    std::atomic<qint64> m_nextStartPos{0};           // 下一个新块的起始偏移
    QMutex m_failedMutex;
    QQueue<QPair<qint64, qint64>> m_failedBlocks;    // 失败待重试的小块
    qint64 m_currentBlockSize;                       // 当前块大小（初始256KB）
    qint64 m_minBlockSize = 64 * 1024;
    qint64 m_maxBlockSize = 16 * 1024 * 1024;          // 最大16MB

    // 移除 m_tasks 和 m_totalBlocks（或者保留用于统计，但不再作为主要队列）

private:
    int m_consecutive403Errors = 0;
    bool m_useSingleThreadFallback = false;
    QNetworkReply *m_fallbackReply = nullptr;
    QFile m_fallbackFile;
    void onFallbackError(QNetworkReply::NetworkError code);
    void onFallbackFinished();
    void onFallbackReadyRead();
    void startSingleThreadFallback();

private:
    bool m_fallbackActive = false;
    int m_consecutiveEmptyBlocks = 0;

    void startFallbackSingleThread();
    QHash<QNetworkReply*, QElapsedTimer> m_replyTimers;
public slots:
    void abortBlock(int segmentId);




};

#endif // DOWNLOADWORKER_H
