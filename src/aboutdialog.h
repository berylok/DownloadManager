#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>
#include <QPainter>
#include <QStyle>

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    void setupUI();
    QWidget* createAboutPage();
    QWidget* createLicensePage();
    QWidget* createThirdPartyPage();

    QTabWidget *m_tabWidget;
    QPushButton *m_okBtn;
};

#endif // ABOUTDIALOG_H
