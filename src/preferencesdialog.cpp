#include "preferencesdialog.h"
#include "preferences.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QDialogButtonBox>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("首选项"));

    // 创建控件
    QLabel *pathLabel = new QLabel(tr("默认下载目录:"));
    m_pathEdit = new QLineEdit;
    m_pathEdit->setText(Preferences::getDefaultDownloadPath());
    m_browseBtn = new QPushButton(tr("浏览..."));

    QLabel *maxLabel = new QLabel(tr("最大并发下载数:"));
    m_maxConcurrentSpin = new QSpinBox;
    m_maxConcurrentSpin->setRange(1, 10);
    m_maxConcurrentSpin->setValue(Preferences::getMaxConcurrentDownloads());

    QLabel *threadLabel = new QLabel(tr("每个下载的默认线程数:"));
    m_threadCountSpin = new QSpinBox;
    m_threadCountSpin->setRange(1, 20);
    m_threadCountSpin->setValue(Preferences::getDefaultThreadCount());


    // 新增：重试次数
    QLabel *retryLabel = new QLabel(tr("最大重试次数:"));
    m_maxRetriesSpin = new QSpinBox;
    m_maxRetriesSpin->setRange(0, 10);          // 0 表示不重试
    m_maxRetriesSpin->setValue(Preferences::getMaxRetries());



    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok |
                                                       QDialogButtonBox::Cancel);
    m_okBtn = buttonBox->button(QDialogButtonBox::Ok);
    m_cancelBtn = buttonBox->button(QDialogButtonBox::Cancel);

    // 布局
    // 布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(m_browseBtn);

    QHBoxLayout *maxLayout = new QHBoxLayout;
    maxLayout->addWidget(maxLabel);
    maxLayout->addWidget(m_maxConcurrentSpin);
    maxLayout->addStretch();

    QHBoxLayout *threadLayout = new QHBoxLayout;
    threadLayout->addWidget(threadLabel);
    threadLayout->addWidget(m_threadCountSpin);
    threadLayout->addStretch();

    // 新增重试次数的布局
    QHBoxLayout *retryLayout = new QHBoxLayout;
    retryLayout->addWidget(retryLabel);
    retryLayout->addWidget(m_maxRetriesSpin);
    retryLayout->addStretch();

    // 按顺序添加到主布局
    mainLayout->addLayout(pathLayout);
    mainLayout->addLayout(maxLayout);
    mainLayout->addLayout(threadLayout);
    mainLayout->addLayout(retryLayout);  // 新增行
    mainLayout->addStretch();
    mainLayout->addWidget(buttonBox);



    // 连接信号
    connect(m_browseBtn, &QPushButton::clicked, this, &PreferencesDialog::browseDownloadPath);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreferencesDialog::browseDownloadPath()
{
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    tr("选择默认下载目录"), m_pathEdit->text());
    if (!dir.isEmpty())
        m_pathEdit->setText(dir);
}

void PreferencesDialog::accept()
{
    // 保存已有设置
    Preferences::setDefaultDownloadPath(m_pathEdit->text());
    Preferences::setMaxConcurrentDownloads(m_maxConcurrentSpin->value());
    Preferences::setDefaultThreadCount(m_threadCountSpin->value());

    // 新增：保存重试次数
    Preferences::setMaxRetries(m_maxRetriesSpin->value());

    QDialog::accept();
}
