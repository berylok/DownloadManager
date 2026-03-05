#include "aboutdialog.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QTextStream>
#include <QDebug>
#include <QScrollArea>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
}

void AboutDialog::setupUI()
{
    setWindowTitle(tr("关于 多线程下载器"));
    setFixedSize(550, 600);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // 创建选项卡
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createAboutPage(), tr("关于"));
    m_tabWidget->addTab(createLicensePage(), tr("许可证"));
    m_tabWidget->addTab(createThirdPartyPage(), tr("第三方组件"));

    mainLayout->addWidget(m_tabWidget);

    // 按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_okBtn = new QPushButton(tr("确定"), this);
    m_okBtn->setFixedWidth(80);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_okBtn);

    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);
}

QWidget* AboutDialog::createAboutPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setSpacing(15);

    // 图标区域（保持不变）
    QLabel *iconLabel = new QLabel(page);
    QPixmap iconPixmap = QApplication::windowIcon().pixmap(80, 80);
    if (iconPixmap.isNull()) {
        iconPixmap = style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(80, 80);
    }
    if (iconPixmap.isNull()) {
        iconPixmap = QPixmap(80, 80);
        iconPixmap.fill(Qt::transparent);
        QPainter painter(&iconPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(46, 139, 87));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(4, 4, 72, 72);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 28, QFont::Bold));
        painter.drawText(iconPixmap.rect(), Qt::AlignCenter, "D");
    }
    iconLabel->setPixmap(iconPixmap);
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);

    // 标题
    QLabel *titleLabel = new QLabel(tr("多线程下载器"), page);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "font-size: 24px;"
        "font-weight: bold;"
        "color: #2E8B57;"
        "margin: 5px 0;");
    layout->addWidget(titleLabel);

    // 版本信息
    QLabel *versionLabel = new QLabel(
        tr("<div style='text-align:center;'>"
           "<b>版本：</b>%1<br>"
           "<b>发布日期：</b>2025年3月"
           "</div>").arg(QApplication::applicationVersion()), page);
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("color: #666; font-size: 12px; margin-bottom: 10px;");
    layout->addWidget(versionLabel);

    // 分隔线
    QFrame *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // ===== 可滚动的项目简介区域 =====
    // 创建一个滚动区域
    QScrollArea *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame); // 无边框
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 只垂直滚动

    // 内部容器 widget
    QWidget *contentWidget = new QWidget();
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(5, 5, 5, 5);

    // 项目简介文本（长文本）
    QLabel *descLabel = new QLabel(
        tr("<p style='line-height:1.6;'>"
           "一个基于 Qt6 的多线程下载管理器，支持：<br>"
           "✓ 断点续传 "
           "✓ 动态分块下载 "
           "✓ 任务队列管理 "
           "✓ 实时速度显示<br><br>"
           "本下载器旨在弥补浏览器单线程下载的不足，通过多线程技术显著提升下载速度。"
           "建议线程数设置为 4～8 条以获得最佳性能。<br><br>"
           "更多功能：支持暂停/恢复下载，支持取消下载，支持自定义并发数，支持线程数调节，"
           "支持代理设置，支持系统托盘，支持整体进度显示，支持单个下载项管理，支持清除已完成项，"
           "支持退出时等待下载完成，支持断点续传，支持动态分块，支持失败重试，支持速度限制等。"
           "</p>"), contentWidget);
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignLeft);

    contentLayout->addWidget(descLabel);
    contentLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    layout->addWidget(scrollArea);

    // 分隔线
    QFrame *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line2);

    // 作者信息
    QLabel *authorLabel = new QLabel(
        tr("<div style='text-align:center; color:#666; font-size:10px;'>"
           "<p><b>开发：</b>berylok (幸运人的珠宝)</p>"
           "<p><b>技术顾问：</b>Deepseek AI</p>"
           "<p>© 2025 多线程下载器项目</p>"
           "</div>"), page);
    authorLabel->setWordWrap(true);
    authorLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(authorLabel);

    layout->addStretch();
    return page;
}

QWidget* AboutDialog::createLicensePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    // 图标
    QLabel *iconLabel = new QLabel(page);
    QPixmap iconPixmap = style()->standardIcon(QStyle::SP_FileIcon).pixmap(32, 32);
    if (!iconPixmap.isNull()) {
        iconLabel->setPixmap(iconPixmap);
        iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(iconLabel);
    }

    // 标题
    QLabel *titleLabel = new QLabel(tr("GNU LGPL-3.0 许可证"), page);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #2E8B57;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 分隔线
    QFrame *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // 许可证说明
    QString licenseText = tr(
        "<p style='line-height:1.6;'>"
        "本软件采用 <b>GNU Lesser General Public License v3.0 (LGPL-3.0)</b> 开源协议。<br><br>"
        "<b>您可以自由地：</b>"
        "<ul>"
        "<li>✅ 在任何场景下免费使用本软件</li>"
        "<li>✅ 修改源代码以适应您的需求</li>"
        "<li>✅ 重新分发原始或修改后的版本</li>"
        "</ul>"
        "<b>但需遵守：</b>"
        "<ul>"
        "<li>📝 保留原始版权声明</li>"
        "<li>📝 注明对软件的修改</li>"
        "<li>📝 如果修改了 LGPL 库本身（而非使用），需开源修改</li>"
        "</ul>"
        "<br>基于 Qt6 框架开发，Qt 也在 LGPL-3.0 下授权。"
        "</p>"
        );
    QLabel *descLabel = new QLabel(licenseText, page);
    descLabel->setWordWrap(true);
    descLabel->setTextFormat(Qt::RichText);
    layout->addWidget(descLabel);

    // 完整许可证链接
    QLabel *linkLabel = new QLabel(
        tr("<p style='text-align:center; margin-top:10px;'>"
           "<a href='https://www.gnu.org/licenses/lgpl-3.0.html' style='color:#0066cc;'>"
           "📄 查看完整许可证文本</a></p>"), page);
    linkLabel->setOpenExternalLinks(true);
    linkLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(linkLabel);

    layout->addStretch();
    return page;
}

QWidget* AboutDialog::createThirdPartyPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);

    // Qt 图标
    QLabel *iconLabel = new QLabel(page);
    QPixmap iconPixmap = style()->standardIcon(QStyle::SP_DialogApplyButton).pixmap(32, 32);
    if (!iconPixmap.isNull()) {
        iconLabel->setPixmap(iconPixmap);
        iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(iconLabel);
    }

    // Qt 框架标题
    QLabel *qtTitle = new QLabel(tr("Qt Framework"), page);
    qtTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #2E8B57;");
    qtTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(qtTitle);

    // Qt 详细信息
    QString qtText = tr(
                         "<p>本软件基于 <a href='https://www.qt.io/'>Qt6</a> 开发，采用 LGPL-3.0 协议。</p>"
                         "<p><b>版本：</b> %1<br>"
                         "<b>模块：</b> Core, Widgets, Network, Concurrent</p>"
                         "<p><b>官方网站：</b><br>"
                         "<a href='https://www.qt.io/'>https://www.qt.io/</a></p>"
                         ).arg(QT_VERSION_STR);
    QLabel *qtDesc = new QLabel(qtText, page);
    qtDesc->setWordWrap(true);
    qtDesc->setOpenExternalLinks(true);
    qtDesc->setAlignment(Qt::AlignCenter);
    layout->addWidget(qtDesc);

    // 分隔线
    QFrame *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // 其他依赖
    QLabel *otherTitle = new QLabel(tr("其他依赖"), page);
    otherTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #2E8B57; margin-top: 10px;");
    otherTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(otherTitle);

    QLabel *otherDesc = new QLabel(
        tr("<p>本软件仅依赖 Qt 框架，无其他第三方库依赖。</p>"
           "<p style='margin-top:20px; color:#666; font-style:italic;'>"
           "感谢所有开源社区和贡献者！"
           "</p>"), page);
    otherDesc->setWordWrap(true);
    otherDesc->setAlignment(Qt::AlignCenter);
    layout->addWidget(otherDesc);

    layout->addStretch();
    return page;
}
