// main.cpp
#include <QApplication>
#include <QIcon>
#include "mainwindow.h"

#include <QRandomGenerator>
#include <QString>

int main(int argc, char *argv[])
{
    // // 强制使用XCB（X11）平台插件，通过XWayland运行
    // qputenv("QT_QPA_PLATFORM", "xcb");

    qRegisterMetaType<QPair<qint64, qint64>>();

    QApplication a(argc, argv);

    a.setApplicationName("DownloadManager");
    a.setOrganizationName("DownloadManager");
    // a.setApplicationDisplayName("多线程下载器1.63b2");

    int randomNum = QRandomGenerator::global()->generate();
    QString hexStr = QString::number(static_cast<unsigned int>(randomNum), 16).toUpper();  // 16 表示十六进制，toUpper() 转为大写
    QString newName = QString("多线程下载器1.66b-%1").arg(hexStr);
    a.setApplicationDisplayName(newName);

    a.setApplicationVersion("1.6.6");

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
