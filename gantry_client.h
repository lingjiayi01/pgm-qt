#pragma once
#include "gantry_data.h"
#include <QtNetwork>
#include <functional>

// ============================================================================
// GantryClient — HTTP REST 客户端（连 PGM gantry_api.py）
// ============================================================================

class GantryClient : public QObject {
    Q_OBJECT
public:
    explicit GantryClient(QObject *parent = nullptr);
    ~GantryClient() override;

    void connectToBackend(const QString &host = "127.0.0.1", quint16 port = 8080);
    void disconnect();
    bool isConnected() const { return m_connected; }
    bool isPlcConnected() const { return m_plcConnected; }
    QString currentHost() const { return m_host; }
    quint16 currentPort() const { return m_port; }

    // 控制指令
    void setAutoMode();
    void setManualMode();
    void startHoming();
    void startPositionMode();
    void resetFault();
    void emergencyStop();
    void manualJog(bool forward, double speed, double seconds);
    void moveToPosition(double angleDeg, double speed, double timeoutSec = 300.0,
                        const MotionPositionOptions &opts = {});
    void stopManualMotion();
    void closeAllBrakes();
    void openAllBrakes();
    void recoverEstop(int channel = 2);
    void runSelfTest();
    void runWorkflowFull(const QList<double> &angles,
                         const WorkflowFullOptions &opts = {});

    // 查询
    void requestSnapshot();
    void sendPing();
    void pollStatus();
    void runPointTableVerify();
    bool isMotionInProgress() const { return m_motionInProgress; }

signals:
    void connected();
    void disconnected();
    void plcConnectionChanged(bool connected);
    void statusUpdated(const GantryStatus &status);
    void commandResponse(const TcsResponse &resp);
    void communicationError(const QString &error);
    void logMessage(const QString &msg);
    void motionFinished();

private:
    using ResponseHandler = std::function<void(const ApiResponse &)>;

    void get(const QString &path, const QString &logTag, ResponseHandler handler,
             int timeoutMs = 4000);
    void post(const QString &path, const QJsonObject &body,
              const QString &logTag, ResponseHandler handler);
    void applyStatusData(const QJsonObject &data);
    void emitLog(const QString &msg);
    void markDisconnected(const QString &reason = QString());
    void setPlcConnected(bool connected);
    void finishConnectProbe(const ApiResponse &statusApi);
    QString apiRelativePath(const QString &fullPath) const;
    void emitCommandResponse(const ApiResponse &api, const QString &relativePath);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_host;
    quint16 m_port = 8080;
    bool m_connected = false;
    bool m_plcConnected = false;
    int m_consecutivePollFailures = 0;
    bool m_motionInProgress = false;
    void setMotionInProgress(bool busy);
};
