#include "main_window.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr int kLayoutSpacing = 8;
constexpr int kGroupMargin = 10;
constexpr int kBtnMinWidth = 104;
constexpr int kTitleBarHeight = 48;
}

// ============================================================================
// 全局样式表: 工业暗色主题
// ============================================================================
static const char *kStyle = R"(
QMainWindow { background-color: #1a1a20; }
QGroupBox {
    font-weight: bold; color: #c0c0d0;
    border: 1px solid #3a3a45; border-radius: 6px;
    margin-top: 18px; padding-top: 14px;
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
    padding: 6px 12px; min-height: 26px;
}
QPushButton:hover { background-color: #3d3d4a; border-color: #6a6a78; }
QPushButton:pressed { background-color: #2a2a32; }
QPushButton#btnEstop {
    background-color: #333340; border: 1px solid #c04040;
    color: #ff9090; font-weight: bold;
}
QPushButton#btnEstop:hover { background-color: #3d3d4a; }
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

void MainWindow::applyPanelLayout(QBoxLayout *layout) {
    if (!layout) return;
    layout->setSpacing(kLayoutSpacing);
    layout->setContentsMargins(kGroupMargin, kGroupMargin, kGroupMargin, kGroupMargin);
}

void MainWindow::applyGroupLayout(QGroupBox *group) {
    if (!group || !group->layout()) return;
    group->layout()->setSpacing(kLayoutSpacing);
    group->layout()->setContentsMargins(kGroupMargin, kGroupMargin + 4,
                                        kGroupMargin, kGroupMargin);
}

void MainWindow::styleUniformButtons(const QList<QPushButton *> &buttons, int minWidth) {
    for (auto *btn : buttons) {
        if (!btn) continue;
        btn->setMinimumWidth(minWidth);
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
        m_connStatusLabel->setText(m_client.isTcsMode() ? "已连接 (TCS)" : "已连接 (Modbus)");
        m_connStatusLabel->setStyleSheet("color:#30d050; font-weight:bold;");
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

    // 2~4. 上部：左状态 + 中参数(含仪表) + 右控制
    auto *upper = new QHBoxLayout;
    upper->setSpacing(kLayoutSpacing);
    upper->addWidget(buildStatusPanel(), 2);
    upper->addWidget(buildParametersPanel(), 3);
    upper->addWidget(buildRightPanel(), 1);
    root->addLayout(upper, 3);

    // 5. 底部：曲线 | 日志（左右平分）
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(kLayoutSpacing);
    bottom->addWidget(buildChartPanel(), 1);
    bottom->addWidget(buildLogPanel(), 1);
    root->addLayout(bottom, 2);
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

    auto *sub = new QLabel("Proton Gantry Modbus / TCS 上位机");
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
    item.dot->setFixedSize(10, 10);
    item.dot->setStyleSheet(
        "background-color:#555; border-radius:5px;"
        "min-width:10px; min-height:10px;");
    item.text = new QLabel(label);
    item.text->setStyleSheet("color:#b0b0b8; font-size:9pt;");
    return item;
}

void MainWindow::addLedToGrid(QGridLayout *grid, int row, int col,
                              LedItem &item, const QString &label) {
    item = makeLed(label);
    auto *cell = new QWidget;
    auto *hl = new QHBoxLayout(cell);
    hl->setContentsMargins(2, 2, 4, 2);
    hl->setSpacing(6);
    hl->addWidget(item.dot, 0, Qt::AlignVCenter);
    hl->addWidget(item.text, 1, Qt::AlignVCenter);
    grid->addWidget(cell, row, col);
}

void MainWindow::setLedColor(LedItem &led, bool on, bool running) {
    if (running)
        led.dot->setStyleSheet(
            "background-color:#e0b020; border-radius:5px;"
            "min-width:10px; min-height:10px;");
    else if (on)
        led.dot->setStyleSheet(
            "background-color:#30d050; border-radius:5px;"
            "min-width:10px; min-height:10px;");
    else
        led.dot->setStyleSheet(
            "background-color:#e04040; border-radius:5px;"
            "min-width:10px; min-height:10px;");
    led.text->setStyleSheet(
        on ? "color:#e0e0e8; font-size:9pt;" : "color:#b0b0b8; font-size:9pt;");
}

// ============================================================================
// 2. 左上方 — 系统状态（2 列网格，灯在左）
// ============================================================================

QWidget *MainWindow::buildStatusPanel() {
    auto *gb = new QGroupBox("系统状态");
    auto *grid = new QGridLayout(gb);
    grid->setSpacing(4);
    grid->setContentsMargins(kGroupMargin, kGroupMargin + 6, kGroupMargin, kGroupMargin);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    addLedToGrid(grid, 0, 0, m_ledAuto,          "自动模式");
    addLedToGrid(grid, 0, 1, m_ledManual,        "手动模式");
    addLedToGrid(grid, 1, 0, m_ledSpeed,         "速度模式");
    addLedToGrid(grid, 1, 1, m_ledHoming,        "寻零运行");
    addLedToGrid(grid, 2, 0, m_ledPosition,      "位置运行");
    addLedToGrid(grid, 2, 1, m_ledMotor,         "电机运行");
    addLedToGrid(grid, 3, 0, m_ledHomingDone,    "寻零完成");
    addLedToGrid(grid, 3, 1, m_ledEstop,         "急停正常");
    addLedToGrid(grid, 4, 0, m_ledSafety,        "安全继电器");
    addLedToGrid(grid, 4, 1, m_ledAir,           "气压正常");
    addLedToGrid(grid, 5, 0, m_ledBrakes,        "制动器关闭");
    addLedToGrid(grid, 5, 1, m_ledMotionInhibit, "运动允许");
    addLedToGrid(grid, 6, 0, m_ledBeamPermit,    "可出束");

    gb->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    return gb;
}

// ============================================================================
// 3. 中上方 — 仪表 + 实时参数（两列表格：名左对齐、值右对齐）
// ============================================================================

QWidget *MainWindow::buildParametersPanel() {
    auto *wrap = new QWidget;
    auto *h = new QHBoxLayout(wrap);
    applyPanelLayout(h);

    m_gauge = new GaugeWidget;
    m_gauge->setTitle("旋转机架角度");
    m_gauge->setMinimumSize(220, 220);
    m_gauge->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    h->addWidget(m_gauge, 2);

    auto *gb = new QGroupBox("实时参数");
    auto *grid = new QGridLayout(gb);
    grid->setSpacing(6);
    grid->setContentsMargins(kGroupMargin, kGroupMargin + 6, kGroupMargin, kGroupMargin);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 1);

    auto addParam = [&](int row, const QString &name, QLabel *&valueOut) {
        auto *nameLb = new QLabel(name);
        nameLb->setProperty("class", "paramName");
        nameLb->setStyleSheet("color:#888898; font-size:9pt;");
        nameLb->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        valueOut = new QLabel("—");
        valueOut->setProperty("class", "paramValue");
        valueOut->setStyleSheet("color:#e8e8f0; font-size:10pt; font-weight:bold;");
        valueOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        grid->addWidget(nameLb, row, 0);
        grid->addWidget(valueOut, row, 1);
    };

    addParam(0,  "伺服角度",    m_labelServoAngle);
    addParam(1,  "ABS_01角度",  m_labelAbs01Angle);
    addParam(2,  "ABS_02角度",  m_labelAbs02Angle);
    addParam(3,  "当前速度",    m_labelCurrentSpeed);
    addParam(4,  "位置给定",    m_labelPositionSetpoint);
    addParam(5,  "速度给定",    m_labelSpeedSetpoint);
    addParam(6,  "伺服1扭矩",   m_labelServo1Torque);
    addParam(7,  "伺服2扭矩",   m_labelServo2Torque);
    addParam(8,  "串动1",       m_labelSlip1);
    addParam(9,  "串动2",       m_labelSlip2);
    addParam(10, "剪切力",      m_labelShearForce);
    addParam(11, "急停过冲",    m_labelEstopOvershoot);

    h->addWidget(gb, 3);
    wrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return wrap;
}

// ============================================================================
// 4. 右侧 — 连接 + 运动 + 安全
// ============================================================================

QWidget *MainWindow::buildRightPanel() {
    auto *wrap = new QWidget;
    wrap->setMinimumWidth(300);
    wrap->setMaximumWidth(380);
    auto *v = new QVBoxLayout(wrap);
    applyPanelLayout(v);

    v->addWidget(buildConnectionPanel());
    v->addWidget(buildMotionPanel(), 1);
    v->addWidget(buildSafetyPanel());

    return wrap;
}

QWidget *MainWindow::buildConnectionPanel() {
    auto *gb = new QGroupBox("连接设置");
    auto *g = new QGridLayout(gb);
    g->setSpacing(kLayoutSpacing);
    g->setContentsMargins(kGroupMargin, kGroupMargin + 6, kGroupMargin, kGroupMargin);
    g->setColumnStretch(1, 1);

    g->addWidget(new QLabel("主机"), 0, 0);
    m_hostEdit = new QLineEdit("192.168.10.1");
    g->addWidget(m_hostEdit, 0, 1);

    g->addWidget(new QLabel("端口"), 1, 0);
    m_portEdit = new QLineEdit("510");
    g->addWidget(m_portEdit, 1, 1);

    g->addWidget(new QLabel("协议"), 2, 0);
    m_connModeCombo = new QComboBox;
    m_connModeCombo->addItem("Modbus PLC", 0);
    m_connModeCombo->addItem("TCS JSON", 1);
    connect(m_connModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onConnModeChanged);
    g->addWidget(m_connModeCombo, 2, 1);

    auto *btnC = new QPushButton("连接");
    btnC->setObjectName("btnConnect");
    connect(btnC, &QPushButton::clicked, this, &MainWindow::connectToPlc);

    auto *btnD = new QPushButton("断开");
    btnD->setObjectName("btnDisconnect");
    connect(btnD, &QPushButton::clicked, this, &MainWindow::disconnectFromPlc);

    m_btnTcsSnapshot = new QPushButton("快照");
    m_btnTcsSnapshot->setToolTip("TCS 模式: 发送 snapshot 刷新状态");
    connect(m_btnTcsSnapshot, &QPushButton::clicked, this, &MainWindow::requestTcsSnapshot);

    m_btnTcsPing = new QPushButton("Ping");
    m_btnTcsPing->setToolTip("TCS 模式: 连通性检测");
    connect(m_btnTcsPing, &QPushButton::clicked, this, &MainWindow::sendTcsPing);

    styleUniformButtons({btnC, btnD, m_btnTcsSnapshot, m_btnTcsPing});
    g->addWidget(btnC, 3, 0);
    g->addWidget(btnD, 3, 1);
    g->addWidget(m_btnTcsSnapshot, 4, 0);
    g->addWidget(m_btnTcsPing, 4, 1);

    auto *statusRow = new QWidget;
    auto *sl = new QHBoxLayout(statusRow);
    sl->setContentsMargins(0, 4, 0, 0);
    sl->setSpacing(6);
    m_connStatusLamp = new QLabel;
    m_connStatusLamp->setFixedSize(14, 14);
    m_connStatusLamp->setStyleSheet(
        "background-color:#d04040; border-radius:7px;");
    m_connStatusLabel = new QLabel("已断开");
    m_connStatusLabel->setStyleSheet("color:#d04040; font-weight:bold;");
    sl->addWidget(m_connStatusLamp);
    sl->addWidget(m_connStatusLabel);
    sl->addStretch();
    g->addWidget(statusRow, 5, 0, 1, 2);

    return gb;
}

QWidget *MainWindow::buildMotionPanel() {
    auto *outer = new QGroupBox("运动控制");
    auto *v = new QVBoxLayout(outer);
    applyGroupLayout(outer);

    // --- 模式 ---
    m_modeGroup = new QGroupBox("模式");
    auto *modeG = new QGridLayout(m_modeGroup);
    modeG->setSpacing(kLayoutSpacing);
    modeG->setContentsMargins(8, 8, 8, 8);

    m_btnAuto = new QPushButton("自动模式");
    connect(m_btnAuto, &QPushButton::clicked, this, &MainWindow::setAutoMode);
    m_btnManual = new QPushButton("手动模式");
    connect(m_btnManual, &QPushButton::clicked, this, &MainWindow::setManualMode);
    m_btnHome = new QPushButton("寻零");
    connect(m_btnHome, &QPushButton::clicked, this, &MainWindow::startHoming);
    m_btnReset = new QPushButton("复位故障");
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::resetFault);

    styleUniformButtons({m_btnAuto, m_btnManual, m_btnHome, m_btnReset});
    modeG->addWidget(m_btnAuto,   0, 0);
    modeG->addWidget(m_btnManual, 0, 1);
    modeG->addWidget(m_btnHome,   1, 0);
    modeG->addWidget(m_btnReset,  1, 1);
    v->addWidget(m_modeGroup);

    // --- 点动 ---
    m_jogGroup = new QGroupBox("点动");
    auto *jogG = new QGridLayout(m_jogGroup);
    jogG->setSpacing(kLayoutSpacing);
    jogG->setContentsMargins(8, 8, 8, 8);
    jogG->setColumnStretch(1, 1);

    jogG->addWidget(new QLabel("速度"), 0, 0);
    m_jogSpeedSpin = new QDoubleSpinBox;
    m_jogSpeedSpin->setRange(0.1, 20.0);
    m_jogSpeedSpin->setValue(3.0);
    m_jogSpeedSpin->setSuffix(" °/s");
    m_jogSpeedSpin->setDecimals(1);
    jogG->addWidget(m_jogSpeedSpin, 0, 1);

    jogG->addWidget(new QLabel("时长"), 1, 0);
    m_jogSecondsSpin = new QDoubleSpinBox;
    m_jogSecondsSpin->setRange(0.1, 60.0);
    m_jogSecondsSpin->setValue(1.0);
    m_jogSecondsSpin->setSuffix(" s");
    m_jogSecondsSpin->setDecimals(1);
    jogG->addWidget(m_jogSecondsSpin, 1, 1);

    auto *btnFwd = new QPushButton("正转");
    auto *btnRev = new QPushButton("反转");
    auto *btnStop = new QPushButton("停止");
    connect(btnFwd, &QPushButton::clicked, this, &MainWindow::jogFwd);
    connect(btnRev, &QPushButton::clicked, this, &MainWindow::jogRev);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopManual);
    styleUniformButtons({btnFwd, btnRev, btnStop});
    jogG->addWidget(btnFwd,  2, 0);
    jogG->addWidget(btnRev,  2, 1);
    jogG->addWidget(btnStop, 3, 0, 1, 2);
    v->addWidget(m_jogGroup);

    // --- 定位 ---
    auto *posGb = new QGroupBox("定位");
    auto *posG = new QGridLayout(posGb);
    posG->setSpacing(kLayoutSpacing);
    posG->setContentsMargins(8, 8, 8, 8);
    posG->setColumnStretch(1, 1);

    posG->addWidget(new QLabel("目标角度"), 0, 0);
    m_targetAngleSpin = new QDoubleSpinBox;
    m_targetAngleSpin->setRange(-185.0, 185.0);
    m_targetAngleSpin->setValue(0.0);
    m_targetAngleSpin->setSuffix(" °");
    m_targetAngleSpin->setDecimals(2);
    posG->addWidget(m_targetAngleSpin, 0, 1);

    posG->addWidget(new QLabel("速度"), 1, 0);
    m_targetSpeedSpin = new QDoubleSpinBox;
    m_targetSpeedSpin->setRange(0.1, 20.0);
    m_targetSpeedSpin->setValue(3.0);
    m_targetSpeedSpin->setSuffix(" °/s");
    m_targetSpeedSpin->setDecimals(1);
    posG->addWidget(m_targetSpeedSpin, 1, 1);

    auto *btnMove = new QPushButton("执行定位");
    btnMove->setObjectName("btnMove");
    connect(btnMove, &QPushButton::clicked, this, &MainWindow::moveToPosition);
    styleUniformButtons({btnMove});
    posG->addWidget(btnMove, 2, 0, 1, 2);
    v->addWidget(posGb);

    updateControlsForConnectionMode();
    return outer;
}

QWidget *MainWindow::buildSafetyPanel() {
    auto *outer = new QGroupBox("安全控制");
    auto *v = new QVBoxLayout(outer);
    applyGroupLayout(outer);

    auto *estopGb = new QGroupBox("紧急停止");
    estopGb->setObjectName("gbEstop");
    auto *estopL = new QVBoxLayout(estopGb);
    estopL->setContentsMargins(8, 8, 8, 8);

    m_btnEstop = new QPushButton("触发紧急停止");
    m_btnEstop->setObjectName("btnEstop");
    connect(m_btnEstop, &QPushButton::clicked, this, &MainWindow::emergencyStop);
    styleUniformButtons({m_btnEstop});
    estopL->addWidget(m_btnEstop);
    v->addWidget(estopGb);

    auto *opsGb = new QGroupBox("制动与恢复");
    auto *opsG = new QGridLayout(opsGb);
    opsG->setSpacing(kLayoutSpacing);
    opsG->setContentsMargins(8, 8, 8, 8);

    m_btnBrakesClose = new QPushButton("关闭全部制动器");
    connect(m_btnBrakesClose, &QPushButton::clicked, this, &MainWindow::closeBrakes);
    m_btnBrakesOpen = new QPushButton("打开全部制动器");
    connect(m_btnBrakesOpen, &QPushButton::clicked, this, &MainWindow::openBrakes);
    m_btnEstop2Recover = new QPushButton("急停2恢复");
    m_btnEstop2Recover->setToolTip("松开急停2后发送故障复位脉冲");
    connect(m_btnEstop2Recover, &QPushButton::clicked, this, &MainWindow::recoverEstop2);

    styleUniformButtons({m_btnBrakesClose, m_btnBrakesOpen, m_btnEstop2Recover});
    opsG->addWidget(m_btnBrakesClose,   0, 0, 1, 2);
    opsG->addWidget(m_btnBrakesOpen,    1, 0, 1, 2);
    opsG->addWidget(m_btnEstop2Recover, 2, 0, 1, 2);
    v->addWidget(opsGb);

    return outer;
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
    m_angleAxis->setRange(-185, 185);
    m_angleAxis->setLabelsColor(QColor(160, 160, 170));
    m_angleAxis->setGridLineColor(QColor(60, 60, 70));
    m_chart->addAxis(m_angleAxis, Qt::AlignLeft);
    m_angleSeries->attachAxis(m_angleAxis);
    m_targetScatter->attachAxis(m_angleAxis);

    auto *cv = new QChartView(m_chart);
    cv->setRenderHint(QPainter::Antialiasing);
    cv->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    l->addWidget(cv);

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
    btnBar->addStretch();
    auto *btnCl = new QPushButton("清空");
    btnCl->setFixedWidth(kBtnMinWidth);
    connect(btnCl, &QPushButton::clicked, this, [this]() { m_logTable->setRowCount(0); });
    btnBar->addWidget(btnCl);
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
    updateChart(s.abs01AngleDeg.value_or(0.0));
    if (s.positionSetpoint.has_value())
        m_gauge->setTargetAngle(*s.positionSetpoint);
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
    m_ledBeamPermit.text->setText(bp ? "可出束" : QString("可出束(%1)").arg(bpr));
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
    m_ledBeamPermit.text->setText("可出束");
    if (m_gauge) {
        m_gauge->setAngle(0.0);
        m_gauge->setTargetAngle(std::numeric_limits<double>::quiet_NaN());
    }
}

void MainWindow::updateAngleDisplay(const GantryStatus &s) {
    if (m_gauge)
        m_gauge->setAngle(s.abs01AngleDeg.value_or(s.servoAngleDeg.value_or(0.0)));
}

void MainWindow::updateParameterDisplay(const GantryStatus &s) {
    auto f = [](auto v, int d = 3) {
        return v.has_value() ? QString::number(*v, 'f', d) : "—";
    };
    auto fd = [](auto v) {
        return v.has_value() ? QString::number(*v, 'f', 3) + "°" : "—";
    };
    if (!m_labelServoAngle) return;
    m_labelServoAngle->setText(fd(s.servoAngleDeg));
    m_labelAbs01Angle->setText(fd(s.abs01AngleDeg));
    m_labelAbs02Angle->setText(fd(s.abs02AngleDeg));
    m_labelCurrentSpeed->setText(f(s.servoCurrentSpeed, 2));
    m_labelPositionSetpoint->setText(fd(s.positionSetpoint));
    m_labelSpeedSetpoint->setText(f(s.speedSetpoint, 2));
    m_labelServo1Torque->setText(f(s.servo1Torque, 2));
    m_labelServo2Torque->setText(f(s.servo2Torque, 2));
    m_labelSlip1->setText(f(s.axialSlip1, 3));
    m_labelSlip2->setText(f(s.axialSlip2, 3));
    m_labelShearForce->setText(f(s.shearForce, 2));
    m_labelEstopOvershoot->setText(fd(s.estopOvershoot));
}

void MainWindow::updateChart(double angle) {
    if (!m_angleSeries) return;
    QDateTime now = QDateTime::currentDateTime();
    m_angleSeries->append(now.toMSecsSinceEpoch(), angle);
    if (m_currentStatus.positionSetpoint.has_value()) {
        m_targetScatter->clear();
        m_targetScatter->append(now.toMSecsSinceEpoch(), *m_currentStatus.positionSetpoint);
    }
    m_timeAxis->setRange(now.addSecs(-30), now);
    if (m_angleSeries->count() > 50) {
        double lo = 1e9, hi = -1e9;
        for (int i = std::max(0, m_angleSeries->count() - 50); i < m_angleSeries->count(); ++i) {
            double y = m_angleSeries->at(i).y();
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
        double m = std::max(5.0, (hi - lo) * 0.2);
        m_angleAxis->setRange(lo - m, hi + m);
    }
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

void MainWindow::onConnModeChanged(int index) {
    if (index == 0) {
        if (m_hostEdit->text() == "127.0.0.1") m_hostEdit->setText("192.168.10.1");
        if (m_portEdit->text() == "5510") m_portEdit->setText("510");
    } else {
        if (m_hostEdit->text() == "192.168.10.1") m_hostEdit->setText("127.0.0.1");
        if (m_portEdit->text() == "510") m_portEdit->setText("5510");
    }
    updateControlsForConnectionMode();
}

void MainWindow::updateControlsForConnectionMode() {
    const bool tcs = m_connModeCombo && m_connModeCombo->currentData().toInt() == 1;
    const bool connected = m_client.isConnected();

    if (m_modeGroup) m_modeGroup->setEnabled(!tcs && connected);
    if (m_jogGroup) m_jogGroup->setEnabled(!tcs && connected);
    if (m_btnEstop) m_btnEstop->setEnabled(!tcs && connected);
    if (m_btnBrakesClose) m_btnBrakesClose->setEnabled(!tcs && connected);
    if (m_btnBrakesOpen) m_btnBrakesOpen->setEnabled(!tcs && connected);
    if (m_btnEstop2Recover) m_btnEstop2Recover->setEnabled(!tcs && connected);
    if (m_btnTcsSnapshot) {
        m_btnTcsSnapshot->setVisible(tcs);
        m_btnTcsSnapshot->setEnabled(tcs && connected);
    }
    if (m_btnTcsPing) {
        m_btnTcsPing->setVisible(tcs);
        m_btnTcsPing->setEnabled(tcs && connected);
    }
}

void MainWindow::connectToPlc() {
    QString host = m_hostEdit->text().trimmed();
    int port = m_portEdit->text().toInt();
    if (m_connModeCombo->currentData().toInt() == 0)
        m_client.connectToPlc(host, static_cast<quint16>(port));
    else
        m_client.connectToTcsService(host, static_cast<quint16>(port));
    updateControlsForConnectionMode();
    m_pollTimer.start(200);
    m_chartTimer.start();
}

void MainWindow::disconnectFromPlc() {
    m_pollTimer.stop();
    resetAllLeds();
    m_client.disconnect();
    updateControlsForConnectionMode();
}

void MainWindow::openBrakes() {
    if (QMessageBox::warning(this, "确认",
            "确定要打开全部制动器吗？仅用于维护/调试。",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.openAllBrakes();
}

void MainWindow::recoverEstop2() {
    if (QMessageBox::information(this, "急停2恢复",
            "请先松开旋转急停2 (DI 00035=0)，再确认发送故障复位脉冲。",
            QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
        m_client.recoverEstop2();
}

void MainWindow::emergencyStop() {
    if (QMessageBox::warning(this, "确认急停",
            "确定要触发紧急停止吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.emergencyStop();
}

void MainWindow::jogFwd() {
    m_client.manualJog(true, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
}
void MainWindow::jogRev() {
    m_client.manualJog(false, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
}
void MainWindow::moveToPosition() {
    m_client.moveToPosition(m_targetAngleSpin->value(), m_targetSpeedSpin->value(), 300.0);
}
