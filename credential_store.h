#pragma once
#include <QString>
#include <QtGlobal>

// 登录凭据本地存储：用户名明文；密码仅「记住密码」时用 OS 保护或机器密钥加密。
class CredentialStore {
public:
    static QString loadUsername();
    static void saveUsername(const QString &username);

    static bool loadRememberPassword();
    static void saveRememberPassword(bool remember);

    static bool loadAutoLogin();
    static void saveAutoLogin(bool autoLogin);

    static bool hasStoredPassword();
    static bool savePassword(const QString &password);
    static QString loadPassword();
    static void clearPassword();

    static QString loadBackendHost();
    static void saveBackendHost(const QString &host);
    static quint16 loadBackendPort();
    static void saveBackendPort(quint16 port);
};
