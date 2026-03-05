#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

private slots:
    void browseDownloadPath();
    void accept() override;

private:
    QLineEdit *m_pathEdit;
    QSpinBox *m_maxConcurrentSpin;
    QSpinBox *m_threadCountSpin;
    QPushButton *m_browseBtn;
    QPushButton *m_okBtn;
    QPushButton *m_cancelBtn;

    QSpinBox *m_maxRetriesSpin;

};

#endif // PREFERENCESDIALOG_H
