#include "gantry_client.h"
#include <QModbusReply>
#include <QTcpSocket>
#include <cmath>
#include <cstring>

// ============================================================================
// 构造 / 析构
// ============================================================================

GantryClient::GantryClient(QObject *parent)
    : QObject(parent) {}

GantryClient::~GantryClient() {
    disconnect();
}

// ============================================================================
// 连接管理
// ============================================================================

void GantryClient::connectToPlc(const QString &host, quint16 port) {
    disconnect();
    m_mode = ConnMode::Modbus;
    m_host = host;
    m_port = port;

    m_modbusClient = new QModbusTcpClient(this);
    m_modbusClient->setConnectionParameter(
        QModbusDevice::NetworkAddressParameter, host);
    m_modbusClient->setConnectionParameter(
        QModbusDevice::NetworkPortParameter, port);
    m_modbusClient->setTimeout(3000);
    m_modbusClient->setNumberOfRetries(1);

    connect(m_modbusClient, &QModbusClient::stateChanged, this,
            [this](QModbusDevice::State state) {
        if (state == QModbusDevice::ConnectedState) {
            emitLog(QString("Modbus 已连接 %1:%2").arg(m_host).arg(m_port));
            emit connected();
        } else if (state == QModbusDevice::UnconnectedState) {
            emitLog("Modbus 已断开");
            emit disconnected();
        }
    });
    connect(m_modbusClient, &QModbusClient::errorOccurred, this,
            [this](QModbusDevice::Error) {
        QString msg = m_modbusClient->errorString();
        emitLog("Modbus 错误: " + msg);
        emit communicationError(msg);
    });

    emitLog(QString("正在连接 Modbus %1:%2...").arg(host).arg(port));
    m_modbusClient->connectDevice();
}

void GantryClient::connectToTcsService(const QString &host, quint16 port) {
    disconnect();
    m_mode = ConnMode::Tcs;
    m_host = host;
    m_port = port;

    m_tcsSocket = new QTcpSocket(this);
    m_tcsReadBuffer.clear();

    connect(m_tcsSocket, &QTcpSocket::connected, this, [this]() {
        emitLog(QString("TCS 服务已连接 %1:%2").arg(m_host).arg(m_port));
        emit connected();
    });
    connect(m_tcsSocket, &QTcpSocket::disconnected, this, [this]() {
        emitLog("TCS 已断开");
        emit disconnected();
    });
    connect(m_tcsSocket, &QTcpSocket::readyRead,
            this, &GantryClient::onTcsReadyRead);
    connect(m_tcsSocket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        emit communicationError(m_tcsSocket->errorString());
        emitLog("TCS 错误: " + m_tcsSocket->errorString());
    });

    emitLog(QString("正在连接 TCS %1:%2...").arg(host).arg(port));
    m_tcsSocket->connectToHost(host, port);
}

void GantryClient::disconnect() {
    if (m_mode == ConnMode::Modbus && m_modbusClient) {
        m_modbusClient->disconnectDevice();
        m_modbusClient->deleteLater();
        m_modbusClient = nullptr;
    }
    if (m_mode == ConnMode::Tcs && m_tcsSocket) {
        m_tcsSocket->disconnectFromHost();
        m_tcsSocket->deleteLater();
        m_tcsSocket = nullptr;
    }
    m_mode = ConnMode::None;
    m_discreteValid = false;
    m_inputRegsValid = false;
    m_holdingRegsValid = false;
}

bool GantryClient::isConnected() const {
    if (m_mode == ConnMode::Modbus && m_modbusClient)
        return m_modbusClient->state() == QModbusDevice::ConnectedState;
    if (m_mode == ConnMode::Tcs && m_tcsSocket)
        return m_tcsSocket->state() == QAbstractSocket::ConnectedState;
    return false;
}

// ============================================================================
// IEEE754 浮点编解码 (BIG_BIG: 高字在前, 每字大端)
// ============================================================================

double GantryClient::decodeFloat(const uint16_t regs[2]) {
    uint8_t bytes[4];
    bytes[0] = (regs[0] >> 8) & 0xFF;
    bytes[1] =  regs[0]       & 0xFF;
    bytes[2] = (regs[1] >> 8) & 0xFF;
    bytes[3] =  regs[1]       & 0xFF;
    float val;
    std::memcpy(&val, bytes, sizeof(float));
    return std::round(val * 10000.0) / 10000.0;
}

void GantryClient::encodeFloat(double value, uint16_t out[2]) {
    float fval = static_cast<float>(value);
    uint8_t bytes[4];
    std::memcpy(bytes, &fval, sizeof(float));
    out[0] = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
    out[1] = (static_cast<uint16_t>(bytes[2]) << 8) | bytes[3];
}

// ============================================================================
// 辅助: 从缓存中抽取 float
// ============================================================================

static std::optional<double> extractFloat(const std::vector<uint16_t> &regs,
                                          uint16_t baseAddr,
                                          uint16_t blockBaseAddr) {
    int offset = static_cast<int>(baseAddr) - static_cast<int>(blockBaseAddr);
    if (offset < 0 || offset + 1 >= static_cast<int>(regs.size()))
        return std::nullopt;
    return GantryClient::decodeFloat(&regs[offset]);
}

// ============================================================================
// 拼装 GantryStatus (三路读全返回后调用)
// ============================================================================

void GantryClient::buildStatusFromPartial() {
    if (m_mode != ConnMode::Modbus) return;

    GantryStatus s;

    // 离散输入
    if (m_discreteValid && m_discreteBits.size() >= ModbusAddr::DI_TOTAL_COUNT) {
        s.rawDiscreteBits = m_discreteBits;
        auto b = [&](uint16_t i) -> bool {
            return i < m_discreteBits.size() ? m_discreteBits[i] : false;
        };
        s.autoModeActive        = b(ModbusAddr::DI_AUTO_ACTIVE);
        s.manualModeActive      = b(ModbusAddr::DI_MANUAL_ACTIVE);
        s.speedModeRunning      = b(ModbusAddr::DI_SPEED_MODE_RUNNING);
        s.homingRunning         = b(ModbusAddr::DI_HOMING_RUNNING);
        s.positionModeRunning   = b(ModbusAddr::DI_POSITION_RUNNING);
        s.homingDone            = b(ModbusAddr::DI_HOMING_DONE);
        s.motorRunning          = b(ModbusAddr::DI_MOTOR_RUNNING);
        s.zeroSwitch            = b(ModbusAddr::DI_ZERO_SWITCH);
        s.plcEstop              = b(ModbusAddr::DI_ESTOP_PLC);
        s.estop1                = b(ModbusAddr::DI_ESTOP1);
        s.estop2                = b(ModbusAddr::DI_ESTOP2);
        s.estop3                = b(ModbusAddr::DI_ESTOP3);
        s.safetyRelayNotReady   = b(ModbusAddr::DI_SAFETY_RELAY_NOT_OK);
        s.angleOutOfRange       = b(ModbusAddr::DI_ANGLE_OUT_RANGE);
        s.targetAngleOutOfRange = b(ModbusAddr::DI_TARGET_ANGLE_OUT);
        s.air1PressureOk        = b(ModbusAddr::DI_AIR1_PRESSURE_OK);
        s.air2PressureOk        = b(ModbusAddr::DI_AIR2_PRESSURE_OK);
        s.air1Low               = b(ModbusAddr::DI_AIR1_LOW);
        s.air2Low               = b(ModbusAddr::DI_AIR2_LOW);
        s.limitPos185Ok         = b(ModbusAddr::DI_LIMIT_POS_185_OK);
        s.limitNeg185Ok         = b(ModbusAddr::DI_LIMIT_NEG_185_OK);
        s.atPosLimit            = b(ModbusAddr::DI_AT_POS_LIMIT);
        s.atNegLimit            = b(ModbusAddr::DI_AT_NEG_LIMIT);
        s.brakesOpen.clear();
        for (int i = ModbusAddr::DI_BRAKE1_OPEN; i <= ModbusAddr::DI_BRAKE6_OPEN; ++i)
            s.brakesOpen.push_back(b(i));
    }

    // 输入寄存器
    if (m_inputRegsValid && m_inputRegs.size() >= 20) {
        const uint16_t base = ModbusAddr::IR_ABS01_ANGLE;
        s.abs01AngleDeg     = extractFloat(m_inputRegs, ModbusAddr::IR_ABS01_ANGLE,     base);
        s.abs02AngleDeg     = extractFloat(m_inputRegs, ModbusAddr::IR_ABS02_ANGLE,     base);
        s.servoAngleDeg     = extractFloat(m_inputRegs, ModbusAddr::IR_SERVO_ANGLE,     base);
        s.servoCurrentSpeed = extractFloat(m_inputRegs, ModbusAddr::IR_SERVO_SPEED,     base);
        s.servo1Torque      = extractFloat(m_inputRegs, ModbusAddr::IR_SERVO1_TORQUE,   base);
        s.servo2Torque      = extractFloat(m_inputRegs, ModbusAddr::IR_SERVO2_TORQUE,   base);
        s.axialSlip1        = extractFloat(m_inputRegs, ModbusAddr::IR_AXIAL_SLIP1,     base);
        s.axialSlip2        = extractFloat(m_inputRegs, ModbusAddr::IR_AXIAL_SLIP2,     base);
        s.shearForce        = extractFloat(m_inputRegs, ModbusAddr::IR_SHEAR_FORCE,     base);
        s.estopOvershoot    = extractFloat(m_inputRegs, ModbusAddr::IR_ESTOP_OVERSHOOT, base);
    }

    // 保持寄存器
    if (m_holdingRegsValid && m_holdingRegs.size() >= 2) {
        const uint16_t base = ModbusAddr::HR_SPEED_SETPOINT;
        s.speedSetpoint    = extractFloat(m_holdingRegs, ModbusAddr::HR_SPEED_SETPOINT,    base);
        s.positionSetpoint = extractFloat(m_holdingRegs, ModbusAddr::HR_POSITION_SETPOINT, base);
    }

    emit statusUpdated(s);
}

// ============================================================================
// TCS Socket 行解析
// ============================================================================

void GantryClient::onTcsReadyRead() {
    m_tcsReadBuffer.append(m_tcsSocket->readAll());
    while (true) {
        int idx = m_tcsReadBuffer.indexOf('\n');
        if (idx < 0) break;
        QByteArray line = m_tcsReadBuffer.left(idx).trimmed();
        m_tcsReadBuffer.remove(0, idx + 1);
        if (line.isEmpty()) continue;
        QJsonObject obj = parseJsonLine(line);
        if (obj.isEmpty()) continue;
        emit commandResponse(parseTcsResponse(obj));
    }
}

// ============================================================================
// 状态轮询 — 三路并发读
// ============================================================================

void GantryClient::pollStatus() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    if (m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    // 离散输入
    if (auto *reply = m_modbusClient->sendReadRequest(
            QModbusDataUnit(QModbusDataUnit::DiscreteInputs, 0, ModbusAddr::DI_TOTAL_COUNT),
            m_unitId)) {
        connect(reply, &QModbusReply::finished, this, [this, reply]() {
            if (reply->error() == QModbusDevice::NoError) {
                const auto &result = reply->result();
                m_discreteBits.resize(result.valueCount());
                for (int i = 0; i < result.valueCount(); ++i)
                    m_discreteBits[i] = (result.value(i) != 0);
                m_discreteValid = true;
            }
            reply->deleteLater();
            buildStatusFromPartial();
        });
    }

    // 输入寄存器 (IR 8192 起 22 字 = 11 个 REAL)
    if (auto *reply = m_modbusClient->sendReadRequest(
            QModbusDataUnit(QModbusDataUnit::InputRegisters,
                            ModbusAddr::IR_ABS01_ANGLE, 22),
            m_unitId)) {
        connect(reply, &QModbusReply::finished, this, [this, reply]() {
            if (reply->error() == QModbusDevice::NoError) {
                const auto &result = reply->result();
                m_inputRegs.resize(result.valueCount());
                for (int i = 0; i < result.valueCount(); ++i)
                    m_inputRegs[i] = result.value(i);
                m_inputRegsValid = true;
            }
            reply->deleteLater();
            buildStatusFromPartial();
        });
    }

    // 保持寄存器 (HR 24576 起 10 字)
    if (auto *reply = m_modbusClient->sendReadRequest(
            QModbusDataUnit(QModbusDataUnit::HoldingRegisters,
                            ModbusAddr::HR_SPEED_SETPOINT, 10),
            m_unitId)) {
        connect(reply, &QModbusReply::finished, this, [this, reply]() {
            if (reply->error() == QModbusDevice::NoError) {
                const auto &result = reply->result();
                m_holdingRegs.resize(result.valueCount());
                for (int i = 0; i < result.valueCount(); ++i)
                    m_holdingRegs[i] = result.value(i);
                m_holdingRegsValid = true;
            }
            reply->deleteLater();
            buildStatusFromPartial();
        });
    }
}

// ============================================================================
// Modbus 写辅助
// ============================================================================

static void writeSingleCoil(QModbusTcpClient *c, uint16_t addr, bool val, quint8 unitId) {
    if (!c || c->state() != QModbusDevice::ConnectedState) return;
    auto unit = QModbusDataUnit(QModbusDataUnit::Coils, addr, 1);
    unit.setValue(0, val ? 1 : 0);
    auto *reply = c->sendWriteRequest(unit, unitId);
    if (reply)
        QObject::connect(reply, &QModbusReply::finished, reply, &QModbusReply::deleteLater);
}

static void writeFloatHolding(QModbusTcpClient *c, uint16_t addr,
                              double val, quint8 unitId) {
    if (!c || c->state() != QModbusDevice::ConnectedState) return;
    uint16_t regs[2];
    GantryClient::encodeFloat(val, regs);
    auto unit = QModbusDataUnit(QModbusDataUnit::HoldingRegisters, addr, 2);
    unit.setValue(0, regs[0]);
    unit.setValue(1, regs[1]);
    auto *reply = c->sendWriteRequest(unit, unitId);
    if (reply)
        QObject::connect(reply, &QModbusReply::finished, reply, &QModbusReply::deleteLater);
}

// 脉冲线圈: 写 0 → 60ms → 写 1 → 60ms → 写 0
static void pulseCoil(QModbusTcpClient *c, uint16_t addr, quint8 unitId) {
    writeSingleCoil(c, addr, false, unitId);
    QTimer::singleShot(60, c, [c, addr, unitId]() {
        writeSingleCoil(c, addr, true, unitId);
        QTimer::singleShot(60, c, [c, addr, unitId]() {
            writeSingleCoil(c, addr, false, unitId);
        });
    });
}

// ============================================================================
// 控制指令
// ============================================================================

void GantryClient::setAutoMode() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) {
        emitLog("未连接 Modbus，无法切换模式");
        return;
    }
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_MANUAL_MODE, false, m_unitId);
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_HOMING_START, false, m_unitId);
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_POSITION_START, false, m_unitId);
    QTimer::singleShot(50, this, [this]() {
        writeSingleCoil(m_modbusClient, ModbusAddr::COIL_AUTO_MODE, true, m_unitId);
    });
    emitLog("发送: 自动模式");
}

void GantryClient::setManualMode() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) {
        emitLog("未连接 Modbus，无法切换模式");
        return;
    }
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_AUTO_MODE, false, m_unitId);
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_HOMING_START, false, m_unitId);
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_POSITION_START, false, m_unitId);
    QTimer::singleShot(50, this, [this]() {
        writeSingleCoil(m_modbusClient, ModbusAddr::COIL_MANUAL_MODE, true, m_unitId);
    });
    emitLog("发送: 手动模式");
}

void GantryClient::startHoming() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    pulseCoil(m_modbusClient, ModbusAddr::COIL_HOMING_START, m_unitId);
    emitLog("发送: 寻零启动 (16387 脉冲)");
}

void GantryClient::startPositionMode() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    pulseCoil(m_modbusClient, ModbusAddr::COIL_POSITION_START, m_unitId);
    emitLog("发送: 位置模式启动 (16388 脉冲)");
}

void GantryClient::resetFault() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    pulseCoil(m_modbusClient, ModbusAddr::COIL_RESET, m_unitId);
    emitLog("发送: 故障复位 (16384 脉冲)");
}

void GantryClient::emergencyStop() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    pulseCoil(m_modbusClient, ModbusAddr::COIL_E_STOP, m_unitId);
    emitLog("⚠ 发送: 紧急停止 (16391 脉冲)");
}

void GantryClient::manualJog(bool forward, double speed, double seconds) {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) {
        emitLog("未连接 Modbus，无法点动");
        return;
    }
    writeFloatHolding(m_modbusClient, ModbusAddr::HR_SPEED_SETPOINT, speed, m_unitId);
    uint16_t coil = forward ? ModbusAddr::COIL_MANUAL_FWD : ModbusAddr::COIL_MANUAL_REV;
    uint16_t other = forward ? ModbusAddr::COIL_MANUAL_REV : ModbusAddr::COIL_MANUAL_FWD;
    writeSingleCoil(m_modbusClient, other, false, m_unitId);
    QTimer::singleShot(30, this, [this, coil, seconds]() {
        writeSingleCoil(m_modbusClient, coil, true, m_unitId);
        int ms = std::max(10, static_cast<int>(seconds * 1000));
        QTimer::singleShot(ms, this, [this, coil]() {
            writeSingleCoil(m_modbusClient, coil, false, m_unitId);
            emitLog("点动完成");
        });
    });
    emitLog(QString("发送: 点动 %1 %2s @速度=%3")
                .arg(forward ? "正转" : "反转").arg(seconds).arg(speed));
}

void GantryClient::moveToPosition(double angleDeg, double speed, double timeoutSec) {
    if (m_mode == ConnMode::Tcs && m_tcsSocket) {
        QJsonObject cmd;
        cmd["cmd"] = "move";
        cmd["angle"] = angleDeg;
        cmd["speed"] = speed;
        cmd["timeout"] = timeoutSec;
        cmd["request_id"] = QString("qt-%1").arg(++m_requestIdCounter);
        m_tcsSocket->write(jsonLine(cmd));
        emitLog(QString("发送 move: angle=%1 speed=%2 (TCS)").arg(angleDeg).arg(speed));
        return;
    }
    if (m_mode == ConnMode::Modbus && m_modbusClient) {
        writeFloatHolding(m_modbusClient, ModbusAddr::HR_POSITION_SETPOINT, angleDeg, m_unitId);
        QTimer::singleShot(60, this, [this, speed]() {
            writeFloatHolding(m_modbusClient, ModbusAddr::HR_SPEED_SETPOINT, speed, m_unitId);
            QTimer::singleShot(60, this, [this]() {
                pulseCoil(m_modbusClient, ModbusAddr::COIL_POSITION_START, m_unitId);
            });
        });
        emitLog(QString("发送定位: angle=%1 speed=%2 (Modbus)").arg(angleDeg).arg(speed));
        return;
    }
    emitLog("未连接，无法定位");
}

void GantryClient::stopManualMotion() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_MANUAL_FWD, false, m_unitId);
    writeSingleCoil(m_modbusClient, ModbusAddr::COIL_MANUAL_REV, false, m_unitId);
    emitLog("发送: 停止手动运动");
}

void GantryClient::closeAllBrakes() {
    if (m_mode != ConnMode::Modbus || !m_modbusClient) return;
    pulseCoil(m_modbusClient, ModbusAddr::COIL_ALL_BRAKES_CLOSE, m_unitId);
    emitLog("发送: 全部制动器关闭 (16399)");
}

void GantryClient::requestSnapshot() {
    if (m_mode == ConnMode::Tcs && m_tcsSocket) {
        QJsonObject cmd;
        cmd["cmd"] = "snapshot";
        cmd["request_id"] = QString("qt-%1").arg(++m_requestIdCounter);
        m_tcsSocket->write(jsonLine(cmd));
    } else if (m_mode == ConnMode::Modbus) {
        pollStatus();
    }
}

void GantryClient::sendPing() {
    if (m_mode == ConnMode::Tcs && m_tcsSocket) {
        QJsonObject cmd;
        cmd["cmd"] = "ping";
        cmd["request_id"] = QString("qt-%1").arg(++m_requestIdCounter);
        m_tcsSocket->write(jsonLine(cmd));
    }
}

void GantryClient::emitLog(const QString &msg) {
    emit logMessage(QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), msg));
}
