#include "mainwindow.h"
#include "downloaditem.h"
#include "downloadmanager.h"

#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>
#include <QTimer>

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSslConfiguration>
#include <QNetworkCookieJar>
#include <QPixmap>
#include <QPainter>
#include <QThread>
#include "preferencesdialog.h"
#include "preferences.h"

#include "aboutdialog.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_downloadManager(new DownloadManager(this))
    , m_statusUpdateTimer(nullptr)  // 在初始化列表中初始化
{
    // 先设置网络优化（会创建并保存 m_networkManager）
    setupNetworkOptimization();

    // 设置窗口图标
    // QIcon appIcon(":/app_icon.png");
    // if (appIcon.isNull()) {
    //     // 如果资源图标不存在，使用系统图标作为备用
    //     appIcon = QIcon::fromTheme("applications-internet");
    // }
    // setWindowIcon(appIcon);

    setupUI();
    setupMenuBar();
    setupTrayIcon();

    connect(m_downloadManager, &DownloadManager::downloadAdded,
            this, &MainWindow::onDownloadAdded);
    connect(m_downloadManager, &DownloadManager::downloadProgressChanged,
            this, &MainWindow::onDownloadProgressChanged);

    // 创建并启动状态更新定时器
    m_statusUpdateTimer = new QTimer(this);
    connect(m_statusUpdateTimer, &QTimer::timeout, this, &MainWindow::onDownloadProgressChanged);
    m_statusUpdateTimer->start(1000); // 每秒更新一次

    //setWindowTitle("多线程下载器");
    resize(700, 600);

}

MainWindow::~MainWindow()
{
    qDebug() << "~MainWindow() start";

    // 先隐藏托盘，避免托盘保持进程
    if (m_trayIcon) {
        m_trayIcon->hide();
    }

    // 取消并等待下载结束（DownloadManager::waitForAll 已实现/建议实现）
    if (m_downloadManager) {
        m_downloadManager->cancelAllDownloads();
        m_downloadManager->waitForAll(3000); // 等待最多3s
        delete m_downloadManager;
        m_downloadManager = nullptr;
    }

    // 中止并释放所有挂起的 replies
    if (m_networkManager) {
        const auto replies = m_networkManager->findChildren<QNetworkReply*>();
        for (QNetworkReply *r : replies) {
            if (r->isRunning()) r->abort();
            r->deleteLater();
        }
        m_networkManager->deleteLater();
        m_networkManager = nullptr;
    }

    qDebug() << "~MainWindow() end";
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    QHBoxLayout *addLayout = new QHBoxLayout();
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText("输入下载URL或拖拽文件到此");
    m_addButton = new QPushButton("添加下载", this);

    addLayout->addWidget(m_urlEdit);
    addLayout->addWidget(m_addButton);

    m_downloadList = new QListWidget(this);
    m_downloadList->setAlternatingRowColors(true);
    m_downloadList->setSelectionMode(QAbstractItemView::SingleSelection); // 单选模式

    QHBoxLayout *statusLayout = new QHBoxLayout();
    //m_statusLabel = new QLabel("当前状态: 就绪", this);
    m_overallProgress = new QProgressBar(this);
    m_overallProgress->setTextVisible(true);
    m_overallProgress->setMinimum(0);
    m_overallProgress->setMaximum(100);

    // statusLayout->addWidget(m_statusLabel);
    // statusLayout->addStretch();
    statusLayout->addWidget(new QLabel("总进度:"));
    statusLayout->addWidget(m_overallProgress);

    mainLayout->addLayout(addLayout);
    mainLayout->addWidget(m_downloadList);
    mainLayout->addLayout(statusLayout);

    // 连接信号
    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::addDownload);
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &MainWindow::addDownload);

    // 添加列表项选择变化的连接
    connect(m_downloadList, &QListWidget::itemSelectionChanged, this, &MainWindow::onItemSelectionChanged);
}

// 处理列表项选择变化
void MainWindow::onItemSelectionChanged()
{
    QListWidgetItem *currentItem = m_downloadList->currentItem();
    updateMenuActions(currentItem);
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("文件(&F)");

    QAction *addAction = fileMenu->addAction("添加下载(&A)");
    addAction->setShortcut(QKeySequence::New);
    connect(addAction, &QAction::triggered, this, &MainWindow::addDownload);

    fileMenu->addSeparator();

    QAction *clearAction = fileMenu->addAction("清除已完成(&C)");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearCompleted);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("退出(&X)");
    connect(exitAction, &QAction::triggered, this, &MainWindow::quitApplication);

    QMenu *downloadMenu = menuBar()->addMenu("下载(&D)");

    // 修改这里：连接正确的槽函数


    m_removeAction = downloadMenu->addAction("删除(&D)");
    m_removeAction->setEnabled(false);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::onRemoveDownload); // 连接删除功能

    downloadMenu->addSeparator();

    QMenu *toolsMenu = menuBar()->addMenu("工具(&T)");
    QAction *prefAction = toolsMenu->addAction("首选项(&P)");
    connect(prefAction, &QAction::triggered, this, &MainWindow::showPreferences);

    // 新增帮助菜单
    QMenu *helpMenu = menuBar()->addMenu("帮助(&H)");

    m_aboutAction = helpMenu->addAction("关于(&A)");
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    m_checkUpdateAction = helpMenu->addAction("检查更新(&U)");
    connect(m_checkUpdateAction, &QAction::triggered, this, &MainWindow::checkForUpdates);
}

void MainWindow::setupTrayIcon()
{
    // 1. 检查系统托盘是否可用
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "警告：当前系统不支持系统托盘";
        m_trayIcon = nullptr;
        return;
    }

    // 2. 创建托盘图标对象
    if (!m_trayIcon) {
        m_trayIcon = new QSystemTrayIcon(this);
    }

    // 3. 加载主图标（支持多尺寸）
    QIcon trayIcon;

    // 优先从资源文件加载
    trayIcon = QIcon(":/app_icon.png");

    // 如果资源图标加载失败，尝试从主题获取备用图标
    if (trayIcon.isNull() || trayIcon.availableSizes().isEmpty()) {
        qWarning() << "警告：资源图标 :/app_icon.png 加载失败，使用备用图标";
        trayIcon = QIcon::fromTheme("applications-internet",
                                    QIcon(":/default_icon.png"));
    }

    // 最后检查，如果还是空，创建一个简单的彩色图标作为保底
    if (trayIcon.isNull()) {
        qWarning() << "警告：所有图标加载失败，创建默认图标";
        QPixmap pixmap(64, 64);
        pixmap.fill(QColor(70, 130, 180)); // 钢蓝色
        QPainter painter(&pixmap);
        painter.setPen(Qt::white);
        painter.setBrush(Qt::white);
        painter.drawEllipse(10, 10, 44, 44);
        painter.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter, "DL");
        trayIcon = QIcon(pixmap);
    }

    // 4. 设置托盘图标（让 Qt 自动处理 DPI 和尺寸）
    m_trayIcon->setIcon(trayIcon);

    // 5. 设置托盘提示文本
    m_trayIcon->setToolTip(tr("多线程下载器 - 就绪"));

    // 6. 创建托盘菜单
    if (!m_trayMenu) {
        m_trayMenu = new QMenu(this);

        // 显示主窗口动作
        QAction *showAction = m_trayMenu->addAction(tr("显示主窗口"));
        showAction->setIcon(QIcon::fromTheme("window-show"));
        connect(showAction, &QAction::triggered, this, [this]() {
            show();
            raise();
            activateWindow();
            showNormal(); // 如果是最小化状态，恢复正常
        });

        m_trayMenu->addSeparator();

        // 显示/隐藏已完成项
        QAction *toggleCompletedAction = m_trayMenu->addAction(tr("显示已完成项"));
        toggleCompletedAction->setCheckable(true);
        toggleCompletedAction->setChecked(true);
        connect(toggleCompletedAction, &QAction::toggled, this, [this](bool checked) {
            // 这里可以添加逻辑来控制是否显示已完成的下载项
            qInfo() << "显示已完成项：" << checked;
        });

        m_trayMenu->addSeparator();

        // 退出动作
        QAction *quitAction = m_trayMenu->addAction(tr("退出"));
        quitAction->setIcon(QIcon::fromTheme("application-exit"));
        quitAction->setShortcut(QKeySequence::Quit);
        connect(quitAction, &QAction::triggered, this, &MainWindow::quitApplication);
    }

    m_trayIcon->setContextMenu(m_trayMenu);

    // 7. 连接托盘激活信号
    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayIconActivated);

    // 8. 连接消息点击信号（如果显示通知消息）
    connect(m_trayIcon, &QSystemTrayIcon::messageClicked,
            this, [this]() {
                qInfo() << "托盘消息被点击";
                show();
                raise();
                activateWindow();
            });

    // 9. 显示托盘图标
    m_trayIcon->show();

    qDebug() << "系统托盘初始化完成";
}


void MainWindow::addDownload()
{
    QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty()) {
        QMessageBox::warning(this, "警告", "请输入下载URL");
        return;
    }

    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        QMessageBox::warning(this, "警告", "无效的URL");
        return;
    }

    // 获取默认下载目录
    QString defaultPath = Preferences::getDefaultDownloadPath();
    QString savePath;

    // 检查默认目录是否可用
    QDir defaultDir(defaultPath);
    if (defaultDir.exists()) {
        // 默认目录存在，直接使用
        savePath = defaultPath;
    } else {
        // 默认目录不存在，让用户手动选择
        savePath = QFileDialog::getExistingDirectory(this, "选择保存目录", QDir::homePath());
        if (savePath.isEmpty()) {
            return;
        }
    }

    m_downloadManager->addDownload(qurl, savePath);
    m_urlEdit->clear();
}



void MainWindow::clearCompleted()
{
    QList<DownloadItem*> itemsToRemove;
    QList<int> rowsToRemove;

    // 逆序遍历，收集需要删除的项及其行号
    for (int i = m_downloadList->count() - 1; i >= 0; --i) {
        QListWidgetItem *item = m_downloadList->item(i);
        DownloadItem *ditem = item->data(Qt::UserRole).value<DownloadItem*>();
        if (ditem && (ditem->status().contains("已完成") ||
                      ditem->status().contains("失败") ||
                      ditem->status().contains("已取消"))) {
            itemsToRemove.append(ditem);
            rowsToRemove.append(i);
        }
    }

    if (itemsToRemove.isEmpty())
        return;

    // 1. 通知 DownloadManager 移除并删除这些项（指针将很快失效）
    m_downloadManager->removeItems(itemsToRemove);

    // 2. 从界面列表中删除对应的项（此时部件已被安排删除，我们只需删除列表项）
    for (int row : rowsToRemove) {
        delete m_downloadList->takeItem(row);
    }

    // 更新整体进度显示
    onDownloadProgressChanged();
}

void MainWindow::onDownloadAdded(DownloadItem *item)
{
    if (!item) return;

    // 创建列表项
    QListWidgetItem *listItem = new QListWidgetItem(m_downloadList);
    // 设置大小提示，确保列表项有足够空间
    listItem->setSizeHint(item->sizeHint());
    // 存储 DownloadItem 指针
    listItem->setData(Qt::UserRole, QVariant::fromValue(item));
    // 直接将 DownloadItem 设置为列表项的部件
    m_downloadList->setItemWidget(listItem, item);

    // 连接进度变化信号，用于更新整体进度
    connect(item, &DownloadItem::progressChanged, this, &MainWindow::onDownloadProgressChanged);
}

void MainWindow::onDownloadProgressChanged()
{
    m_overallProgress->setValue(static_cast<int>(m_downloadManager->getOverallProgress()));

    for (int i = 0; i < m_downloadList->count(); ++i) {
        QListWidgetItem *item = m_downloadList->item(i);
        QWidget *widget = m_downloadList->itemWidget(item);
        if (widget) widget->update();
    }
}

void MainWindow::createDownloadItemWidget(DownloadItem *item)
{
    Q_UNUSED(item);
    // we do creation in onDownloadAdded to avoid duplication
}

// 替换 MainWindow::onTrayIconActivated 为下面实现
#include <QElapsedTimer>

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    // 防抖：如果短时间内连续触发多个激活事件，忽略重复（避免 Trigger + DoubleClick 导致 toggle）
    static QElapsedTimer lastActivation;
    const int debounceMs = 400; // 时间阈值，400ms 内的重复事件忽略（可根据需要调整）

    if (lastActivation.isValid() && lastActivation.elapsed() < debounceMs) {
        // 如果是非常短时间的重复激活，优先以 DoubleClick 为准 —— 这里直接忽略重复 Trigger
        // 但仍然继续处理 DoubleClick，以保持响应
        if (reason == QSystemTrayIcon::Context) {
            // 右键 context 要正常显示菜单，Qt 一般自动处理，但这里保留以防特殊平台
        } else if (reason == QSystemTrayIcon::DoubleClick) {
            // fall-through to handle below
        } else {
            // 忽略重复的 Trigger 等
            return;
        }
    }

    lastActivation.restart();

    switch (reason) {
    case QSystemTrayIcon::Trigger: // 单击（多数桌面发出）
    case QSystemTrayIcon::DoubleClick: // 双击
        // 对单击/双击都仅做“显示&激活窗口”而不做 hide（避免 toggle 导致立即隐藏）
        if (!isVisible()) {
            show();
        }
        raise();
        activateWindow();
        break;

    case QSystemTrayIcon::Context: // 右键（显示菜单）
        // 通常 Qt 会自动显示托盘菜单；如果需要可以在这里手动弹出
        // if (m_trayMenu) m_trayMenu->popup(QCursor::pos());
        break;

    default:
        break;
    }
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // 可选：立即应用某些设置，例如最大并发下载数
        if (m_downloadManager) {
            m_downloadManager->setMaxConcurrentDownloads(Preferences::getMaxConcurrentDownloads());
        }
        // 线程数的设置在 DownloadItem 启动时使用，无需立即更新
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "MainWindow::closeEvent() called";

    // 如果托盘可用，点击 X 隐藏到托盘（默认行为）
    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        qDebug() << "Hidden to tray, event ignored";
    } else {
        // 确保先清理所有下载
        quitApplication();
        event->accept();
        qDebug() << "Application quit, event accepted";
    }
}

// 在 mainwindow.cpp 的 quitApplication() 函数中添加更多调试信息
void MainWindow::quitApplication()
{
    qDebug() << "MainWindow::quitApplication() called";

    static bool quitting = false;
    if (quitting) {
        qDebug() << "Already quitting, ignoring duplicate call";
        return;
    }
    quitting = true;

    // 停止状态更新定时器
    if (m_statusUpdateTimer) {
        qDebug() << "1. Stopping status update timer...";
        m_statusUpdateTimer->stop();
        delete m_statusUpdateTimer;
        m_statusUpdateTimer = nullptr;
        qDebug() << "   Status update timer stopped";
    }

    // 停止并清理下载管理器（先停止所有下载）
    if (m_downloadManager) {
        qDebug() << "2. Cancelling all downloads...";
        m_downloadManager->cancelAllDownloads();  // DownloadManager 的方法名没变
        qDebug() << "3. Waiting for all downloads to finish...";
        m_downloadManager->waitForAll(3000);

        qDebug() << "4. Deleting download manager...";
        delete m_downloadManager;
        m_downloadManager = nullptr;
        qDebug() << "   Download manager deleted";
    }

    // 隐藏托盘
    if (m_trayIcon) {
        qDebug() << "5. Hiding tray icon...";
        m_trayIcon->hide();
        delete m_trayIcon;
        m_trayIcon = nullptr;
        qDebug() << "   Tray icon hidden";
    }

    // 清理网络管理器
    if (m_networkManager) {
        qDebug() << "6. Cleaning up network manager...";
        const auto replies = m_networkManager->findChildren<QNetworkReply*>();
        qDebug() << "   Found" << replies.size() << "network replies";
        for (QNetworkReply *reply : replies) {
            if (reply && reply->isRunning()) {
                qDebug() << "   Aborting reply:" << reply;
                reply->abort();
                reply->deleteLater();
            }
        }

        QCoreApplication::processEvents();
        QThread::msleep(100);

        delete m_networkManager;
        m_networkManager = nullptr;
        qDebug() << "   Network manager cleaned up";
    }

    // 清理下载列表
    if (m_downloadList) {
        qDebug() << "7. Cleaning up download list...";
        m_downloadList->clear();
        qDebug() << "   Download list cleared";
    }

    QThread::msleep(200);
    QCoreApplication::processEvents();

    qDebug() << "8. MainWindow::quitApplication() completed, calling QApplication::quit()";
    QApplication::quit();
}

void MainWindow::setupNetworkOptimization()
{
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy proxy;
    proxy.setType(QNetworkProxy::NoProxy);
    QNetworkProxy::setApplicationProxy(proxy);

    m_networkManager = new QNetworkAccessManager(this);

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    QNetworkCookieJar *cookieJar = new QNetworkCookieJar(this);
    m_networkManager->setCookieJar(cookieJar);

    qDebug() << "网络优化设置完成";
}



// 删除当前选中的下载项
// mainwindow.cpp
void MainWindow::onRemoveDownload()
{
    QListWidgetItem *currentItem = m_downloadList->currentItem();
    if (!currentItem) {
        QMessageBox::information(this, "提示", "请先选择一个下载项");
        return;
    }

    QVariant data = currentItem->data(Qt::UserRole);
    if (data.canConvert<DownloadItem*>()) {
        DownloadItem *item = data.value<DownloadItem*>();
        if (item) {
            int result = QMessageBox::question(this, "确认删除",
                                               QString("确定要删除下载项 '%1' 吗？").arg(item->fileName()),
                                               QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes) {
                // 1. 通知 DownloadManager 移除（会自动 cancel 和 deleteLater）
                m_downloadManager->removeItems({item});

                // 2. 从界面列表中移除
                int row = m_downloadList->row(currentItem);
                // 界面部件会随着列表项删除而自动销毁，无需额外 deleteLater
                delete m_downloadList->takeItem(row);

                // 3. 更新菜单状态和总进度
                updateMenuActions(nullptr);
                onDownloadProgressChanged();
            }
        }
    }
}

// 更新菜单动作状态
void MainWindow::updateMenuActions(QListWidgetItem *item)
{
    if (!item) {
        m_removeAction->setEnabled(false);
        return;
    }
    DownloadItem *ditem = item->data(Qt::UserRole).value<DownloadItem*>();
    if (!ditem) {
        m_removeAction->setEnabled(false);
        return;
    }
    QString st = ditem->status();
    m_removeAction->setEnabled(true);
}

#include "mainwindow.h"
// 确保包含必要的头文件
#include <QMessageBox>
#include <QApplication>

void MainWindow::showAboutDialog()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::checkForUpdates()
{
    // 这里可以实现检查更新的逻辑，例如访问网络获取最新版本号
    // 简单提示：
    QMessageBox::information(this, tr("检查更新"), tr("当前已是最新版本。"));
}

