#include "gantry_client.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kConnectTimeoutMs = 5000;
constexpr int kPollTimeoutMs = 4000;
constexpr int kMotionTimeoutMs = 600000;
constexpr int kMaxConsecutiveFailures = 3;
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
                       ResponseHandler handler, int timeoutMs) {
    if (!m_nam) return;
    QUrl url(m_baseUrl + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(timeoutMs);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, logTag, handler]() {
        const QByteArray body = reply->readAll();
        ApiResponse api = parseApiResponse(body);
        if (reply->error() != QNetworkReply::NoError && body.isEmpty()) {
            const QString err = QString("%1: %2").arg(logTag, reply->errorString());
            emitLog(err);
            if (m_connected && reply->error() != QNetworkReply::OperationCanceledError) {
                ++m_consecutivePollFailures;
                emit communicationError(err);
                if (m_consecutivePollFailures >= kMaxConsecutiveFailures)
                    markDisconnected(QStringLiteral("连续 %1 次通信失败，判定断线")
                                     .arg(m_consecutivePollFailures));
            }
            reply->deleteLater();
            return;
        }
        m_consecutivePollFailures = 0;
        if (!api.ok && !api.error.isEmpty())
            emitLog(QString("  错误: %1 [%2]").arg(api.error, api.errorCode));
        if (handler)
            handler(api);
        reply->deleteLater();
    });
}

QString GantryClient::apiRelativePath(const QString &fullPath) const {
    QString p = fullPath;
    if (p.startsWith(QStringLiteral("/api/v1/")))
        p = p.mid(8);
    return p;
}

void GantryClient::emitCommandResponse(const ApiResponse &api, const QString &relativePath) {
    emit commandResponse(apiResponseToCommandResponse(api, apiPathToCommandId(relativePath)));
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
    const QString rel = apiRelativePath(path);
    const bool isMotion = rel.startsWith(QStringLiteral("motion/"))
        || rel.startsWith(QStringLiteral("workflow/"));
    req.setTransferTimeout(isMotion ? kMotionTimeoutMs : kConnectTimeoutMs);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    emitLog(QString("POST %1 %2").arg(rel, QString::fromUtf8(payload)));
    QNetworkReply *reply = m_nam->post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, logTag, handler, rel, isMotion]() {
        const QByteArray respBody = reply->readAll();
        ApiResponse api = parseApiResponse(respBody);
        if (reply->error() != QNetworkReply::NoError && respBody.isEmpty()) {
            const QString err = QString("%1: %2").arg(logTag, reply->errorString());
            emitLog(err);
            emit communicationError(err);
            if (isMotion && m_motionInProgress)
                setMotionInProgress(false);
            reply->deleteLater();
            return;
        }
        emitLog(QString("%1 ← ok=%2").arg(logTag).arg(api.ok ? "true" : "false"));
        if (!api.ok && !api.error.isEmpty())
            emitLog(QString("  错误: %1 [%2]").arg(api.error, api.errorCode));
        if (handler)
            handler(api);
        emitCommandResponse(api, rel);
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

void GantryClient::setPlcConnected(bool connected) {
    if (m_plcConnected == connected) return;
    m_plcConnected = connected;
    emit plcConnectionChanged(connected);
}

void GantryClient::markDisconnected(const QString &reason) {
    if (!m_connected) return;
    m_connected = false;
    setPlcConnected(false);
    if (!reason.isEmpty())
        emitLog(reason);
    if (m_motionInProgress)
        setMotionInProgress(false);
    emit disconnected();
}

void GantryClient::finishConnectProbe(const ApiResponse &statusApi) {
    m_connected = true;
    emitLog(QString("HTTP 已连接 %1").arg(m_baseUrl));
    if (!statusApi.ok && statusApi.errorCode == QStringLiteral("NOT_CONNECTED"))
        setPlcConnected(false);
    if (statusApi.ok)
        applyStatusData(statusApi.data);
    else if (!statusApi.error.isEmpty())
        emit communicationError(statusApi.error);
    emit connected();
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
    get(QStringLiteral("/api/v1/health"), QStringLiteral("健康检查"),
        [this](const ApiResponse &healthApi) {
            if (!healthApi.ok) {
                emit communicationError(healthApi.error.isEmpty()
                    ? QStringLiteral("后端不可达") : healthApi.error);
                emitLog("连接失败");
                return;
            }
            setPlcConnected(healthApi.data.value(QStringLiteral("plc_connected")).toBool(false));
            get(QStringLiteral("/api/v1/status"), QStringLiteral("连接探测"),
                [this](const ApiResponse &statusApi) {
                    if (statusApi.timestamp.isEmpty() && statusApi.error.isEmpty() && !statusApi.ok) {
                        emit communicationError(QStringLiteral("状态接口不可达"));
                        emitLog("连接失败");
                        return;
                    }
                    finishConnectProbe(statusApi);
                }, kConnectTimeoutMs);
        }, kConnectTimeoutMs);
}

void GantryClient::disconnect() {
    const bool wasConnected = m_connected;
    m_connected = false;
    m_baseUrl.clear();
    setPlcConnected(false);
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
                    setPlcConnected(false);
                return;
            }
            setPlcConnected(true);
            applyStatusData(api.data);
        });
}

void GantryClient::requestSnapshot() {
    pollStatus();
}

void GantryClient::sendPing() {
    requestSnapshot();
}

void GantryClient::runPointTableVerify() {
    if (!m_connected) return;
    get(QStringLiteral("/api/v1/verify"), QStringLiteral("点表巡检"),
        [this](const ApiResponse &api) {
            emitCommandResponse(api, QStringLiteral("verify"));
        }, kMotionTimeoutMs);
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

void GantryClient::setMotionInProgress(bool busy) {
    if (m_motionInProgress == busy) return;
    m_motionInProgress = busy;
    if (!busy)
        emit motionFinished();
}

void GantryClient::startHoming() {
    setMotionInProgress(true);
    post(QStringLiteral("/api/v1/motion/home"), QJsonObject(),
         QStringLiteral("寻零"), [this](const ApiResponse &) {
             setMotionInProgress(false);
         });
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
    setMotionInProgress(true);
    post(QStringLiteral("/api/v1/motion/jog"), body,
         QStringLiteral("点动"), [this](const ApiResponse &) {
             setMotionInProgress(false);
         });
}

void GantryClient::moveToPosition(double angleDeg, double speed, double timeoutSec,
                                   const MotionPositionOptions &opts) {
    QJsonObject body;
    body[QStringLiteral("angle")] = angleDeg;
    body[QStringLiteral("speed")] = speed;
    body[QStringLiteral("timeout")] = timeoutSec;
    body[QStringLiteral("tol")] = opts.tol;
    body[QStringLiteral("arrival_mode")] = opts.arrivalMode;
    body[QStringLiteral("require_homing")] = opts.requireHoming;
    body[QStringLiteral("auto_mode")] = opts.autoMode;
    body[QStringLiteral("di04_grace")] = opts.di04Grace;
    body[QStringLiteral("plateau_n")] = opts.plateauN;
    setMotionInProgress(true);
    post(QStringLiteral("/api/v1/motion/position"), body,
         QStringLiteral("定角运动"), [this](const ApiResponse &api) {
             setMotionInProgress(false);
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

void GantryClient::recoverEstop(int channel) {
    if (channel == 2) {
        post(QStringLiteral("/api/v1/safety/estop2-recover"), QJsonObject(),
             QStringLiteral("急停恢复"), nullptr);
    } else {
        emitLog(QStringLiteral("急停%1恢复: 先发故障复位脉冲").arg(channel));
        post(QStringLiteral("/api/v1/safety/reset"), QJsonObject(),
             QStringLiteral("故障复位(急停恢复)"), nullptr);
    }
}

void GantryClient::runSelfTest() {
    setMotionInProgress(true);
    post(QStringLiteral("/api/v1/workflow/self-test"), QJsonObject(),
         QStringLiteral("上电自检"), [this](const ApiResponse &) {
             setMotionInProgress(false);
         });
}

void GantryClient::runWorkflowFull(const QList<double> &angles,
                                     const WorkflowFullOptions &opts) {
    QJsonObject body;
    QJsonArray arr;
    for (double a : angles) arr.append(a);
    body[QStringLiteral("angles")] = arr;
    body[QStringLiteral("speed")] = opts.speed;
    body[QStringLiteral("tol")] = opts.tol;
    body[QStringLiteral("timeout")] = opts.timeout;
    body[QStringLiteral("skip_self_test")] = opts.skipSelfTest;
    body[QStringLiteral("reset_on_fail")] = opts.resetOnFail;
    body[QStringLiteral("arrival_mode")] = opts.arrivalMode;
    body[QStringLiteral("di04_grace")] = opts.di04Grace;
    body[QStringLiteral("plateau_n")] = opts.plateauN;
    setMotionInProgress(true);
    post(QStringLiteral("/api/v1/workflow/full"), body,
         QStringLiteral("完整工作流"), [this](const ApiResponse &) {
             setMotionInProgress(false);
         });
}
