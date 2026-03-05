#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QUrl>
#include <QQueue>
#include <QList>

class QTimer;
class DownloadItem;

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager();

    void addDownload(const QUrl &url, const QString &savePath);
    void removeItems(const QList<DownloadItem*> &items); // 新增声明

    void cancelAllDownloads();
    void waitForAll(int timeoutMs = 5000);

    double getOverallProgress() const;
    int maxConcurrentDownloads() const { return m_maxConcurrentDownloads; }
    void setMaxConcurrentDownloads(int count) { m_maxConcurrentDownloads = count; }

signals:
    void downloadAdded(DownloadItem *item);
    void downloadProgressChanged();
    void allDownloadsFinished();

private slots:
    void processQueue();
    void onDownloadProgress();
    void onDownloadFinished(DownloadItem *item);
    void onDownloadError(DownloadItem *item, const QString &error);
    void onDownloadCanceled(DownloadItem *item);
    void cleanupAllDownloads();

private:
    QTimer *m_queueWatcher;
    QList<DownloadItem*> m_downloads;
    QQueue<DownloadItem*> m_waitingQueue;
    int m_maxConcurrentDownloads; // 确保头文件里有这个成员变量
};

#endif // DOWNLOADMANAGER_H
