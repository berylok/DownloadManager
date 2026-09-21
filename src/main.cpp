


#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

// #include <QRandomGenerator>
#include <QString>
#include <QPair>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

int main(int argc, char *argv[])
{
    // // 强制使用XCB（X11）平台插件，通过XWayland运行
    // qputenv("QT_QPA_PLATFORM", "xcb");

    qRegisterMetaType<QPair<qint64, qint64>>();
    QApplication a(argc, argv);

    a.setApplicationName("DownloadManager");
    a.setOrganizationName("DownloadManager");

    // const quint32 randomNum = QRandomGenerator::global()->generate();
    // const QString hexStr = QString::number(randomNum, 16).toUpper(); // 16 表示十六进制，toUpper() 转为大写
    // QString newName = QString("多线程下载器%1-%2").arg(APP_VERSION, hexStr);

    a.setApplicationVersion(APP_VERSION);

    QString newName = QString("多线程下载器%1").arg(APP_VERSION);
    a.setApplicationDisplayName(newName);


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

