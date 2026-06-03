#pragma once
#include "gantry_data.h"
#include <QtCore>
#include <QtNetwork>
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <memory>

// ============================================================================
// GantryClient — 双协议通信客户端
//   1. Modbus TCP 直连 PLC (默认 192.168.10.1:510)
//   2. TCS JSON 行协议 (PGM tcs-serve, 默认 5510)
// ============================================================================

class GantryClient : public QObject {
    Q_OBJECT
public:
    explicit GantryClient(QObject *parent = nullptr);
    ~GantryClient() override;

    // 连接管理
    void connectToPlc(const QString &host = "192.168.10.1", quint16 port = 510);
    void connectToTcsService(const QString &host = "127.0.0.1", quint16 port = 5510);
    void disconnect();
    bool isConnected() const;
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
    void moveToPosition(double angleDeg, double speed, double timeoutSec = 300.0);
    void stopManualMotion();
    void closeAllBrakes();

    // 查询
    void requestSnapshot();
    void sendPing();
    void pollStatus();  // 由定时器周期性调用

    // 浮点编解码 (BIG_BIG)
    static double decodeFloat(const uint16_t regs[2]);
    static void encodeFloat(double value, uint16_t out[2]);

signals:
    void connected();
    void disconnected();
    void statusUpdated(const GantryStatus &status);
    void commandResponse(const TcsResponse &resp);
    void communicationError(const QString &error);
    void logMessage(const QString &msg);

private:
    void onTcsReadyRead();
    void emitLog(const QString &msg);
    void buildStatusFromPartial();

    enum class ConnMode { None, Modbus, Tcs };
    ConnMode m_mode = ConnMode::None;

    // Modbus
    QModbusTcpClient *m_modbusClient = nullptr;
    // TCS
    QTcpSocket *m_tcsSocket = nullptr;
    QByteArray m_tcsReadBuffer;

    QString m_host;
    quint16 m_port = 510;
    quint8 m_unitId = 1;
    int m_requestIdCounter = 0;

    // 缓存的部分数据 (等待三路读全部返回后拼装)
    std::vector<bool> m_discreteBits;
    std::vector<uint16_t> m_inputRegs;
    std::vector<uint16_t> m_holdingRegs;
    bool m_discreteValid = false;
    bool m_inputRegsValid = false;
    bool m_holdingRegsValid = false;
};
