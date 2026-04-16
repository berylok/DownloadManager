#ifndef DOWNLOADSEGMENT_H
#define DOWNLOADSEGMENT_H

#include <QObject>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QMutex>
#include <atomic>
#include <QTimer>
#include <QPointer>

class DownloadWorker; // 前置声明

class DownloadSegment : public QObject
{
    Q_OBJECT
public:
    explicit DownloadSegment(int id, const QUrl &url, const QString &tempDir,
                             DownloadWorker *worker, QObject *parent = nullptr);
    ~DownloadSegment();

    Q_INVOKABLE void fetchNextBlock();
    Q_INVOKABLE void cancelDownload();

    // 状态查询
    bool isFinished() const { return m_finished; }
    bool isCanceled() const { return m_canceled; }
    int id() const { return m_id; }
    qint64 downloaded() const { return m_downloaded; }
public:
    // 返回当前正在下载的块范围，若未开始或已结束则返回 (-1, -1)
    QPair<qint64, qint64> currentBlock() const { return qMakePair(m_currentStart, m_currentEnd); }
signals:
    void progress(int id, qint64 bytes);
    void blockFinished(int id, qint64 start, qint64 end);
    void finished(int id);
    void error(int id, const QString &msg);
    void canceled(int id);   // 可以保留

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError);
    void onWatchdogTimeout();

private:
    bool openFile();
    void closeFile();
    void setupRequest(qint64 start, qint64 end);


    int m_id;
    QUrl m_url;
    QString m_tempDir;
    DownloadWorker *m_worker;

    qint64 m_currentStart;
    qint64 m_currentEnd;
    QString m_currentTempFile;
    qint64 m_downloaded;

    std::atomic<bool> m_canceled{false};
    std::atomic<bool> m_finished{false};

    mutable QMutex m_mutex;
    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply;
    QFile m_file;

    QTimer *m_watchdog;
    static const int WATCHDOG_TIMEOUT_MS = 30000;
public:
    Q_INVOKABLE void resumeDownload();
private:
    Q_INVOKABLE void cleanupReply();
};

#endif // DOWNLOADSEGMENT_H
