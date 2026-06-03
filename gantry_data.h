#pragma once
#include <QtCore>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>

// ============================================================================
// Modbus 寄存器地址常量 — 与 gantry_registers.py 严格一致
// ============================================================================

namespace ModbusAddr {
    // --- 线圈 COIL (0x05写/0x01读) ---
    constexpr uint16_t COIL_RESET            = 16384;
    constexpr uint16_t COIL_AUTO_MODE        = 16385;
    constexpr uint16_t COIL_MANUAL_MODE      = 16386;
    constexpr uint16_t COIL_HOMING_START     = 16387;
    constexpr uint16_t COIL_POSITION_START   = 16388;
    constexpr uint16_t COIL_MANUAL_FWD       = 16389;
    constexpr uint16_t COIL_MANUAL_REV       = 16390;
    constexpr uint16_t COIL_E_STOP           = 16391;
    constexpr uint16_t COIL_BRAKE1_OPEN      = 16392;
    constexpr uint16_t COIL_BRAKE2_OPEN      = 16393;
    constexpr uint16_t COIL_BRAKE3_OPEN      = 16394;
    constexpr uint16_t COIL_BRAKE4_OPEN      = 16395;
    constexpr uint16_t COIL_BRAKE5_OPEN      = 16396;
    constexpr uint16_t COIL_BRAKE6_OPEN      = 16397;
    constexpr uint16_t COIL_ALL_BRAKES_OPEN  = 16398;
    constexpr uint16_t COIL_ALL_BRAKES_CLOSE = 16399;

    // --- 离散输入 DI (0x02读) ---
    constexpr uint16_t DI_AUTO_ACTIVE          = 0;
    constexpr uint16_t DI_MANUAL_ACTIVE        = 1;
    constexpr uint16_t DI_SPEED_MODE_RUNNING   = 2;
    constexpr uint16_t DI_HOMING_RUNNING       = 3;
    constexpr uint16_t DI_POSITION_RUNNING     = 4;
    constexpr uint16_t DI_HOMING_DONE          = 5;
    constexpr uint16_t DI_MOTOR_RUNNING        = 6;
    constexpr uint16_t DI_BRAKE_ANY_OPEN       = 10;
    constexpr uint16_t DI_BRAKE1_OPEN          = 11;
    constexpr uint16_t DI_BRAKE2_OPEN          = 12;
    constexpr uint16_t DI_BRAKE3_OPEN          = 13;
    constexpr uint16_t DI_BRAKE4_OPEN          = 14;
    constexpr uint16_t DI_BRAKE5_OPEN          = 15;
    constexpr uint16_t DI_BRAKE6_OPEN          = 16;
    constexpr uint16_t DI_AIR1_PRESSURE_OK     = 17;
    constexpr uint16_t DI_AIR2_PRESSURE_OK     = 18;
    constexpr uint16_t DI_ZERO_SWITCH          = 19;
    constexpr uint16_t DI_LIMIT_POS_185_OK     = 20;
    constexpr uint16_t DI_LIMIT_NEG_185_OK     = 21;
    constexpr uint16_t DI_ESTOP_PLC            = 33;
    constexpr uint16_t DI_ESTOP1               = 34;
    constexpr uint16_t DI_ESTOP2               = 35;
    constexpr uint16_t DI_ESTOP3               = 36;
    constexpr uint16_t DI_SAFETY_RELAY_NOT_OK  = 37;
    constexpr uint16_t DI_AT_POS_LIMIT         = 38;
    constexpr uint16_t DI_AT_NEG_LIMIT         = 39;
    constexpr uint16_t DI_AIR1_LOW             = 40;
    constexpr uint16_t DI_AIR2_LOW             = 41;
    constexpr uint16_t DI_ANGLE_OUT_RANGE      = 42;
    constexpr uint16_t DI_TARGET_ANGLE_OUT     = 43;

    constexpr uint16_t DI_TOTAL_COUNT          = 70;

    // --- 输入寄存器 IR (0x04读) REAL=2字 ---
    constexpr uint16_t IR_ABS01_ANGLE        = 8192;
    constexpr uint16_t IR_ABS02_ANGLE        = 8194;
    constexpr uint16_t IR_SERVO_ANGLE        = 8196;
    constexpr uint16_t IR_SERVO_SPEED        = 8198;
    constexpr uint16_t IR_SERVO1_TORQUE      = 8200;
    constexpr uint16_t IR_SERVO2_TORQUE      = 8202;
    constexpr uint16_t IR_AXIAL_SLIP1        = 8204;
    constexpr uint16_t IR_AXIAL_SLIP2        = 8206;
    constexpr uint16_t IR_SHEAR_FORCE        = 8208;
    constexpr uint16_t IR_ESTOP_OVERSHOOT    = 8210;

    // --- 保持寄存器 HR (0x10写/0x03读) ---
    constexpr uint16_t HR_SPEED_SETPOINT     = 24576;
    constexpr uint16_t HR_POSITION_SETPOINT  = 24578;

    constexpr double ANGLE_WORK_LIMIT_DEG    = 185.0;
}

// ============================================================================
// 数据帧：GantryStatus — 完整的状态快照
// ============================================================================

struct GantryStatus {
    // --- 模式与流程 ---
    bool autoModeActive        = false;    // 00000
    bool manualModeActive      = false;    // 00001
    bool speedModeRunning      = false;    // 00002
    bool homingRunning         = false;    // 00003
    bool positionModeRunning   = false;    // 00004
    bool homingDone            = false;    // 00005
    bool motorRunning          = false;    // 00006
    bool zeroSwitch            = false;    // 00019

    // --- 安全与联锁 ---
    bool plcEstop              = false;    // 00033
    bool estop1                = false;    // 00034
    bool estop2                = false;    // 00035
    bool estop3                = false;    // 00036
    bool safetyRelayNotReady   = false;    // 00037
    bool angleOutOfRange       = false;    // 00042
    bool targetAngleOutOfRange = false;    // 00043
    bool air1PressureOk        = false;    // 00017
    bool air2PressureOk        = false;    // 00018
    bool air1Low               = false;    // 00040
    bool air2Low               = false;    // 00041
    bool limitPos185Ok         = false;    // 00020
    bool limitNeg185Ok         = false;    // 00021
    bool atPosLimit            = false;    // 00038
    bool atNegLimit            = false;    // 00039
    std::vector<bool> brakesOpen;          // 00011~00016 (6位)

    // --- 角度与参数 (IR / HR) ---
    std::optional<double> servoAngleDeg;
    std::optional<double> abs01AngleDeg;
    std::optional<double> abs02AngleDeg;
    std::optional<double> servoCurrentSpeed;
    std::optional<double> servo1Torque;
    std::optional<double> servo2Torque;
    std::optional<double> axialSlip1;
    std::optional<double> axialSlip2;
    std::optional<double> shearForce;
    std::optional<double> estopOvershoot;
    std::optional<double> positionSetpoint;
    std::optional<double> speedSetpoint;

    // --- 原始离散位全集 (DI 0~69) ---
    std::vector<bool> rawDiscreteBits;

    // --- 聚合判据 ---
    bool motionInhibit() const {
        bool estopAny = plcEstop || estop1 || estop2 || estop3;
        bool airInletFault = !(air1PressureOk && air2PressureOk);
        bool servoFault = false;
        if (rawDiscreteBits.size() > 65) {
            for (int i = 61; i <= 65; ++i)
                if (i < (int)rawDiscreteBits.size() && rawDiscreteBits[i]) {
                    servoFault = true;
                    break;
                }
        }
        return estopAny
            || safetyRelayNotReady
            || !limitPos185Ok || !limitNeg185Ok
            || atPosLimit || atNegLimit
            || angleOutOfRange || targetAngleOutOfRange
            || airInletFault || air1Low || air2Low
            || servoFault;
    }

    // 可出束判据 (与 evaluate_beam_permit_placeholder 一致)
    std::pair<bool, QString> beamPermit(double tol = 0.5) const {
        if (motionInhibit())
            return {false, QStringLiteral("motion_inhibit=1")};
        if (!autoModeActive)
            return {false, QStringLiteral("非自动模式(00000!=1)")};
        if (positionModeRunning)
            return {false, QStringLiteral("位置模式运行中(00004=1)")};
        for (auto b : brakesOpen)
            if (b)
                return {false, QStringLiteral("制动器打开检测存在1")};
        double cur = abs01AngleDeg.value_or(servoAngleDeg.value_or(0.0));
        if (positionSetpoint.has_value()) {
            if (std::abs(cur - *positionSetpoint) > tol)
                return {false,
                    QString("角度未到位: 当前%1 vs 给定%2")
                        .arg(cur, 0, 'f', 3)
                        .arg(*positionSetpoint, 0, 'f', 3)};
        }
        return {true, QStringLiteral("许可=1(接TCS复核)")};
    }

    QString estopLabels() const {
        QStringList labels;
        if (plcEstop) labels << "PLC";
        if (estop1) labels << "E1";
        if (estop2) labels << "E2";
        if (estop3) labels << "E3";
        return labels.isEmpty() ? QString() : labels.join(',');
    }
};

// ============================================================================
// TCS 响应解析
// ============================================================================

struct TcsResponse {
    bool ok = false;
    QString cmd;
    QString requestId;
    QString error;
    bool pong = false;
    bool motionComplete = false;
    QString motionDetail;
    double targetDeg = 0.0;
    double speed = 0.0;
    bool beamPermit = false;
    QString beamPermitReason;
};

// ============================================================================
// JSON 行编解码工具
// ============================================================================

inline QByteArray jsonLine(const QJsonObject &obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
}

inline QJsonObject parseJsonLine(const QByteArray &raw) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    return (err.error == QJsonParseError::NoError) ? doc.object() : QJsonObject();
}

inline TcsResponse parseTcsResponse(const QJsonObject &obj) {
    TcsResponse r;
    r.ok = obj.value("ok").toBool(false);
    r.cmd = obj.value("cmd").toString();
    r.requestId = obj.value("request_id").toString();
    r.error = obj.value("error").toString();
    r.pong = obj.value("pong").toBool(false);
    r.motionComplete = obj.value("motion_complete").toBool(false);
    r.motionDetail = obj.value("motion_detail").toString();
    r.targetDeg = obj.value("target_deg").toDouble(0.0);
    r.speed = obj.value("speed").toDouble(0.0);
    r.beamPermit = obj.value("beam_permit_placeholder").toBool(false);
    r.beamPermitReason = obj.value("beam_permit_reason").toString();
    return r;
}
