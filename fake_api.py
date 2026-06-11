# -*- coding: utf-8 -*-
"""
Qt 前端独立测试用的模拟 API。
不连 PLC，返回固定假数据，供前端功能验证。

用法:
    python fake_api.py
    # 然后 Qt 前端连接 127.0.0.1:8080

访问 http://127.0.0.1:8080/docs 查看所有端点。
"""

import math
import os
import time
from datetime import datetime, timezone

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

app = FastAPI(title="PGM Mock API", version="1.0.0")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

_START_TIME = time.time()

# ============================================================
# 辅助
# ============================================================

def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()

def _ok(data):
    return {"ok": True, "data": data, "timestamp": _now_iso()}

def _fail(error, code="ERROR"):
    return {"ok": False, "error": error, "error_code": code, "timestamp": _now_iso()}

def _fake_angle() -> float:
    """模拟角度在 45° 附近正弦摆动"""
    elapsed = time.time() - _START_TIME
    return round(45.0 + 3.0 * math.sin(elapsed * 0.3), 4)

def _fake_angle_abs02() -> float:
    """ABS02 略有偏差"""
    return round(_fake_angle() + 0.02, 4)

# ============================================================
# GET 端点
# ============================================================

@app.get("/api/v1/health")
async def health():
    return _ok({"plc_connected": True})

@app.get("/api/v1/status")
async def status():
    """完整状态快照 —— 模拟自动模式、寻零完成、所有安全正常"""
    angle = _fake_angle()

    # 模拟 70 个 DI 位
    bits = [False] * 70
    bits[0] = True   # 00000 自动模式
    bits[5] = True   # 00005 寻零完成
    bits[17] = True  # 00017 气压1 OK
    bits[18] = True  # 00018 气压2 OK
    bits[20] = True  # 00020 正极限 OK
    bits[21] = True  # 00021 负极限 OK

    return _ok({
        "discrete_bits": bits,
        "safety": {
            "e_stop_any": False,
            "safety_relay_not_ready": False,
            "limit_pos_185_bad": False,
            "limit_neg_185_bad": False,
            "at_pos_limit": False,
            "at_neg_limit": False,
            "angle_out": False,
            "target_angle_out": False,
            "air_inlet_fault": False,
            "air1_low": False,
            "air2_low": False,
            "servo_fault": False,
            "motion_inhibit": False,
        },
        "analog": {
            "ir_8194_abs02_angle_deg": _fake_angle_abs02(),
            "ir_8198_servo_speed": 0.0,
            "ir_8200_servo1_torque": 1.23,
            "ir_8202_servo2_torque": 1.15,
            "ir_8204_axial_slip1": 0.01,
            "ir_8206_axial_slip2": 0.02,
            "ir_8208_shear_force": 0.5,
            "ir_8210_estop_overshoot": 0.0,
            "hr_24576_speed_setpoint_deg": 3.0,
        },
        "tcs_snapshot": {
            "di_00000_auto_mode": True,
            "di_00001_manual_mode": False,
            "di_00003_homing_running": False,
            "di_00004_position_mode_running": False,
            "di_00005_homing_done": True,
            "di_00006_motor_running": False,
            "di_00019_zero_switch": False,
            "di_00033_plc_estop": False,
            "di_00034_estop1": False,
            "di_00035_estop2": False,
            "di_00036_estop3": False,
            "di_00037_safety_relay_not_ready": False,
            "di_00042_angle_out_of_range": False,
            "di_00043_target_angle_out_of_range": False,
            "di_00017_air1_pressure_ok": True,
            "di_00018_air2_pressure_ok": True,
            "di_00040_air1_low": False,
            "di_00041_air2_low": False,
            "ir_8192_abs01_angle_deg": angle,
            "ir_8196_servo_angle_deg": angle,
            "hr_24578_position_setpoint_deg": 45.0,
            "brakes_open_11_16": [False, False, False, False, False, False],
            "motion_inhibit": False,
            "beam_permit_placeholder": True,
            "beam_permit_reason": "许可=1(模拟)",
        },
        "beam_permit_placeholder": True,
        "beam_permit_reason": "许可=1(模拟)",
    })

@app.get("/api/v1/status/safety")
async def safety():
    return _ok({"e_stop_any": False, "safety_relay_not_ready": False,
                "motion_inhibit": False, "servo_fault": False,
                "air_inlet_fault": False, "air1_low": False, "air2_low": False,
                "limit_pos_185_bad": False, "limit_neg_185_bad": False,
                "at_pos_limit": False, "at_neg_limit": False,
                "angle_out": False, "target_angle_out": False})

@app.get("/api/v1/status/angles")
async def angles():
    a = _fake_angle()
    return _ok({"servo_angle_deg": a, "abs01_angle_deg": a,
                "abs02_angle_deg": _fake_angle_abs02(), "current_angle_deg": a})

@app.get("/api/v1/verify")
async def verify():
    return _ok({"all_ok": True, "errors": []})

# ============================================================
# POST 端点 —— 全部直接返回 ok
# ============================================================

class JogBody(BaseModel):
    forward: bool = True
    speed: float = 3.0
    seconds: float = 1.0

class PositionBody(BaseModel):
    angle: float
    speed: float = 3.0
    timeout: float = 300.0
    tol: float = 0.5
    arrival_mode: str = "hybrid"
    di04_grace: float = 5.0
    plateau_n: int = 5
    require_homing: bool = True
    auto_mode: bool = True

class BrakesOpenBody(BaseModel):
    confirm: bool = False

@app.post("/api/v1/motion/home")
async def motion_home():
    return _ok({"success": True, "detail": "寻零完成(模拟)"})

@app.post("/api/v1/motion/jog")
async def motion_jog(body: JogBody):
    return _ok({"success": True,
                "detail": f"点动完成: {'正转' if body.forward else '反转'} {body.seconds}s"})

@app.post("/api/v1/motion/position")
async def motion_position(body: PositionBody):
    return _ok({"success": True, "detail": f"到位(模拟): 目标={body.angle}°",
                "target_deg": body.angle, "speed": body.speed})

@app.post("/api/v1/motion/stop")
async def motion_stop():
    return _ok({"detail": "手动运动已停止(模拟)"})

@app.post("/api/v1/motion/estop")
async def motion_estop():
    return _ok({"detail": "16391 急停脉冲已发送(模拟)"})

@app.post("/api/v1/safety/reset")
async def safety_reset():
    return _ok({"detail": "故障复位脉冲已发送(模拟)"})

@app.post("/api/v1/safety/estop2-recover")
async def safety_estop2_recover():
    return _ok({"success": True, "detail": "急停2恢复完成(模拟)"})

@app.post("/api/v1/brakes/close")
async def brakes_close():
    return _ok({"detail": "16399 一键关制动已发送(模拟)"})

@app.post("/api/v1/brakes/open")
async def brakes_open(body: BrakesOpenBody):
    if not body.confirm:
        return _fail("打开制动器须 confirm=true", "CONFIRM_REQUIRED")
    return _ok({"detail": "16398 一键开制动已发送(模拟)"})

@app.post("/api/v1/mode/auto")
async def mode_auto():
    return _ok({"detail": "自动模式(模拟)"})

@app.post("/api/v1/mode/manual")
async def mode_manual():
    return _ok({"detail": "手动模式(模拟)"})

@app.post("/api/v1/workflow/self-test")
async def workflow_self_test():
    return _ok({"success": True, "failures": []})

@app.post("/api/v1/workflow/full")
async def workflow_full():
    return _ok({"success": True, "detail": "完整工作流完成(模拟)"})

# ============================================================
# 启动
# ============================================================

if __name__ == "__main__":
    import uvicorn
    host = os.environ.get("FAKE_API_HOST", "0.0.0.0")
    port = int(os.environ.get("FAKE_API_PORT", "8080"))
    print(f"模拟 API 启动: http://{host}:{port}")
    print(f"Swagger 文档: http://127.0.0.1:{port}/docs")
    uvicorn.run(app, host=host, port=port)
