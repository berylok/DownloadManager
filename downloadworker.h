#ifndef DOWNLOADWORKER_H
#define DOWNLOADWORKER_H

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

class DownloadSegment;
class QThread;

class DownloadWorker : public QObject
{
    Q_OBJECT

public:
    explicit DownloadWorker(QObject *parent = nullptr);
    ~DownloadWorker();

    // 可被跨线程调用的方法
    Q_INVOKABLE void startDownload(const QUrl &url, const QString &savePath,
                                   int threadCount = 4, qint64 blockSize = 0);

    Q_INVOKABLE void cancelDownload();

    // 状态查询

    bool isFinished() const { return m_finished; }
    bool isDownloading() const { return m_downloading; }
    qint64 fileSize() const { return m_fileSize; }
    qint64 downloadedSize() const { return m_downloadedSize; }
    int progress() const;
    QString speed() const;
    QString timeRemaining() const;
    qint64 totalTimeMs() const;

    // 供 DownloadSegment 调用的公共方法
    Q_INVOKABLE QPair<qint64, qint64> takeBlock();

signals:
    void progressUpdated(qint64 downloaded, qint64 total, qint64 speed);
    void finished();
    void errorOccurred(const QString &error);
    void canceled();
    void mergeStarted();   // 新增：合并开始信号
    void mergeProgress(int percent);   // 新增：合并进度（0-100）

private slots:
    void onSegmentProgress(int id, qint64 delta);
    void onSegmentFinished(int id);
    void onSegmentError(int id, const QString &error);
    void onSegmentCanceled(int id);
    void onBlockFinished(int id, qint64 start, qint64 end);   // 新增：单个块完成
    void updateSpeed();

private:
    bool fetchFileSize();
    void mergeFiles();          // 改为合并所有块文件
    void cleanupTemp();

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

    // 原子状态标志
    std::atomic<bool> m_canceled{false};

    // 其他状态
    bool m_downloading = false;
    bool m_finished = false;

    // 分段管理（仍保留，用于暂停/恢复/取消等操作）
    QList<QPointer<DownloadSegment>> m_segments;
    QList<QPointer<QThread>> m_threads;

    QList<qint64> m_segmentProgress;          // 用于整体速度计算
    QMap<int, int> m_retryCounts;

    // 定时器
    QTimer *m_speedUpdateTimer;
    QDateTime m_startTime;
    QElapsedTimer m_elapsedTimer;
    qint64 m_lastSpeedBytes = 0;
    qint64 m_totalTimeMs = 0;
    int m_maxRetries = 3;

    // 互斥锁
    mutable QMutex m_mutex;

    // 动态分块新增成员
    QMutex m_taskMutex;                         // 保护任务队列
    QQueue<QPair<qint64, qint64>> m_tasks;      // 待下载块队列
    int m_totalBlocks = 0;                       // 总块数
    QAtomicInt m_completedBlocks;                 // 已完成块数
};

#endif // DOWNLOADWORKER_H
