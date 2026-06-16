#include "main_window.h"
#include <QStringConverter>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kLayoutSpacing = 6;
constexpr int kCompactSpacing = 3;
constexpr int kGroupInset = 4;
constexpr int kBtnMinWidth = 72;
constexpr int kTitleBarHeight = 44;
constexpr int kStatusMaxWidth = 340;
constexpr int kRightPanelMinWidth = 300;
constexpr int kRightPanelMaxWidth = 400;
constexpr int kRightGroupSpacing = 8;
constexpr int kMotionLayerSpacing = 8;
constexpr int kSafetyBtnHeight = 32;
constexpr int kParamValueColWidth = 80;
constexpr int kGaugeMinSize = 200;

constexpr int kStatusPanelMaxWidth = 172;

const QString kBeamPermitLedLabel = QStringLiteral("可出束[软件占位]");
const QString kBeamPermitLedTooltip = QStringLiteral(
    "PGM 软件占位预判（beam_permit_placeholder），仅供监视参考。\n"
    "最终出束许可由 TCS 治疗控制系统决定；本指示灯亮≠允许临床出束。");
}

// ============================================================================
// 全局样式表: 工业暗色主题
// ============================================================================
static const char *kStyle = R"(
QMainWindow { background-color: #1a1a20; }
QGroupBox {
    font-weight: bold; color: #c0c0d0;
    border: 1px solid #3a3a45; border-radius: 4px;
    margin-top: 12px; padding-top: 8px;
    background-color: #22222a;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 12px; padding: 2px 8px;
}
QGroupBox#gbEstop::title {
    background-color: #8b2020; color: #ffffff;
    border-radius: 3px; padding: 4px 12px;
}
QLabel { color: #d0d0d8; }
QPushButton {
    background-color: #333340; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 4px;
    padding: 4px 8px; min-height: 24px; max-height: 28px;
}
QPushButton:hover { background-color: #3d3d4a; border-color: #6a6a78; }
QPushButton:pressed { background-color: #2a2a32; }
QPushButton#btnEstop {
    background-color: #b82020; border: 1px solid #ff5050;
    color: #ffffff; font-weight: bold;
}
QPushButton#btnEstop:hover { background-color: #d02828; }
QPushButton#btnEstop:pressed { background-color: #901818; }
QPushButton#btnConnect {
    background-color: #204020; border-color: #40a040; color: #80ff80;
}
QPushButton#btnDisconnect {
    background-color: #4a3a10; border-color: #a08030; color: #ffd060;
}
QPushButton#btnMove {
    background-color: #203050; border-color: #4060a0; color: #80b0ff;
}
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
    background-color: #2a2a32; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 3px; padding: 4px 6px;
    min-height: 24px;
}
QWidget#titleBar {
    background-color: #141820;
    border-bottom: 2px solid #2a4060;
}
QLabel#titleText {
    color: #e8ecf4; font-size: 16pt; font-weight: bold;
}
QLabel#titleSub {
    color: #8090a8; font-size: 9pt;
}
QLabel.paramName {
    color: #888898; font-size: 9pt;
}
QLabel.paramValue {
    color: #e8e8f0; font-size: 10pt; font-weight: bold;
}
)";

// ============================================================================
// 布局辅助
// ============================================================================

void MainWindow::compactGroupLayout(QLayout *layout) {
    if (!layout) return;
    layout->setSpacing(kLayoutSpacing);
    layout->setContentsMargins(6, 10, 6, 6);
}

void MainWindow::styleUniformButtons(const QList<QPushButton *> &buttons, int minWidth) {
    for (auto *btn : buttons) {
        if (!btn) continue;
        btn->setMinimumWidth(minWidth);
        btn->setMinimumHeight(26);
        btn->setMaximumHeight(30);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

static void styleSafetyButtons(const QList<QPushButton *> &buttons) {
    for (auto *btn : buttons) {
        if (!btn) continue;
        btn->setMinimumHeight(kSafetyBtnHeight);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

// ============================================================================
// 构造 / 析构
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("PGM 旋转机架控制系统");
    resize(1360, 860);
    setMinimumSize(1120, 720);
    setStyleSheet(kStyle);
    buildUi();

    connect(&m_client, &GantryClient::connected, this, [this]() {
        m_connStatusLamp->setStyleSheet(
            "background-color:#30d050; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText("已连接 (HTTP)");
        m_connStatusLabel->setStyleSheet("color:#30d050; font-weight:bold;");
        m_pollTimer.start(200);
        updateControlsForConnectionMode();
        onLogMessage("=== 已连接 ===");
    });
    connect(&m_client, &GantryClient::disconnected, this, [this]() {
        m_connStatusLamp->setStyleSheet(
            "background-color:#d04040; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText("已断开");
        m_connStatusLabel->setStyleSheet("color:#d04040; font-weight:bold;");
        m_pollTimer.stop();
        resetAllLeds();
        updateControlsForConnectionMode();
        onLogMessage("=== 已断开 ===");
    });
    connect(&m_client, &GantryClient::statusUpdated,
            this, &MainWindow::onStatusUpdated);
    connect(&m_client, &GantryClient::commandResponse,
            this, &MainWindow::onCommandResponse);
    connect(&m_client, &GantryClient::logMessage,
            this, &MainWindow::onLogMessage);
    connect(&m_client, &GantryClient::communicationError,
            this, &MainWindow::onConnectionError);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        m_client.pollStatus();
    });

    connect(&m_client, &GantryClient::motionFinished, this, [this]() {
        m_motionBusy = false;
        setMotionButtonsEnabled(true);
    });

    m_chartTimer.start();
}

MainWindow::~MainWindow() {
    m_pollTimer.stop();
    m_client.disconnect();
}

// ============================================================================
// 主布局 — 5 大区
// ============================================================================

void MainWindow::buildUi() {
    auto *cw = new QWidget(this);
    setCentralWidget(cw);

    auto *root = new QVBoxLayout(cw);
    root->setSpacing(kLayoutSpacing);
    root->setContentsMargins(8, 8, 8, 8);

    // 1. 顶部标题栏
    root->addWidget(buildTitleBar());

    // 上排四块：系统状态(窄列) | 表盘 | 实时参数 | 右侧控制(加宽)
    auto *upper = new QHBoxLayout;
    upper->setSpacing(kLayoutSpacing);
    upper->addWidget(buildStatusPanel(), 0);
    upper->addWidget(buildGaugePanel(), 2);
    upper->addWidget(buildParametersPanel(), 2);
    upper->addWidget(buildRightPanel(), 5);
    root->addLayout(upper, 3);

    // 5. 底部：曲线 | 日志（左右平分）
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(kLayoutSpacing);
    bottom->addWidget(buildChartPanel(), 1);
    bottom->addWidget(buildLogPanel(), 1);
    root->addLayout(bottom, 2);

    // 所有控件构建完成后统一设置初始状态
    updateControlsForConnectionMode();
}

// ============================================================================
// 1. 顶部标题栏
// ============================================================================

QWidget *MainWindow::buildTitleBar() {
    auto *bar = new QWidget;
    bar->setObjectName("titleBar");
    bar->setFixedHeight(kTitleBarHeight);

    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(16, 0, 16, 0);
    h->setSpacing(12);

    auto *title = new QLabel("PGM 旋转机架控制系统");
    title->setObjectName("titleText");
    h->addWidget(title);

    auto *sub = new QLabel("Proton Gantry HTTP REST 上位机");
    sub->setObjectName("titleSub");
    h->addWidget(sub);
    h->addStretch();

    auto *ver = new QLabel("v1.0");
    ver->setStyleSheet("color:#606878; font-size:9pt;");
    h->addWidget(ver, 0, Qt::AlignVCenter);

    return bar;
}

// ============================================================================
// LED 工具
// ============================================================================

MainWindow::LedItem MainWindow::makeLed(const QString &label) {
    LedItem item;
    item.dot = new QLabel;
    item.dot->setFixedSize(8, 8);
    item.dot->setStyleSheet(
        "background-color:#555; border-radius:4px;"
        "min-width:8px; min-height:8px;");
    item.text = new QLabel(label);
    item.text->setStyleSheet("color:#b0b0b8; font-size:8pt;");
    item.text->setMinimumWidth(0);
    return item;
}

void MainWindow::addLedToGrid(QGridLayout *grid, int row, int col,
                              LedItem &item, const QString &label) {
    item = makeLed(label);
    auto *cell = new QWidget;
    auto *hl = new QHBoxLayout(cell);
    hl->setContentsMargins(1, 0, 2, 0);
    hl->setSpacing(4);
    hl->addWidget(item.dot, 0, Qt::AlignVCenter);
    hl->addWidget(item.text, 1, Qt::AlignVCenter);
    item.text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    grid->addWidget(cell, row, col);
}

void MainWindow::setLedColor(LedItem &led, bool on, bool running) {
    if (running)
        led.dot->setStyleSheet(
            "background-color:#e0b020; border-radius:5px;"
            "min-width:8px; min-height:8px;");
    else if (on)
        led.dot->setStyleSheet(
            "background-color:#30d050; border-radius:4px;"
            "min-width:8px; min-height:8px;");
    else
        led.dot->setStyleSheet(
            "background-color:#e04040; border-radius:4px;"
            "min-width:8px; min-height:8px;");
    led.text->setStyleSheet(
        on ? "color:#e0e0e8; font-size:8pt;" : "color:#b0b0b8; font-size:8pt;");
}

// ============================================================================
// 2. 左上 — 系统状态（单列垂直，完整名称）
// ============================================================================

QWidget *MainWindow::buildStatusPanel() {
    auto *gb = new QGroupBox("系统状态");
    auto *v = new QVBoxLayout(gb);
    v->setSpacing(2);
    v->setContentsMargins(4, 8, 4, 4);

    auto addLedRow = [&](LedItem &item, const QString &label,
                          const QString &tooltip = QString()) {
        item = makeLed(label);
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(1, 0, 2, 0);
        hl->setSpacing(4);
        hl->addWidget(item.dot, 0, Qt::AlignVCenter);
        hl->addWidget(item.text, 1, Qt::AlignVCenter);
        item.text->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        if (!tooltip.isEmpty()) {
            row->setToolTip(tooltip);
            item.dot->setToolTip(tooltip);
            item.text->setToolTip(tooltip);
        }
        v->addWidget(row);
    };

    addLedRow(m_ledAuto,          "自动模式");
    addLedRow(m_ledManual,        "手动模式");
    addLedRow(m_ledSpeed,         "速度模式");
    addLedRow(m_ledHoming,        "寻零运行");
    addLedRow(m_ledPosition,      "位置运行");
    addLedRow(m_ledMotor,         "电机运行");
    addLedRow(m_ledHomingDone,    "寻零完成");
    addLedRow(m_ledEstop,         "急停正常");
    addLedRow(m_ledSafety,        "安全继电器");
    addLedRow(m_ledAir,           "气压正常");
    addLedRow(m_ledBrakes,        "制动器关闭");
    addLedRow(m_ledMotionInhibit, "运动允许");
    addLedRow(m_ledBeamPermit, kBeamPermitLedLabel, kBeamPermitLedTooltip);

    gb->setMaximumWidth(kStatusPanelMaxWidth);
    gb->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    return gb;
}

// ============================================================================
// 3. 中 — 角度表盘
// ============================================================================

QWidget *MainWindow::buildGaugePanel() {
    auto *gb = new QGroupBox("角度表盘");
    auto *l = new QVBoxLayout(gb);
    compactGroupLayout(l);

    m_gauge = new GaugeWidget;
    m_gauge->setTitle("旋转机架");
    m_gauge->setMinimumSize(kGaugeMinSize, kGaugeMinSize);
    m_gauge->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l->addWidget(m_gauge);

    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

void MainWindow::addParamCell(QGridLayout *grid, int row, int colBase,
                              const QString &name, QLabel *&valueOut) {
    auto *nameLb = new QLabel(name);
    nameLb->setFixedWidth(kParamNameColWidth);
    nameLb->setStyleSheet("color:#888898; font-size:8pt;");
    nameLb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    valueOut = new QLabel("—");
    valueOut->setFixedWidth(kParamValueColWidth);
    valueOut->setStyleSheet("color:#e8e8f0; font-size:9pt; font-weight:bold;");
    valueOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueOut->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    grid->addWidget(nameLb, row, colBase);
    grid->addWidget(valueOut, row, colBase + 1);
}

// ============================================================================
// 4. 中右 — 实时参数（两列网格，固定列宽）
// ============================================================================

QWidget *MainWindow::buildParametersPanel() {
    auto *gb = new QGroupBox("实时参数");
    auto *grid = new QGridLayout(gb);
    grid->setSpacing(kCompactSpacing);
    grid->setContentsMargins(kGroupInset, kGroupInset + 2, kGroupInset, kGroupInset);
    grid->setColumnMinimumWidth(0, kParamNameColWidth);
    grid->setColumnMinimumWidth(1, kParamValueColWidth);
    grid->setColumnMinimumWidth(2, kParamNameColWidth);
    grid->setColumnMinimumWidth(3, kParamValueColWidth);

    addParamCell(grid, 0, 0, "\u4F3A\u670D\u89D2(\u00B0)",   m_labelServoAngle);
    addParamCell(grid, 0, 2, "ABS_01(\u00B0)",   m_labelAbs01Angle);
    addParamCell(grid, 1, 0, "ABS_02(\u00B0)",   m_labelAbs02Angle);
    addParamCell(grid, 1, 2, "\u901F\u5EA6(\u00B0/s)",     m_labelCurrentSpeed);
    addParamCell(grid, 2, 0, "\u4F4D\u7ED9\u5B9A(\u00B0)",   m_labelPositionSetpoint);
    addParamCell(grid, 2, 2, "\u901F\u7ED9\u5B9A(\u00B0/s)", m_labelSpeedSetpoint);
    addParamCell(grid, 3, 0, "\u626D\u77E91(Nm)",    m_labelServo1Torque);
    addParamCell(grid, 3, 2, "\u626D\u77E92(Nm)",    m_labelServo2Torque);
    addParamCell(grid, 4, 0, "\u4E32\u52A81(mm)",    m_labelSlip1);
    addParamCell(grid, 4, 2, "\u4E32\u52A82(mm)",    m_labelSlip2);
    addParamCell(grid, 5, 0, "\u526A\u5207\u529B(N)",   m_labelShearForce);
    addParamCell(grid, 5, 2, "\u8FC7\u51B2(\u00B0)",     m_labelEstopOvershoot);

    gb->setMaximumWidth(kParamNameColWidth * 2 + kParamValueColWidth * 2 + 24);
    gb->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);
    return gb;
}

// ============================================================================
// 5. 最右侧 — 连接 / 运动 / 安全（三等高）
// ============================================================================

QWidget *MainWindow::buildRightPanel() {
    auto *wrap = new QWidget;
    wrap->setMinimumWidth(kRightPanelMinWidth);
    auto *v = new QVBoxLayout(wrap);
    v->setSpacing(kRightGroupSpacing);
    v->setContentsMargins(0, 0, 0, 0);

    v->addWidget(buildConnectionPanel(), 1);
    v->addWidget(buildMotionPanel(), 1);
    v->addWidget(buildSafetyPanel(), 1);

    wrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return wrap;
}

QWidget *MainWindow::buildConnectionPanel() {
    auto *gb = new QGroupBox("连接设置");
    auto *v = new QVBoxLayout(gb);
    compactGroupLayout(v);
    v->setSpacing(kLayoutSpacing);

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(kCompactSpacing);
    auto *hostLb = new QLabel("主机");
    hostLb->setFixedWidth(36);
    m_hostEdit = new QLineEdit("127.0.0.1");
    m_hostEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *portLb = new QLabel("端口");
    portLb->setFixedWidth(36);
    m_portEdit = new QLineEdit("8080");
    m_portEdit->setFixedWidth(56);
    inputRow->addWidget(hostLb);
    inputRow->addWidget(m_hostEdit, 2);
    inputRow->addWidget(portLb);
    inputRow->addWidget(m_portEdit);
    v->addLayout(inputRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(kLayoutSpacing);
    auto *btnC = new QPushButton("连接");
    btnC->setObjectName("btnConnect");
    connect(btnC, &QPushButton::clicked, this, &MainWindow::connectToPlc);
    auto *btnD = new QPushButton("断开");
    btnD->setObjectName("btnDisconnect");
    connect(btnD, &QPushButton::clicked, this, &MainWindow::disconnectFromPlc);
    styleUniformButtons({btnC, btnD}, 0);
    actionRow->addWidget(btnC, 1);
    actionRow->addWidget(btnD, 1);

    m_connStatusLamp = new QLabel;
    m_connStatusLamp->setFixedSize(14, 14);
    m_connStatusLamp->setStyleSheet(
        "background-color:#d04040; border-radius:7px; min-width:14px; min-height:14px;");
    m_connStatusLabel = new QLabel("已断开");
    m_connStatusLabel->setStyleSheet("color:#d04040; font-size:10pt; font-weight:bold;");
    actionRow->addWidget(m_connStatusLamp, 0, Qt::AlignVCenter);
    actionRow->addWidget(m_connStatusLabel, 1, Qt::AlignVCenter);
    v->addLayout(actionRow);
    v->addStretch(1);

    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

QWidget *MainWindow::buildMotionPanel() {
    auto *gb = new QGroupBox("运动控制");
    auto *v = new QVBoxLayout(gb);
    compactGroupLayout(v);
    v->setSpacing(kMotionLayerSpacing);

    m_motionModbusBlock = new QWidget;
    auto *modbusV = new QVBoxLayout(m_motionModbusBlock);
    modbusV->setSpacing(kMotionLayerSpacing);
    modbusV->setContentsMargins(0, 0, 0, 0);

    // ① 模式：四按钮等宽整行
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(kLayoutSpacing);
    m_btnAuto = new QPushButton("自动");
    connect(m_btnAuto, &QPushButton::clicked, this, &MainWindow::setAutoMode);
    m_btnManual = new QPushButton("手动");
    connect(m_btnManual, &QPushButton::clicked, this, &MainWindow::setManualMode);
    m_btnHome = new QPushButton("寻零");
    connect(m_btnHome, &QPushButton::clicked, this, &MainWindow::startHoming);
    m_btnReset = new QPushButton("复位");
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::resetFault);
    styleUniformButtons({m_btnAuto, m_btnManual, m_btnHome, m_btnReset}, 0);
    modeRow->addWidget(m_btnAuto, 1);
    modeRow->addWidget(m_btnManual, 1);
    modeRow->addWidget(m_btnHome, 1);
    modeRow->addWidget(m_btnReset, 1);
    modbusV->addLayout(modeRow);

    // ② 点动：参数组 + 三按钮组
    auto *jogRow = new QHBoxLayout;
    jogRow->setSpacing(kLayoutSpacing);
    auto *jogParams = new QHBoxLayout;
    jogParams->setSpacing(kCompactSpacing);
    auto *jogSpdLb = new QLabel("速度");
    jogSpdLb->setFixedWidth(32);
    m_jogSpeedSpin = new QDoubleSpinBox;
    m_jogSpeedSpin->setRange(0.1, 20.0);
    m_jogSpeedSpin->setValue(3.0);
    m_jogSpeedSpin->setSuffix(" °/s");
    m_jogSpeedSpin->setDecimals(1);
    m_jogSpeedSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *jogSecLb = new QLabel("时长");
    jogSecLb->setFixedWidth(32);
    m_jogSecondsSpin = new QDoubleSpinBox;
    m_jogSecondsSpin->setRange(0.1, 60.0);
    m_jogSecondsSpin->setValue(1.0);
    m_jogSecondsSpin->setSuffix(" s");
    m_jogSecondsSpin->setDecimals(1);
    m_jogSecondsSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    jogParams->addWidget(jogSpdLb);
    jogParams->addWidget(m_jogSpeedSpin, 1);
    jogParams->addWidget(jogSecLb);
    jogParams->addWidget(m_jogSecondsSpin, 1);

    auto *jogBtns = new QHBoxLayout;
    jogBtns->setSpacing(kLayoutSpacing);
    auto *btnFwd = new QPushButton("正转");
    auto *btnRev = new QPushButton("反转");
    auto *btnStop = new QPushButton("停止");
    connect(btnFwd, &QPushButton::clicked, this, &MainWindow::jogFwd);
    connect(btnRev, &QPushButton::clicked, this, &MainWindow::jogRev);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopManual);
    styleUniformButtons({btnFwd, btnRev, btnStop}, 0);
    jogBtns->addWidget(btnFwd, 1);
    jogBtns->addWidget(btnRev, 1);
    jogBtns->addWidget(btnStop, 1);

    jogRow->addLayout(jogParams, 1);
    jogRow->addLayout(jogBtns, 1);
    modbusV->addLayout(jogRow);
    v->addWidget(m_motionModbusBlock);

    // ③ 定位：角度+速度一行，执行按钮独占下一行
    auto *posInputRow = new QHBoxLayout;
    posInputRow->setSpacing(kLayoutSpacing);
    auto *angLb = new QLabel("角度");
    angLb->setFixedWidth(32);
    m_targetAngleSpin = new QDoubleSpinBox;
    m_targetAngleSpin->setRange(-185.0, 185.0);
    m_targetAngleSpin->setValue(0.0);
    m_targetAngleSpin->setSuffix(" °");
    m_targetAngleSpin->setDecimals(2);
    m_targetAngleSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *spdLb = new QLabel("速度");
    spdLb->setFixedWidth(32);
    m_targetSpeedSpin = new QDoubleSpinBox;
    m_targetSpeedSpin->setRange(0.1, 20.0);
    m_targetSpeedSpin->setValue(3.0);
    m_targetSpeedSpin->setSuffix(" °/s");
    m_targetSpeedSpin->setDecimals(1);
    m_targetSpeedSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    posInputRow->addWidget(angLb);
    posInputRow->addWidget(m_targetAngleSpin, 1);
    posInputRow->addWidget(spdLb);
    posInputRow->addWidget(m_targetSpeedSpin, 1);
    auto *toLb = new QLabel("\u8D85\u65F6");
    toLb->setFixedWidth(32);
    m_timeoutSpin = new QDoubleSpinBox;
    m_timeoutSpin->setRange(10.0, 600.0);
    m_timeoutSpin->setValue(300.0);
    m_timeoutSpin->setSuffix(" s");
    m_timeoutSpin->setDecimals(0);
    m_timeoutSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    posInputRow->addWidget(toLb);
    posInputRow->addWidget(m_timeoutSpin, 1);
    v->addLayout(posInputRow);

    auto *btnMove = new QPushButton("\u6267\u884C\u5B9A\u4F4D");
    btnMove->setObjectName("btnMove");
    connect(btnMove, &QPushButton::clicked, this, &MainWindow::moveToPosition);
    styleUniformButtons({btnMove}, 0);
    m_btnMove = btnMove;
    v->addWidget(btnMove);
    
    // ④ 工作流按钮行：自检 + 完整工作流
    auto *wfRow = new QHBoxLayout;
    wfRow->setSpacing(kLayoutSpacing);
    m_btnSelfTest = new QPushButton("\u4E0A\u7535\u81EA\u68C0");
    connect(m_btnSelfTest, &QPushButton::clicked, this, &MainWindow::runSelfTest);
    m_btnWorkflow = new QPushButton("\u5B8C\u6574\u5DE5\u4F5C\u6D41");
    connect(m_btnWorkflow, &QPushButton::clicked, this, &MainWindow::runWorkflowFull);
    styleUniformButtons({m_btnSelfTest, m_btnWorkflow}, 0);
    wfRow->addWidget(m_btnSelfTest, 1);
    wfRow->addWidget(m_btnWorkflow, 1);
    v->addLayout(wfRow);
    
    v->addStretch(1);
    
    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

QWidget *MainWindow::buildSafetyPanel() {
    auto *gb = new QGroupBox("安全控制");
    auto *v = new QVBoxLayout(gb);
    compactGroupLayout(v);
    v->setSpacing(kLayoutSpacing);

    m_btnEstop = new QPushButton("触发急停");
    m_btnEstop->setObjectName("btnEstop");
    connect(m_btnEstop, &QPushButton::clicked, this, &MainWindow::emergencyStop);

    m_btnBrakesClose = new QPushButton("关闭制动");
    connect(m_btnBrakesClose, &QPushButton::clicked, this, &MainWindow::closeBrakes);

    m_btnBrakesOpen = new QPushButton("打开制动");
    connect(m_btnBrakesOpen, &QPushButton::clicked, this, &MainWindow::openBrakes);

    m_btnEstopRecover = new QPushButton(QStringLiteral("\u6025\u505C\u6062\u590D"));
    m_btnEstopRecover->setToolTip(QStringLiteral("\u663E\u793A\u5F53\u524D\u6025\u505C\u72B6\u6001\u5E76\u63D0\u4F9B\u6062\u590D\u64CD\u4F5C"));
    connect(m_btnEstopRecover, &QPushButton::clicked, this, &MainWindow::recoverEstop);

    styleSafetyButtons({m_btnEstop, m_btnBrakesClose, m_btnBrakesOpen, m_btnEstopRecover});

    v->addStretch(1);
    v->addWidget(m_btnEstop);
    v->addStretch(1);
    v->addWidget(m_btnBrakesClose);
    v->addStretch(1);
    v->addWidget(m_btnBrakesOpen);
    v->addStretch(1);
    v->addWidget(m_btnEstopRecover);
    v->addStretch(1);

    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

// ============================================================================
// 5. 底部 — 角度曲线（左）+ 通信日志（右）
// ============================================================================

QWidget *MainWindow::buildChartPanel() {
    auto *gb = new QGroupBox("角度实时曲线");
    auto *l = new QVBoxLayout(gb);
    l->setContentsMargins(6, 6, 6, 6);
    l->setSpacing(4);

    m_angleSeries = new QLineSeries;
    m_angleSeries->setName("ABS_01 角度");
    m_angleSeries->setColor(QColor(60, 180, 255));
    m_angleSeries->setPen(QPen(QColor(60, 180, 255), 2));

    m_targetScatter = new QScatterSeries;
    m_targetScatter->setName("目标角度");
    m_targetScatter->setColor(QColor(255, 200, 60));
    m_targetScatter->setMarkerSize(8);

    m_chart = new QChart;
    m_chart->addSeries(m_angleSeries);
    m_chart->addSeries(m_targetScatter);
    m_chart->setAnimationOptions(QChart::NoAnimation);
    m_chart->legend()->setLabelColor(QColor(200, 200, 210));
    m_chart->setBackgroundBrush(QColor(24, 24, 30));
    m_chart->setPlotAreaBackgroundBrush(QColor(20, 20, 26));
    m_chart->setPlotAreaBackgroundVisible(true);

    m_timeAxis = new QDateTimeAxis;
    m_timeAxis->setFormat("HH:mm:ss");
    m_timeAxis->setLabelsColor(QColor(160, 160, 170));
    m_timeAxis->setGridLineColor(QColor(60, 60, 70));
    m_chart->addAxis(m_timeAxis, Qt::AlignBottom);
    m_angleSeries->attachAxis(m_timeAxis);
    m_targetScatter->attachAxis(m_timeAxis);

    m_angleAxis = new QValueAxis;
    m_angleAxis->setRange(ModbusAddr::CHART_ANGLE_AXIS_MIN,
                          ModbusAddr::CHART_ANGLE_AXIS_MAX);
    m_angleAxis->setLabelsColor(QColor(160, 160, 170));
    m_angleAxis->setGridLineColor(QColor(60, 60, 70));
    m_chart->addAxis(m_angleAxis, Qt::AlignLeft);
    m_angleSeries->attachAxis(m_angleAxis);
    m_targetScatter->attachAxis(m_angleAxis);

    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l->addWidget(m_chartView);

    // 时间窗口选择 + 导出按钮
    auto *chartBar = new QHBoxLayout;
    chartBar->setSpacing(kLayoutSpacing);
    auto *winLb = new QLabel(QStringLiteral("\u7A97\u53E3"));
    winLb->setFixedWidth(28);
    m_chartWindowCombo = new QComboBox;
    m_chartWindowCombo->addItems({QStringLiteral("30s"), QStringLiteral("1min"),
                                  QStringLiteral("2min"), QStringLiteral("5min")});
    m_chartWindowCombo->setFixedWidth(64);
    auto *btnPng = new QPushButton(QStringLiteral("\u622A\u56FE"));
    btnPng->setFixedWidth(kBtnMinWidth);
    connect(btnPng, &QPushButton::clicked, this, &MainWindow::exportChartPng);
    auto *btnCsv = new QPushButton(QStringLiteral("CSV"));
    btnCsv->setFixedWidth(kBtnMinWidth);
    connect(btnCsv, &QPushButton::clicked, this, &MainWindow::exportChartCsv);
    chartBar->addWidget(winLb);
    chartBar->addWidget(m_chartWindowCombo);
    chartBar->addStretch(1);
    chartBar->addWidget(btnPng);
    chartBar->addWidget(btnCsv);
    l->addLayout(chartBar);

    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

QWidget *MainWindow::buildLogPanel() {
    auto *gb = new QGroupBox("通信日志");
    auto *l = new QVBoxLayout(gb);
    l->setContentsMargins(6, 6, 6, 6);
    l->setSpacing(4);

    m_logTable = new QTableWidget(0, 3);
    m_logTable->setHorizontalHeaderLabels({"日期", "时间", "日志详情"});
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_logTable->setShowGrid(true);
    m_logTable->setStyleSheet(
        "QTableWidget {"
        "  background-color:#2C2C34; color:#FFFFFF;"
        "  border:1px solid #44444E; border-radius:4px;"
        "  gridline-color:#3E3E46;"
        "  font-family:\"Consolas\",\"Courier New\",monospace; font-size:9pt;"
        "}"
        "QHeaderView::section {"
        "  background-color:#1E2A3A; color:#FFFFFF;"
        "  border:1px solid #3A4A5A; padding:4px 8px;"
        "  font-weight:bold; font-size:9pt;"
        "}"
        "QTableWidget::item { padding:2px 6px; color:#FFFFFF; }"
        "QTableWidget::item:selected { background-color:#3A5A8A; color:#FFFFFF; }"
    );
    m_logTable->setColumnWidth(0, 88);
    m_logTable->setColumnWidth(1, 96);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l->addWidget(m_logTable, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(0, 2, 0, 0);
    auto *btnCl = new QPushButton("清空");
    btnCl->setFixedWidth(kBtnMinWidth);
    connect(btnCl, &QPushButton::clicked, this, [this]() { m_logTable->setRowCount(0); });
    btnBar->addWidget(btnCl, 0, Qt::AlignRight);
    l->addLayout(btnBar);

    gb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return gb;
}

// ============================================================================
// 日志行
// ============================================================================

void MainWindow::appendLogRow(const QString &msg) {
    if (!m_logTable) return;
    QString datePart, timePart, detail;
    if (msg.startsWith('[')) {
        int cb = msg.indexOf(']');
        if (cb > 1) {
            datePart = QDate::currentDate().toString("yyyy-MM-dd");
            timePart = msg.mid(1, cb - 1);
            detail = msg.mid(cb + 2).trimmed();
        } else {
            datePart = QDate::currentDate().toString("yyyy-MM-dd");
            timePart = QTime::currentTime().toString("HH:mm:ss.zzz");
            detail = msg;
        }
    } else {
        datePart = QDate::currentDate().toString("yyyy-MM-dd");
        timePart = QTime::currentTime().toString("HH:mm:ss.zzz");
        detail = msg;
    }

    clearLastHighlight();
    while (m_logTable->rowCount() >= kMaxLogRows) {
        m_logTable->removeRow(0);
        if (m_lastHighlightRow >= 0) m_lastHighlightRow--;
    }

    int row = m_logTable->rowCount();
    m_logTable->insertRow(row);
    static const QColor kRowBg(44, 44, 52);
    auto mk = [&](const QString &t) {
        auto *it = new QTableWidgetItem(t);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setForeground(Qt::white);
        it->setBackground(kRowBg);
        return it;
    };
    m_logTable->setItem(row, 0, mk(datePart));
    m_logTable->setItem(row, 1, mk(timePart));
    m_logTable->setItem(row, 2, mk(detail));

    static const QColor kHL(25, 90, 165);
    for (int c = 0; c < 3; ++c) {
        auto *it = m_logTable->item(row, c);
        if (it) {
            it->setBackground(kHL);
            QFont f = it->font();
            f.setBold(true);
            it->setFont(f);
        }
    }
    m_lastHighlightRow = row;
    QTimer::singleShot(2000, this, [this, row]() {
        if (m_lastHighlightRow == row) clearLastHighlight();
    });
    m_logTable->scrollToBottom();
}

void MainWindow::clearLastHighlight() {
    if (m_lastHighlightRow < 0 || !m_logTable) return;
    int rr = m_lastHighlightRow;
    if (rr >= m_logTable->rowCount()) {
        m_lastHighlightRow = -1;
        return;
    }
    static const QColor kBg(44, 44, 52);
    for (int c = 0; c < 3; ++c) {
        auto *it = m_logTable->item(rr, c);
        if (it) {
            it->setBackground(kBg);
            QFont f = it->font();
            f.setBold(false);
            it->setFont(f);
        }
    }
    m_lastHighlightRow = -1;
}

// ============================================================================
// 状态更新
// ============================================================================

void MainWindow::onStatusUpdated(const GantryStatus &s) {
    m_currentStatus = s;
    updateStatusLeds(s);
    updateAngleDisplay(s);
    updateParameterDisplay(s);
    updateChart(ModbusFloat::angleForDisplay(s.abs01AngleDeg));
    if (s.positionSetpoint.has_value()
        && ModbusFloat::isDisplayableAngle(s.positionSetpoint)) {
        m_gauge->setTargetAngle(*s.positionSetpoint);
    } else {
        m_gauge->setTargetAngle(std::numeric_limits<double>::quiet_NaN());
    }
}

void MainWindow::updateStatusLeds(const GantryStatus &s) {
    setLedColor(m_ledAuto,          s.autoModeActive);
    setLedColor(m_ledManual,        s.manualModeActive);
    setLedColor(m_ledSpeed,         s.speedModeRunning, true);
    setLedColor(m_ledHoming,        s.homingRunning,    true);
    setLedColor(m_ledPosition,      s.positionModeRunning, true);
    setLedColor(m_ledMotor,         s.motorRunning,     true);
    setLedColor(m_ledHomingDone,    s.homingDone);
    bool e = s.plcEstop || s.estop1 || s.estop2 || s.estop3;
    setLedColor(m_ledEstop,         !e);
    setLedColor(m_ledSafety,        !s.safetyRelayNotReady);
    bool airOk = s.air1PressureOk && s.air2PressureOk && !s.air1Low && !s.air2Low;
    setLedColor(m_ledAir,           airOk);
    bool brakesClosed = s.brakesOpen.empty()
        || std::none_of(s.brakesOpen.begin(), s.brakesOpen.end(), [](bool o) { return o; });
    setLedColor(m_ledBrakes,        brakesClosed);
    setLedColor(m_ledMotionInhibit, !s.motionInhibitEffective());
    auto [bp, bpr] = s.beamPermit();
    setLedColor(m_ledBeamPermit,    bp);
    m_ledBeamPermit.text->setText(
        bp ? kBeamPermitLedLabel
           : QStringLiteral("\u53EF\u51FA\u675F[\u8F6F\u4EF6\u5360\u4F4D](%1)").arg(bpr));
    // \u5B8C\u6574\u539F\u56E0\u5199\u5165 tooltip\uFF0C\u907F\u514D LED \u6807\u7B7E\u6EA2\u51FA\u622A\u65AD
    if (!bp && !bpr.isEmpty()) {
        m_ledBeamPermit.text->setToolTip(bpr);
        m_ledBeamPermit.dot->setToolTip(bpr);
    } else {
        m_ledBeamPermit.text->setToolTip(kBeamPermitLedTooltip);
        m_ledBeamPermit.dot->setToolTip(kBeamPermitLedTooltip);
    }
}

void MainWindow::resetAllLeds() {
    setLedColor(m_ledAuto,          false);
    setLedColor(m_ledManual,        false);
    setLedColor(m_ledSpeed,         false);
    setLedColor(m_ledHoming,        false);
    setLedColor(m_ledPosition,      false);
    setLedColor(m_ledMotor,         false);
    setLedColor(m_ledHomingDone,    false);
    setLedColor(m_ledEstop,         false);
    setLedColor(m_ledSafety,        false);
    setLedColor(m_ledAir,           false);
    setLedColor(m_ledBrakes,        false);
    setLedColor(m_ledMotionInhibit, false);
    setLedColor(m_ledBeamPermit,    false);
    m_ledBeamPermit.text->setText(kBeamPermitLedLabel);
    if (m_gauge) {
        m_gauge->setAngle(0.0);
        m_gauge->setTargetAngle(std::numeric_limits<double>::quiet_NaN());
    }
}

void MainWindow::updateAngleDisplay(const GantryStatus &s) {
    if (!m_gauge) return;
    double ang = ModbusFloat::angleForDisplay(s.abs01AngleDeg);
    if (ang == 0.0 && !ModbusFloat::isDisplayableAngle(s.abs01AngleDeg))
        ang = ModbusFloat::angleForDisplay(s.servoAngleDeg);
    m_gauge->setAngle(ang);
}

void MainWindow::updateParameterDisplay(const GantryStatus &s) {
    auto setVal = [](QLabel *lb, const QString &text) {
        if (!lb) return;
        lb->setText(lb->fontMetrics().elidedText(
            text, Qt::ElideRight, lb->width() > 0 ? lb->width() : kParamValueColWidth));
    };
    if (!m_labelServoAngle) return;
    setVal(m_labelServoAngle,        ModbusFloat::formatAngleDegUi(s.servoAngleDeg));
    setVal(m_labelAbs01Angle,        ModbusFloat::formatAngleDegUi(s.abs01AngleDeg));
    setVal(m_labelAbs02Angle,        ModbusFloat::formatAngleDegUi(s.abs02AngleDeg));
    setVal(m_labelCurrentSpeed,      ModbusFloat::formatScalarUi(s.servoCurrentSpeed, 2));
    setVal(m_labelPositionSetpoint,  ModbusFloat::formatAngleDegUi(s.positionSetpoint));
    setVal(m_labelSpeedSetpoint,     ModbusFloat::formatScalarUi(s.speedSetpoint, 2));
    setVal(m_labelServo1Torque,      ModbusFloat::formatScalarUi(s.servo1Torque, 2));
    setVal(m_labelServo2Torque,      ModbusFloat::formatScalarUi(s.servo2Torque, 2));
    setVal(m_labelSlip1,             ModbusFloat::formatScalarUi(s.axialSlip1, 2));
    setVal(m_labelSlip2,             ModbusFloat::formatScalarUi(s.axialSlip2, 2));
    setVal(m_labelShearForce,        ModbusFloat::formatScalarUi(s.shearForce, 2));
    setVal(m_labelEstopOvershoot,    ModbusFloat::formatAngleDegUi(s.estopOvershoot));
}

void MainWindow::updateChart(double angle) {
    if (!m_angleSeries) return;
    const double y = ModbusFloat::sanitizeAbsAngle(angle);
    QDateTime now = QDateTime::currentDateTime();
    m_angleSeries->append(now.toMSecsSinceEpoch(), y);
    // 曲线数据上限裁剪，避免长时间运行内存泄漏
    while (m_angleSeries->count() > kMaxChartPoints)
        m_angleSeries->remove(0);
    if (ModbusFloat::isDisplayableAngle(m_currentStatus.positionSetpoint)) {
        m_targetScatter->clear();
        m_targetScatter->append(now.toMSecsSinceEpoch(),
                                *m_currentStatus.positionSetpoint);
    } else {
        m_targetScatter->clear();
    }
    m_timeAxis->setRange(now.addSecs(-chartWindowSeconds()), now);
    m_angleAxis->setRange(ModbusAddr::CHART_ANGLE_AXIS_MIN,
                          ModbusAddr::CHART_ANGLE_AXIS_MAX);
}

// ============================================================================
// 槽函数
// ============================================================================

void MainWindow::onCommandResponse(const TcsResponse &r) {
    if (!r.ok && !r.error.isEmpty())
        onLogMessage(QString("TCS 错误 [%1]: %2").arg(r.cmd, r.error));
    if (r.cmd == "ping")
        onLogMessage(r.pong ? "Ping: pong=true" : "Ping 错误: " + r.error);
    else if (r.cmd == "snapshot")
        onLogMessage(QString("快照: ok=%1 beam_permit=%2 %3")
            .arg(r.ok).arg(r.beamPermit ? "是" : "否").arg(r.beamPermitReason));
    else if (r.cmd == "move")
        onLogMessage(QString("运动: %1 | %2 | target=%3°")
            .arg(r.motionComplete ? "完成" : "进行中/失败", r.motionDetail)
            .arg(r.targetDeg, 0, 'f', 2));
}

void MainWindow::onLogMessage(const QString &msg) { appendLogRow(msg); }

void MainWindow::onConnectionError(const QString &err) { onLogMessage("通信错误: " + err); }

void MainWindow::updateControlsForConnectionMode() {
    const bool connected = m_client.isConnected();

    if (m_motionModbusBlock) m_motionModbusBlock->setEnabled(connected && !m_motionBusy);
    if (m_btnAuto) m_btnAuto->setEnabled(connected);
    if (m_btnManual) m_btnManual->setEnabled(connected);
    if (m_btnHome) m_btnHome->setEnabled(connected && !m_motionBusy);
    if (m_btnReset) m_btnReset->setEnabled(connected);
    if (m_btnEstop) m_btnEstop->setEnabled(connected);
    if (m_btnBrakesClose) m_btnBrakesClose->setEnabled(connected);
    if (m_btnBrakesOpen) m_btnBrakesOpen->setEnabled(connected);
    if (m_btnEstopRecover) m_btnEstopRecover->setEnabled(connected);
    if (m_btnSelfTest) m_btnSelfTest->setEnabled(connected && !m_motionBusy);
    if (m_btnWorkflow) m_btnWorkflow->setEnabled(connected && !m_motionBusy);
    if (m_btnMove) m_btnMove->setEnabled(connected && !m_motionBusy);
}

void MainWindow::connectToPlc() {
    m_pollTimer.stop();
    QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u53C2\u6570\u9519\u8BEF"),
            QStringLiteral("\u4E3B\u673A\u5730\u5740\u4E0D\u80FD\u4E3A\u7A7A\u3002"));
        return;
    }
    bool ok = false;
    int port = m_portEdit->text().toInt(&ok);
    if (!ok || port <= 0 || port > 65535) {
        QMessageBox::warning(this, QStringLiteral("\u53C2\u6570\u9519\u8BEF"),
            QStringLiteral("\u7AEF\u53E3\u53F7\u65E0\u6548\uFF0C\u8BF7\u8F93\u5165 1~65535 \u4E4B\u95F4\u7684\u6574\u6570\u3002"));
        return;
    }
    m_client.connectToBackend(host, static_cast<quint16>(port));
    // \u8F6E\u8BE2\u5B9A\u65F6\u5668\u5728 connected \u4FE1\u53F7\u4E2D\u542F\u52A8\uFF0C\u907F\u514D\u8FDE\u63A5\u672A\u5B8C\u6210\u524D\u7A7A\u8F6E\u8BE2
    m_chartTimer.start();
}

void MainWindow::disconnectFromPlc() {
    m_pollTimer.stop();
    resetAllLeds();
    m_client.disconnect();
    updateControlsForConnectionMode();
}

void MainWindow::openBrakes() {
    QString angleInfo = m_currentStatus.abs01AngleDeg.has_value()
        ? QStringLiteral("\u5F53\u524D\u89D2\u5EA6: %1\u00B0").arg(*m_currentStatus.abs01AngleDeg, 0, 'f', 2)
        : QStringLiteral("\u5F53\u524D\u89D2\u5EA6: \u672A\u77E5");
    QString motionInfo = m_currentStatus.motorRunning
        ? QStringLiteral("\u7535\u673A\u8FD0\u884C\u4E2D!") : QStringLiteral("\u7535\u673A\u672A\u8FD0\u884C");
    if (QMessageBox::warning(this, QStringLiteral("\u786E\u8BA4"),
            QStringLiteral("\u786E\u5B9A\u8981\u6253\u5F00\u5168\u90E8\u5236\u52A8\u5668\u5417\uFF1F\u4EC5\u7528\u4E8E\u7EF4\u62A4/\u8C03\u8BD5\u3002\n%1\n%2")
                .arg(angleInfo, motionInfo),
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.openAllBrakes();
}

void MainWindow::recoverEstop() {
    // 显示当前急停状态并提供恢复操作
    QString status;
    if (m_currentStatus.plcEstop) status += QStringLiteral("PLC柜急停(00033) ");
    if (m_currentStatus.estop1)   status += QStringLiteral("急停1(00034) ");
    if (m_currentStatus.estop2)   status += QStringLiteral("急停2(00035) ");
    if (m_currentStatus.estop3)   status += QStringLiteral("急停3(00036) ");
    if (status.isEmpty()) status = QStringLiteral("无急停触发");

    QString msg = QStringLiteral("当前急停状态: %1\n\n"
                   "急停2恢复: 请先松开旋转急停2，再点击「急停2恢复」。\n"
                   "其它急停通道: 请先松开对应物理急停按钮，再点击「故障复位」。")
                   .arg(status);

    QMessageBox dlg(this);
    dlg.setWindowTitle(QStringLiteral("急停恢复"));
    dlg.setText(msg);
    auto *btnEstop2 = dlg.addButton(QStringLiteral("急停2恢复"), QMessageBox::ActionRole);
    auto *btnReset = dlg.addButton(QStringLiteral("故障复位"), QMessageBox::ActionRole);
    dlg.addButton(QMessageBox::Cancel);
    dlg.exec();

    if (dlg.clickedButton() == btnEstop2)
        m_client.recoverEstop(2);
    else if (dlg.clickedButton() == btnReset)
        m_client.resetFault();
}

void MainWindow::startHoming() {
    if (m_motionBusy) return;
    m_motionBusy = true;
    setMotionButtonsEnabled(false);
    m_client.startHoming();
}

void MainWindow::emergencyStop() {
    if (QMessageBox::warning(this, "确认急停",
            "确定要触发紧急停止吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.emergencyStop();
}

void MainWindow::jogFwd() {
    if (m_motionBusy) return;
    m_motionBusy = true;
    setMotionButtonsEnabled(false);
    m_client.manualJog(true, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
}
void MainWindow::jogRev() {
    if (m_motionBusy) return;
    m_motionBusy = true;
    setMotionButtonsEnabled(false);
    m_client.manualJog(false, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
}
void MainWindow::moveToPosition() {
    if (m_motionBusy) return;
    m_motionBusy = true;
    setMotionButtonsEnabled(false);
    m_client.moveToPosition(m_targetAngleSpin->value(), m_targetSpeedSpin->value(),
                           m_timeoutSpin ? m_timeoutSpin->value() : 300.0);
}

// ============================================================================
// 辅助方法
// ============================================================================

void MainWindow::setMotionButtonsEnabled(bool enabled) {
    updateControlsForConnectionMode();
}

int MainWindow::chartWindowSeconds() const {
    if (!m_chartWindowCombo) return 30;
    switch (m_chartWindowCombo->currentIndex()) {
    case 1:  return 60;
    case 2:  return 120;
    case 3:  return 300;
    default: return 30;
    }
}

void MainWindow::runSelfTest() {
    if (m_motionBusy) return;
    m_client.runSelfTest();
}

void MainWindow::runWorkflowFull() {
    if (m_motionBusy) return;
    // 弹出对话框输入角度序列
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this,
        QStringLiteral("完整工作流"),
        QStringLiteral("输入目标角度序列（每行一个角度，单位°）："),
        QStringLiteral("0.0\n45.0\n90.0"),
        &ok);
    if (!ok || text.trimmed().isEmpty()) return;

    QList<double> angles;
    for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
        bool convOk = false;
        double val = line.trimmed().toDouble(&convOk);
        if (convOk && std::abs(val) <= 185.0)
            angles.append(val);
    }
    if (angles.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("输入错误"),
            QStringLiteral("未解析到有效角度值。"));
        return;
    }
    m_motionBusy = true;
    setMotionButtonsEnabled(false);
    m_client.runWorkflowFull(angles);
}

void MainWindow::exportChartPng() {
    if (!m_chart) return;
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出曲线截图"),
        QStringLiteral("gantry_chart.png"),
        QStringLiteral("PNG (*.png);;All Files (*)"));
    if (path.isEmpty()) return;
    QPixmap pix = m_chartView->grab();
    pix.save(path, "PNG");
    onLogMessage(QStringLiteral("曲线截图已保存: %1").arg(path));
}

void MainWindow::exportChartCsv() {
    if (!m_angleSeries) return;
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出曲线 CSV"),
        QStringLiteral("gantry_chart.csv"),
        QStringLiteral("CSV (*.csv);;All Files (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
            QStringLiteral("无法写入文件: %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("timestamp_ms,angle_deg\n");
    for (int i = 0; i < m_angleSeries->count(); ++i) {
        const auto &pt = m_angleSeries->at(i);
        out << QStringLiteral("%1,%2\n").arg(pt.x(), 0, 'f', 0).arg(pt.y(), 0, 'f', 4);
    }
    file.close();
    onLogMessage(QStringLiteral("曲线 CSV 已保存: %1 (%2 点)")
        .arg(path).arg(m_angleSeries->count()));
}
