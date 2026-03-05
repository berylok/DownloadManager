#ifndef DOWNLOADITEM_H
#define DOWNLOADITEM_H

#include <QWidget>
#include <QUrl>
#include <QPointer>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

class DownloadWorker;
class QThread;

class DownloadItem : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadItem(const QUrl &url, const QString &savePath, QWidget *parent = nullptr);
    ~DownloadItem();

    void start();
    void cancel();

    // 状态查询（返回纯数值/字符串，不带前缀）
    QString fileName() const;
    QString status() const;
    int progress() const;
    QString speed() const;
    QString fileSize() const;
    QString timeRemaining() const;

    bool isFinished() const;

signals:
    void progressChanged();
    void finished(DownloadItem* self);
    void error(DownloadItem* self, const QString &msg);
    void canceled(DownloadItem* self);

private slots:
    void onWorkerProgress(qint64 downloaded, qint64 total, qint64 speed);
    void onWorkerFinished();
    void onWorkerError(const QString &error);
    void onWorkerCanceled();

private:
    void setupUI();
    void updateLabels();

    QUrl m_url;
    QString m_savePath;
    DownloadWorker *m_worker = nullptr;
    QThread *m_workerThread = nullptr;

    QLabel *m_nameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelBtn = nullptr;

private:
    qint64 m_averageSpeed = 0;
    qint64 m_totalTimeMs = 0;
};

#endif // DOWNLOADITEM_H
