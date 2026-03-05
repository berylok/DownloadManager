// main.cpp
#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    qRegisterMetaType<QPair<qint64, qint64>>();

    QApplication a(argc, argv);

    a.setApplicationName("DownloadManager");
    a.setOrganizationName("DownloadManager");
    a.setApplicationDisplayName("多线程下载器1.63b");
    a.setApplicationVersion("1.6.3");

#ifdef Q_OS_LINUX
    a.setDesktopFileName("download-manager");
#endif

    // 直接从资源加载图标（假设资源文件中有 app_icon.png）
    QIcon appIcon(":/app_icon.png");
    // 如果资源图标可能缺失，可以加一个简单的回退
    if (appIcon.isNull()) {
        qWarning() << "资源图标未找到，使用系统主题图标";
        appIcon = QIcon::fromTheme("applications-internet");
    }
    a.setWindowIcon(appIcon);

    MainWindow w;
    w.setWindowIcon(appIcon);
    w.show();

    return a.exec();
}
