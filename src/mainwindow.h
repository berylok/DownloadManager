#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QNetworkAccessManager>
#include <QTimer>

class DownloadManager;
class DownloadItem;

QT_BEGIN_NAMESPACE
class QListWidget;
class QListWidgetItem;  // 添加这行
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
class QAction;
class QMenu;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void addDownload();
    void clearCompleted();
    void onDownloadAdded(DownloadItem *item);
    void onDownloadProgressChanged();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void showPreferences();

    // 彻底退出
    void quitApplication();

    // 单独控制下载项的槽函数

    void onRemoveDownload();

    // 列表项选择变化
    void onItemSelectionChanged();

    void showAboutDialog();
    void checkForUpdates();

private:
    void setupUI();
    void setupMenuBar();
    void setupTrayIcon();
    void createDownloadItemWidget(DownloadItem *item);
    void setupNetworkOptimization();
    void updateMenuActions(QListWidgetItem *item);  // 这个函数需要 QListWidgetItem 类型

    DownloadManager *m_downloadManager = nullptr;

    QListWidget *m_downloadList = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QPushButton *m_addButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_overallProgress = nullptr;

    // 托盘相关必须为成员，并且 parent 为 this
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_removeAction = nullptr;

    // 保存 network manager 以便退出时遍历 replies
    QNetworkAccessManager *m_networkManager = nullptr;

    // 添加状态更新定时器
    QTimer *m_statusUpdateTimer = nullptr;

private:
    QAction *m_aboutAction;
    QAction *m_checkUpdateAction;

};

#endif // MAINWINDOW_H
