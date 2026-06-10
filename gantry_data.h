#pragma once
#include <QtCore>
#include <QtEndian>
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
    constexpr double ANGLE_VALID_MIN_DEG     = -185.0;
    constexpr double ANGLE_VALID_MAX_DEG     = 185.0;
    constexpr double CHART_ANGLE_AXIS_MIN    = -200.0;
    constexpr double CHART_ANGLE_AXIS_MAX    = 200.0;
}

// ============================================================================
// Modbus IEEE754 单精度 — 工业 AB-CD（高低字互换，字内大端）
// 寄存器顺序: regs[0]=低字(CD), regs[1]=高字(AB) → 内存字节序 ABCD → float
// ============================================================================

namespace ModbusFloat {

inline bool isFiniteFloat(double v) {
    return std::isfinite(v) && !std::isnan(v);
}

inline double round4(double v) {
    return std::round(v * 10000.0) / 10000.0;
}

inline double decodeAbCd(const uint16_t regs[2]) {
    // 高低字互换: regs[0]=低字(CD), regs[1]=高字(AB) → 32 位大端 float
    const quint32 be = (quint32(regs[1]) << 16) | quint32(regs[0]);
    const quint32 native = qFromBigEndian(be);
    float val = 0.0f;
    std::memcpy(&val, &native, sizeof(float));
    return round4(static_cast<double>(val));
}

inline void encodeAbCd(double value, uint16_t out[2]) {
    float fval = static_cast<float>(value);
    quint32 native = 0;
    std::memcpy(&native, &fval, sizeof(float));
    const quint32 be = qToBigEndian(native);
    out[0] = static_cast<uint16_t>(be & 0xFFFFu);
    out[1] = static_cast<uint16_t>((be >> 16) & 0xFFFFu);
}

/** ABS01/ABS02：超出 [-185,185] 或非有限数 → 0.0 */
inline double sanitizeAbsAngle(double v) {
    if (!isFiniteFloat(v)
        || v < ModbusAddr::ANGLE_VALID_MIN_DEG
        || v > ModbusAddr::ANGLE_VALID_MAX_DEG) {
        return 0.0;
    }
    return round4(v);
}

inline std::optional<double> sanitizeAbsAngleOptional(const std::optional<double> &v) {
    if (!v.has_value()) return std::nullopt;
    return sanitizeAbsAngle(*v);
}

/** UI/表盘用：非法角度显示为 0 */
inline double angleForDisplay(const std::optional<double> &v) {
    if (!v.has_value()) return 0.0;
    return sanitizeAbsAngle(*v);
}

inline bool isDisplayableAngle(const std::optional<double> &v) {
    if (!v.has_value()) return false;
    const double x = *v;
    return isFiniteFloat(x)
        && x >= ModbusAddr::ANGLE_VALID_MIN_DEG
        && x <= ModbusAddr::ANGLE_VALID_MAX_DEG;
}

inline QString formatAngleDegUi(const std::optional<double> &v) {
    if (!isDisplayableAngle(v)) return QStringLiteral("0.00°");
    return QString::number(*v, 'f', 2) + QStringLiteral("°");
}

inline bool isReasonableScalar(const std::optional<double> &v, double maxAbs = 1e6) {
    if (!v.has_value()) return false;
    const double x = *v;
    return isFiniteFloat(x) && std::abs(x) <= maxAbs;
}

inline QString formatScalarUi(const std::optional<double> &v, int decimals = 2) {
    if (!isReasonableScalar(v)) return QStringLiteral("0.00");
    return QString::number(*v, 'f', decimals);
}

} // namespace ModbusFloat

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

    QString estopLabels() const {
        QStringList labels;
        if (plcEstop) labels << "PLC";
        if (estop1) labels << "E1";
        if (estop2) labels << "E2";
        if (estop3) labels << "E3";
        return labels.isEmpty() ? QString() : labels.join(',');
    }

    // TCS snapshot 下发的出束判据（优先于本地 beamPermit()）
    std::optional<bool> tcsBeamPermit;
    std::optional<QString> tcsBeamPermitReason;
    std::optional<bool> tcsMotionInhibit;

    std::pair<bool, QString> beamPermit(double tol = 0.5) const {
        if (tcsBeamPermit.has_value())
            return {*tcsBeamPermit, tcsBeamPermitReason.value_or(QString())};
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

    bool motionInhibitEffective() const {
        if (tcsMotionInhibit.has_value())
            return *tcsMotionInhibit;
        return motionInhibit();
    }
};

// ============================================================================
// HTTP API 响应解析
// ============================================================================

struct ApiResponse {
    bool ok = false;
    QString error;
    QString errorCode;
    QJsonObject data;
    QString timestamp;
};

inline ApiResponse parseApiResponse(const QByteArray &body) {
    ApiResponse r;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError) {
        r.error = err.errorString();
        return r;
    }
    const QJsonObject obj = doc.object();
    r.ok = obj.value(QStringLiteral("ok")).toBool(false);
    r.error = obj.value(QStringLiteral("error")).toString();
    r.errorCode = obj.value(QStringLiteral("error_code")).toString();
    r.timestamp = obj.value(QStringLiteral("timestamp")).toString();
    r.data = obj.value(QStringLiteral("data")).toObject();
    return r;
}

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
    QJsonObject tcsSnapshot;
};

// 将 tcs-serve / API tcs_snapshot 转为 GantryStatus
inline GantryStatus gantryStatusFromTcsSnapshot(const QJsonObject &snap) {
    GantryStatus s;
    auto b = [&](const char *k) { return snap.value(k).toBool(false); };
    s.autoModeActive        = b("di_00000_auto_mode");
    s.manualModeActive      = b("di_00001_manual_mode");
    s.homingRunning         = b("di_00003_homing_running");
    s.positionModeRunning   = b("di_00004_position_mode_running");
    s.homingDone            = b("di_00005_homing_done");
    s.motorRunning          = b("di_00006_motor_running");
    s.zeroSwitch            = b("di_00019_zero_switch");
    s.plcEstop              = b("di_00033_plc_estop");
    s.estop1                = b("di_00034_estop1");
    s.estop2                = b("di_00035_estop2");
    s.estop3                = b("di_00036_estop3");
    s.safetyRelayNotReady   = b("di_00037_safety_relay_not_ready");
    s.angleOutOfRange       = b("di_00042_angle_out_of_range");
    s.targetAngleOutOfRange = b("di_00043_target_angle_out_of_range");
    s.air1PressureOk        = b("di_00017_air1_pressure_ok");
    s.air2PressureOk        = b("di_00018_air2_pressure_ok");
    s.air1Low               = b("di_00040_air1_low");
    s.air2Low               = b("di_00041_air2_low");
    s.limitPos185Ok         = true;
    s.limitNeg185Ok         = true;

    const auto brakes = snap.value("brakes_open_11_16").toArray();
    s.brakesOpen.clear();
    for (const auto &v : brakes)
        s.brakesOpen.push_back(v.toBool(false));

    auto optD = [&](const char *k) -> std::optional<double> {
        if (!snap.contains(k) || snap.value(k).isNull()) return std::nullopt;
        return snap.value(k).toDouble(0.0);
    };
    s.servoAngleDeg     = optD("ir_8196_servo_angle_deg");
    s.abs01AngleDeg     = ModbusFloat::sanitizeAbsAngleOptional(
        optD("ir_8192_abs01_angle_deg"));
    s.positionSetpoint  = ModbusFloat::sanitizeAbsAngleOptional(
        optD("hr_24578_position_setpoint_deg"));

    if (snap.contains("motion_inhibit"))
        s.tcsMotionInhibit = snap.value("motion_inhibit").toBool(false);
    if (snap.contains("beam_permit_placeholder")) {
        s.tcsBeamPermit = snap.value("beam_permit_placeholder").toBool(false);
        s.tcsBeamPermitReason = snap.value("beam_permit_reason").toString();
    }
    return s;
}

// 将 REST /api/v1/status 的 data 转为 GantryStatus
inline GantryStatus gantryStatusFromApiData(const QJsonObject &data) {
    GantryStatus s;
    const QJsonObject snap = data.value(QStringLiteral("tcs_snapshot")).toObject();
    if (!snap.isEmpty())
        s = gantryStatusFromTcsSnapshot(snap);

    const QJsonArray bitsArr = data.value(QStringLiteral("discrete_bits")).toArray();
    if (!bitsArr.isEmpty()) {
        s.rawDiscreteBits.clear();
        s.rawDiscreteBits.reserve(bitsArr.size());
        for (const auto &v : bitsArr)
            s.rawDiscreteBits.push_back(v.toBool(false));

        auto b = [&](uint16_t i) -> bool {
            return i < s.rawDiscreteBits.size() ? s.rawDiscreteBits[i] : false;
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
            s.brakesOpen.push_back(b(static_cast<uint16_t>(i)));
    }

    const QJsonObject analog = data.value(QStringLiteral("analog")).toObject();
    auto optFrom = [](const QJsonObject &obj, const char *k) -> std::optional<double> {
        if (!obj.contains(k) || obj.value(k).isNull()) return std::nullopt;
        return obj.value(k).toDouble(0.0);
    };
    auto optD = [&](const char *k) { return optFrom(analog, k); };

    if (!analog.isEmpty()) {
        if (analog.contains("ir_8194_abs02_angle_deg"))
            s.abs02AngleDeg = ModbusFloat::sanitizeAbsAngleOptional(
                optD("ir_8194_abs02_angle_deg"));
        if (analog.contains("ir_8198_servo_speed"))
            s.servoCurrentSpeed = optD("ir_8198_servo_speed");
        if (analog.contains("ir_8200_servo1_torque"))
            s.servo1Torque = optD("ir_8200_servo1_torque");
        if (analog.contains("ir_8202_servo2_torque"))
            s.servo2Torque = optD("ir_8202_servo2_torque");
        if (analog.contains("ir_8204_axial_slip1"))
            s.axialSlip1 = optD("ir_8204_axial_slip1");
        if (analog.contains("ir_8206_axial_slip2"))
            s.axialSlip2 = optD("ir_8206_axial_slip2");
        if (analog.contains("ir_8208_shear_force"))
            s.shearForce = optD("ir_8208_shear_force");
        if (analog.contains("ir_8210_estop_overshoot"))
            s.estopOvershoot = optD("ir_8210_estop_overshoot");
        if (analog.contains("hr_24576_speed_setpoint_deg"))
            s.speedSetpoint = optD("hr_24576_speed_setpoint_deg");
    }

    if (data.contains(QStringLiteral("beam_permit_placeholder"))) {
        s.tcsBeamPermit = data.value(QStringLiteral("beam_permit_placeholder")).toBool(false);
        s.tcsBeamPermitReason = data.value(QStringLiteral("beam_permit_reason")).toString();
    }
    if (snap.contains(QStringLiteral("motion_inhibit")))
        s.tcsMotionInhibit = snap.value(QStringLiteral("motion_inhibit")).toBool(false);

    return s;
}

inline TcsResponse apiResponseToCommandResponse(const ApiResponse &api, const QString &cmd) {
    TcsResponse r;
    r.ok = api.ok;
    r.cmd = cmd;
    r.error = api.error;
    if (!api.ok)
        return r;
    const QJsonObject d = api.data;
    r.motionComplete = d.value(QStringLiteral("success")).toBool(
        d.value(QStringLiteral("motion_complete")).toBool(false));
    r.motionDetail = d.value(QStringLiteral("detail")).toString(
        d.value(QStringLiteral("motion_detail")).toString());
    r.targetDeg = d.value(QStringLiteral("target_deg")).toDouble(0.0);
    r.speed = d.value(QStringLiteral("speed")).toDouble(0.0);
    if (d.contains(QStringLiteral("tcs_snapshot")))
        r.tcsSnapshot = d.value(QStringLiteral("tcs_snapshot")).toObject();
    r.beamPermit = d.value(QStringLiteral("beam_permit_placeholder")).toBool(false);
    r.beamPermitReason = d.value(QStringLiteral("beam_permit_reason")).toString();
    r.pong = cmd == QStringLiteral("ping") && api.ok;
    return r;
}

// ============================================================================
// JSON 行编解码工具（兼容旧 TCS 行协议）
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
    r.tcsSnapshot = obj.value("tcs_snapshot").toObject();
    return r;
}
