#include "gantry_client.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kConnectTimeoutMs = 5000;
constexpr int kPollTimeoutMs = 4000;
constexpr int kMotionTimeoutMs = 600000;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

GantryClient::GantryClient(QObject *parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

GantryClient::~GantryClient() {
    disconnect();
}

// ============================================================================
// HTTP 辅助
// ============================================================================

void GantryClient::get(const QString &path, const QString &logTag,
                       ResponseHandler handler) {
    if (!m_nam) return;
    QUrl url(m_baseUrl + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(kPollTimeoutMs);

    emitLog(QString("GET %1").arg(path));
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, logTag, handler]() {
        const QByteArray body = reply->readAll();
        ApiResponse api = parseApiResponse(body);
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            const QString err = QString("%1: %2").arg(logTag, reply->errorString());
            emitLog(err);
            if (m_connected && reply->error() != QNetworkReply::OperationCanceledError)
                emit communicationError(err);
            reply->deleteLater();
            return;
        }
        emitLog(QString("%1 ← ok=%2").arg(logTag).arg(api.ok ? "true" : "false"));
        if (!api.ok && !api.error.isEmpty())
            emitLog(QString("  错误: %1 [%2]").arg(api.error, api.errorCode));
        if (handler)
            handler(api);
        reply->deleteLater();
    });
}

void GantryClient::post(const QString &path, const QJsonObject &body,
                        const QString &logTag, ResponseHandler handler) {
    if (!m_nam) return;
    if (!m_connected) {
        emitLog("未连接后端，无法发送: " + logTag);
        return;
    }
    QUrl url(m_baseUrl + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const bool isMotion = path.contains(QStringLiteral("/motion/"))
        || path.contains(QStringLiteral("/workflow/"));
    req.setTransferTimeout(isMotion ? kMotionTimeoutMs : kConnectTimeoutMs);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    emitLog(QString("POST %1 %2").arg(path, QString::fromUtf8(payload)));
    QNetworkReply *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, logTag, handler, path]() {
        const QByteArray respBody = reply->readAll();
        ApiResponse api = parseApiResponse(respBody);
        if (reply->error() != QNetworkReply::NoError && respBody.isEmpty()) {
            const QString err = QString("%1: %2").arg(logTag, reply->errorString());
            emitLog(err);
            emit communicationError(err);
            reply->deleteLater();
            return;
        }
        emitLog(QString("%1 ← ok=%2").arg(logTag).arg(api.ok ? "true" : "false"));
        if (!api.ok && !api.error.isEmpty())
            emitLog(QString("  错误: %1 [%2]").arg(api.error, api.errorCode));
        if (handler)
            handler(api);
        QString cmd = path.section('/', -1);
        if (path.contains(QStringLiteral("/mode/")))
            cmd = path.section('/', -2).section('/', -1) + QStringLiteral("_mode");
        emit commandResponse(apiResponseToCommandResponse(api, cmd));
        reply->deleteLater();
    });
}

void GantryClient::applyStatusData(const QJsonObject &data) {
    if (data.isEmpty()) return;
    GantryStatus s = gantryStatusFromApiData(data);
    emit statusUpdated(s);
}

void GantryClient::emitLog(const QString &msg) {
    emit logMessage(QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), msg));
}

void GantryClient::markDisconnected(const QString &reason) {
    if (!m_connected) return;
    m_connected = false;
    if (!reason.isEmpty())
        emitLog(reason);
    emit disconnected();
}

// ============================================================================
// 连接管理
// ============================================================================

void GantryClient::connectToBackend(const QString &host, quint16 port) {
    disconnect();
    m_host = host.trimmed();
    m_port = port;
    m_baseUrl = QString("http://%1:%2").arg(m_host).arg(m_port);

    emitLog(QString("正在连接后端 %1...").arg(m_baseUrl));
    get(QStringLiteral("/api/v1/status"), QStringLiteral("连接探测"),
        [this](const ApiResponse &api) {
            if (api.timestamp.isEmpty() && api.error.isEmpty() && !api.ok) {
                emit communicationError(QStringLiteral("后端不可达"));
                emitLog("连接失败");
                return;
            }
            m_connected = true;
            emitLog(QString("HTTP 已连接 %1").arg(m_baseUrl));
            if (api.ok)
                applyStatusData(api.data);
            else if (!api.error.isEmpty())
                emit communicationError(api.error);
            emit connected();
        });
}

void GantryClient::disconnect() {
    const bool wasConnected = m_connected;
    m_connected = false;
    m_baseUrl.clear();
    if (wasConnected) {
        emitLog("已断开");
        emit disconnected();
    }
}

// ============================================================================
// 查询
// ============================================================================

void GantryClient::pollStatus() {
    if (!m_connected) return;
    get(QStringLiteral("/api/v1/status"), QStringLiteral("状态轮询"),
        [this](const ApiResponse &api) {
            if (!api.ok) {
                if (api.errorCode == QStringLiteral("NOT_CONNECTED"))
                    markDisconnected(QStringLiteral("后端 PLC 未连接"));
                return;
            }
            applyStatusData(api.data);
        });
}

void GantryClient::requestSnapshot() {
    pollStatus();
}

void GantryClient::sendPing() {
    requestSnapshot();
}

// ============================================================================
// 控制指令
// ============================================================================

void GantryClient::setAutoMode() {
    post(QStringLiteral("/api/v1/mode/auto"), QJsonObject(),
         QStringLiteral("自动模式"), nullptr);
}

void GantryClient::setManualMode() {
    post(QStringLiteral("/api/v1/mode/manual"), QJsonObject(),
         QStringLiteral("手动模式"), nullptr);
}

void GantryClient::startHoming() {
    post(QStringLiteral("/api/v1/motion/home"), QJsonObject(),
         QStringLiteral("寻零"), nullptr);
}

void GantryClient::startPositionMode() {
    emitLog("位置模式由定角运动 API 触发");
}

void GantryClient::resetFault() {
    post(QStringLiteral("/api/v1/safety/reset"), QJsonObject(),
         QStringLiteral("故障复位"), nullptr);
}

void GantryClient::emergencyStop() {
    post(QStringLiteral("/api/v1/motion/estop"), QJsonObject(),
         QStringLiteral("紧急停止"), nullptr);
}

void GantryClient::manualJog(bool forward, double speed, double seconds) {
    QJsonObject body;
    body[QStringLiteral("forward")] = forward;
    body[QStringLiteral("speed")] = speed;
    body[QStringLiteral("seconds")] = seconds;
    post(QStringLiteral("/api/v1/motion/jog"), body,
         QStringLiteral("点动"), nullptr);
}

void GantryClient::moveToPosition(double angleDeg, double speed, double timeoutSec) {
    QJsonObject body;
    body[QStringLiteral("angle")] = angleDeg;
    body[QStringLiteral("speed")] = speed;
    body[QStringLiteral("timeout")] = timeoutSec;
    post(QStringLiteral("/api/v1/motion/position"), body,
         QStringLiteral("定角运动"), [this](const ApiResponse &api) {
             if (api.ok)
                 applyStatusData(api.data);
         });
}

void GantryClient::stopManualMotion() {
    post(QStringLiteral("/api/v1/motion/stop"), QJsonObject(),
         QStringLiteral("停止手动"), nullptr);
}

void GantryClient::closeAllBrakes() {
    post(QStringLiteral("/api/v1/brakes/close"), QJsonObject(),
         QStringLiteral("关闭制动"), nullptr);
}

void GantryClient::openAllBrakes() {
    QJsonObject body;
    body[QStringLiteral("confirm")] = true;
    post(QStringLiteral("/api/v1/brakes/open"), body,
         QStringLiteral("打开制动"), nullptr);
}

void GantryClient::recoverEstop2() {
    post(QStringLiteral("/api/v1/safety/estop2-recover"), QJsonObject(),
         QStringLiteral("急停2恢复"), nullptr);
}
