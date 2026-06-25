#include "login_dialog.h"
#include "credential_store.h"
#include <QCheckBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
const char *kLoginStyle = R"(
QDialog { background-color: #1a1a20; }
QLabel { color: #d0d0d8; }
QLabel#loginTitle {
    color: #e8ecf4; font-size: 18pt; font-weight: bold;
}
QLabel#loginSub {
    color: #8090a8; font-size: 10pt;
}
QLabel#errorLabel {
    color: #ff7070; font-size: 10pt;
}
QLineEdit {
    background-color: #2a2a32; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 3px;
    padding: 8px 10px; min-height: 28px;
}
QPushButton {
    background-color: #333340; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 4px;
    padding: 8px 16px; min-height: 40px;
}
QPushButton:hover { background-color: #3d3d4a; border-color: #6a6a78; }
QPushButton:pressed { background-color: #2a2a32; }
QPushButton#loginBtn {
    background-color: #204020; border-color: #40a040; color: #c8ffc8;
    font-weight: bold;
}
QPushButton#loginBtn:hover { background-color: #285028; }
QCheckBox { color: #b0b0c0; spacing: 8px; }
)";

QString loginUrl(const QString &host, quint16 port) {
    return QStringLiteral("http://%1:%2/api/v1/auth/login").arg(host).arg(port);
}
} // namespace

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("PGM 旋转机架 — 登录"));
    setModal(true);
    setMinimumWidth(420);
    setStyleSheet(kLoginStyle);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(28, 24, 28, 24);

    auto *title = new QLabel(QStringLiteral("PGM 旋转机架控制系统"));
    title->setObjectName(QStringLiteral("loginTitle"));
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    auto *sub = new QLabel(QStringLiteral("请登录后进入上位机操作界面"));
    sub->setObjectName(QStringLiteral("loginSub"));
    sub->setAlignment(Qt::AlignCenter);
    root->addWidget(sub);

    auto *form = new QFormLayout;
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_hostEdit = new QLineEdit(CredentialStore::loadBackendHost());
    m_portEdit = new QLineEdit(QString::number(CredentialStore::loadBackendPort()));
    m_portEdit->setMaximumWidth(88);
    auto *hostRow = new QWidget;
    auto *hostLay = new QHBoxLayout(hostRow);
    hostLay->setContentsMargins(0, 0, 0, 0);
    hostLay->addWidget(m_hostEdit, 1);
    hostLay->addWidget(new QLabel(QStringLiteral(":")));
    hostLay->addWidget(m_portEdit);

    m_userEdit = new QLineEdit(CredentialStore::loadUsername());
    m_passEdit = new QLineEdit;
    m_passEdit->setEchoMode(QLineEdit::Password);
    if (CredentialStore::loadRememberPassword() && CredentialStore::hasStoredPassword())
        m_passEdit->setText(CredentialStore::loadPassword());

    form->addRow(QStringLiteral("后端"), hostRow);
    form->addRow(QStringLiteral("用户名"), m_userEdit);
    form->addRow(QStringLiteral("密码"), m_passEdit);
    root->addLayout(form);

    m_rememberCheck = new QCheckBox(QStringLiteral("记住密码"));
    m_rememberCheck->setChecked(CredentialStore::loadRememberPassword());
    m_autoLoginCheck = new QCheckBox(QStringLiteral("自动登录"));
    m_autoLoginCheck->setChecked(CredentialStore::loadAutoLogin());
    auto *optRow = new QHBoxLayout;
    optRow->addWidget(m_rememberCheck);
    optRow->addWidget(m_autoLoginCheck);
    optRow->addStretch();
    root->addLayout(optRow);

    m_errorLabel = new QLabel;
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    m_loginBtn = new QPushButton(QStringLiteral("登录"));
    m_loginBtn->setObjectName(QStringLiteral("loginBtn"));
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    root->addWidget(m_loginBtn);

    m_passEdit->setFocus();
    connect(m_passEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(m_userEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (m_autoLoginAttempted || !m_autoLoginCheck->isChecked())
        return;
    if (m_userEdit->text().trimmed().isEmpty() || m_passEdit->text().isEmpty())
        return;
    m_autoLoginAttempted = true;
    onLoginClicked();
}

void LoginDialog::setBusy(bool busy) {
    m_loginBtn->setEnabled(!busy);
    m_hostEdit->setEnabled(!busy);
    m_portEdit->setEnabled(!busy);
    m_userEdit->setEnabled(!busy);
    m_passEdit->setEnabled(!busy);
    m_rememberCheck->setEnabled(!busy);
    m_autoLoginCheck->setEnabled(!busy);
}

void LoginDialog::persistCredentials(const QString &username, const QString &password) {
    CredentialStore::saveUsername(username);
    CredentialStore::saveRememberPassword(m_rememberCheck->isChecked());
    CredentialStore::saveAutoLogin(m_autoLoginCheck->isChecked());
    bool okPort = false;
    const int port = m_portEdit->text().toInt(&okPort);
    if (okPort && port > 0 && port <= 65535)
        CredentialStore::saveBackendPort(static_cast<quint16>(port));
    CredentialStore::saveBackendHost(m_hostEdit->text().trimmed());

    if (m_rememberCheck->isChecked()) {
        if (!CredentialStore::savePassword(password))
            m_errorLabel->setText(QStringLiteral("警告：密码未能安全保存到本机"));
    } else {
        CredentialStore::clearPassword();
    }
}

bool LoginDialog::performLogin(const QString &username, const QString &password,
                               QString *errorOut) {
    const QString host = m_hostEdit->text().trimmed();
    bool okPort = false;
    const int port = m_portEdit->text().toInt(&okPort);
    if (host.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("请输入后端地址");
        return false;
    }
    if (!okPort || port <= 0 || port > 65535) {
        if (errorOut) *errorOut = QStringLiteral("端口号无效");
        return false;
    }

    QUrl url(loginUrl(host, static_cast<quint16>(port)));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(8000);

    QJsonObject body;
    body[QStringLiteral("username")] = username;
    body[QStringLiteral("password")] = password;

    QNetworkAccessManager nam;
    QEventLoop loop;
    ApiResponse api;
    QString transportError;

    QNetworkReply *reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        const QByteArray resp = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && resp.isEmpty())
            transportError = reply->errorString();
        else
            api = parseApiResponse(resp);
        loop.quit();
    });
    loop.exec();
    reply->deleteLater();

    if (!transportError.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("无法连接后端：%1").arg(transportError);
        return false;
    }
    if (!api.ok) {
        if (errorOut)
            *errorOut = authErrorMessage(api);
        return false;
    }

    m_session.token = api.data.value(QStringLiteral("token")).toString();
    m_session.username = api.data.value(QStringLiteral("username")).toString();
    m_session.role = api.data.value(QStringLiteral("role")).toString();
    if (!m_session.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("登录响应无效");
        return false;
    }
    return true;
}

void LoginDialog::onLoginClicked() {
    m_errorLabel->hide();
    const QString username = m_userEdit->text().trimmed();
    const QString password = m_passEdit->text();
    if (username.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入用户名"));
        m_errorLabel->show();
        m_userEdit->setFocus();
        return;
    }
    if (password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入密码"));
        m_errorLabel->show();
        m_passEdit->setFocus();
        return;
    }

    setBusy(true);
    QString err;
    const bool ok = performLogin(username, password, &err);
    setBusy(false);

    if (!ok) {
        m_errorLabel->setText(err);
        m_errorLabel->show();
        m_passEdit->selectAll();
        m_passEdit->setFocus();
        return;
    }

    persistCredentials(username, password);
    accept();
}
