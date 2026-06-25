#include "credential_store.h"
#include <QByteArray>
#include <QSettings>
#include <QSysInfo>
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
constexpr char kOrg[] = "PGM";
constexpr char kApp[] = "GantryHMI";

QSettings settings() {
    return QSettings(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
}

QByteArray machineKey() {
    const QByteArray seed = QSysInfo::machineUniqueId();
    return QCryptographicHash::hash(seed.isEmpty() ? QByteArray("pgm-fallback-key") : seed,
                                    QCryptographicHash::Sha256);
}

QByteArray xorCrypt(const QByteArray &plain, const QByteArray &key) {
    if (plain.isEmpty() || key.isEmpty())
        return plain;
    QByteArray out(plain.size(), '\0');
    for (int i = 0; i < plain.size(); ++i)
        out[i] = plain.at(i) ^ key.at(i % key.size());
    return out;
}

#ifdef Q_OS_WIN
bool dpapiProtect(const QByteArray &plain, QByteArray *out, QString *err) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB enc{};
    if (!CryptProtectData(&in, L"PGM Gantry HMI", nullptr, nullptr, nullptr, 0, &enc)) {
        if (err)
            *err = QStringLiteral("Windows 凭据保护失败 (%1)").arg(GetLastError());
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char *>(enc.pbData),
                      static_cast<int>(enc.cbData));
    LocalFree(enc.pbData);
    return true;
}

bool dpapiUnprotect(const QByteArray &cipher, QByteArray *out, QString *err) {
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(cipher.data()));
    in.cbData = static_cast<DWORD>(cipher.size());
    DATA_BLOB plain{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &plain)) {
        if (err)
            *err = QStringLiteral("Windows 凭据解密失败 (%1)").arg(GetLastError());
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char *>(plain.pbData),
                      static_cast<int>(plain.cbData));
    LocalFree(plain.pbData);
    return true;
}
#endif
} // namespace

QString CredentialStore::loadUsername() {
    return settings().value(QStringLiteral("auth/username")).toString();
}

void CredentialStore::saveUsername(const QString &username) {
    settings().setValue(QStringLiteral("auth/username"), username.trimmed());
}

bool CredentialStore::loadRememberPassword() {
    return settings().value(QStringLiteral("auth/remember_password"), false).toBool();
}

void CredentialStore::saveRememberPassword(bool remember) {
    settings().setValue(QStringLiteral("auth/remember_password"), remember);
    if (!remember)
        clearPassword();
}

bool CredentialStore::loadAutoLogin() {
    return settings().value(QStringLiteral("auth/auto_login"), false).toBool();
}

void CredentialStore::saveAutoLogin(bool autoLogin) {
    settings().setValue(QStringLiteral("auth/auto_login"), autoLogin);
}

bool CredentialStore::hasStoredPassword() {
    return settings().contains(QStringLiteral("auth/password_blob"));
}

bool CredentialStore::savePassword(const QString &password) {
    const QByteArray plain = password.toUtf8();
    QByteArray blob;
#ifdef Q_OS_WIN
    if (!dpapiProtect(plain, &blob, nullptr))
        return false;
    settings().setValue(QStringLiteral("auth/password_backend"), QStringLiteral("dpapi"));
#else
    blob = xorCrypt(plain, machineKey());
    settings().setValue(QStringLiteral("auth/password_backend"), QStringLiteral("xor"));
#endif
    settings().setValue(QStringLiteral("auth/password_blob"),
                       QString::fromLatin1(blob.toBase64()));
    return true;
}

QString CredentialStore::loadPassword() {
    const QByteArray blob = QByteArray::fromBase64(
        settings().value(QStringLiteral("auth/password_blob")).toByteArray());
    if (blob.isEmpty())
        return {};

    const QString backend = settings().value(QStringLiteral("auth/password_backend")).toString();
    QByteArray plain;
#ifdef Q_OS_WIN
    if (backend == QStringLiteral("dpapi")) {
        if (!dpapiUnprotect(blob, &plain, nullptr))
            return {};
    } else {
        plain = xorCrypt(blob, machineKey());
    }
#else
    Q_UNUSED(backend);
    plain = xorCrypt(blob, machineKey());
#endif
    return QString::fromUtf8(plain);
}

void CredentialStore::clearPassword() {
    QSettings s = settings();
    s.remove(QStringLiteral("auth/password_blob"));
    s.remove(QStringLiteral("auth/password_backend"));
}

QString CredentialStore::loadBackendHost() {
    return settings().value(QStringLiteral("host"), QStringLiteral("127.0.0.1")).toString();
}

void CredentialStore::saveBackendHost(const QString &host) {
    settings().setValue(QStringLiteral("host"), host.trimmed());
}

quint16 CredentialStore::loadBackendPort() {
    bool ok = false;
    const int port = settings().value(QStringLiteral("port"), 8080).toInt(&ok);
    return (ok && port > 0 && port <= 65535) ? static_cast<quint16>(port) : 8080;
}

void CredentialStore::saveBackendPort(quint16 port) {
    settings().setValue(QStringLiteral("port"), port);
}
