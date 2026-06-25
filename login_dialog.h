#pragma once
#include "gantry_data.h"
#include <QDialog>

constexpr int kReloginExitCode = 1000;

class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);

    AuthSession session() const { return m_session; }

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onLoginClicked();

private:
    bool performLogin(const QString &username, const QString &password, QString *errorOut);
    void setBusy(bool busy);
    void persistCredentials(const QString &username, const QString &password);

    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QCheckBox *m_rememberCheck = nullptr;
    QCheckBox *m_autoLoginCheck = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_loginBtn = nullptr;

    AuthSession m_session;
    bool m_autoLoginAttempted = false;
};
