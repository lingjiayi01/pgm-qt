#include "main_window.h"
#include <QStringConverter>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QJsonDocument>
#include <QSet>
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

constexpr int kStatusPanelMaxWidth = 200;

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
    loadSettings();

    connect(&m_client, &GantryClient::connected, this, [this]() {
        updateConnectionStatusDisplay();
        m_pollTimer.start(pollIntervalMs());
        updateControlsForConnectionMode();
        onLogMessage("=== HTTP 已连接 ===");
        saveSettings();
    });
    connect(&m_client, &GantryClient::disconnected, this, [this]() {
        m_pollTimer.stop();
        updateConnectionStatusDisplay();
        resetAllLeds();
        updateMotionInhibitBar(GantryStatus{});
        updateControlsForConnectionMode();
        onLogMessage("=== 已断开 ===");
    });
    connect(&m_client, &GantryClient::plcConnectionChanged, this, [this](bool) {
        updateConnectionStatusDisplay();
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
        const int ms = pollIntervalMs();
        if (m_pollTimer.interval() != ms)
            m_pollTimer.setInterval(ms);
    });

    connect(&m_client, &GantryClient::motionFinished, this, [this]() {
        setMotionButtonsEnabled(true);
    });

    m_chartTimer.start();
}

MainWindow::~MainWindow() {
    saveSettings();
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

    m_motionInhibitBar = new QLabel(QStringLiteral("运动允许"));
    m_motionInhibitBar->setAlignment(Qt::AlignCenter);
    m_motionInhibitBar->setStyleSheet(
        "background-color:#1a3a1a; color:#80e080; font-weight:bold;"
        "padding:4px 8px; border-radius:4px; font-size:9pt;");
    root->addWidget(m_motionInhibitBar);

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
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    auto *v = new QVBoxLayout(inner);
    v->setSpacing(2);
    v->setContentsMargins(2, 4, 2, 4);

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
    addLedRow(m_ledZeroSwitch,    "零位开关");
    addLedRow(m_ledLimitPos185,   "+185°极限");
    addLedRow(m_ledLimitNeg185,   "-185°极限");
    addLedRow(m_ledAtPosLimit,    "正极限未触发");
    addLedRow(m_ledAtNegLimit,    "负极限未触发");
    addLedRow(m_ledAngleOut,      "角度未超范围");
    addLedRow(m_ledTargetOut,     "目标未超范围");
    addLedRow(m_ledServoFault,    "伺服无故障");
    for (int i = 0; i < 6; ++i)
        addLedRow(m_ledBrakeIndividual[i],
                  QStringLiteral("制动%1关闭").arg(i + 1));
    addLedRow(m_ledMotionInhibit, "运动允许");
    addLedRow(m_ledBeamPermit, kBeamPermitLedLabel, kBeamPermitLedTooltip);

    v->addStretch(1);
    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(gb);
    outer->setContentsMargins(4, 8, 4, 4);
    outer->addWidget(scroll);

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
    auto *btnC = new QPushButton("连接后端");
    btnC->setObjectName("btnConnect");
    connect(btnC, &QPushButton::clicked, this, &MainWindow::connectToBackend);
    auto *btnD = new QPushButton("断开");
    btnD->setObjectName("btnDisconnect");
    connect(btnD, &QPushButton::clicked, this, &MainWindow::disconnectFromBackend);
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

    auto *engRow = new QHBoxLayout;
    engRow->setSpacing(kLayoutSpacing);
    m_btnVerify = new QPushButton("点表巡检");
    connect(m_btnVerify, &QPushButton::clicked, this, &MainWindow::runPointTableVerify);
    m_btnDiscrete = new QPushButton("离散量");
    connect(m_btnDiscrete, &QPushButton::clicked, this, &MainWindow::showDiscreteMonitor);
    styleUniformButtons({m_btnVerify, m_btnDiscrete}, 0);
    engRow->addWidget(m_btnVerify, 1);
    engRow->addWidget(m_btnDiscrete, 1);
    v->addLayout(engRow);

    auto *pollRow = new QHBoxLayout;
    pollRow->setSpacing(kCompactSpacing);
    auto *pollLb = new QLabel("轮询");
    pollLb->setFixedWidth(36);
    m_pollIntervalSpin = new QSpinBox;
    m_pollIntervalSpin->setRange(300, 5000);
    m_pollIntervalSpin->setValue(1000);
    m_pollIntervalSpin->setSuffix(" ms");
    m_pollIntervalSpin->setSingleStep(100);
    connect(m_pollIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { saveSettings(); });
    pollRow->addWidget(pollLb);
    pollRow->addWidget(m_pollIntervalSpin, 1);
    v->addLayout(pollRow);
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

    // 高级选项（可折叠）
    m_advOptionsGroup = new QGroupBox("高级选项");
    m_advOptionsGroup->setCheckable(true);
    m_advOptionsGroup->setChecked(false);
    m_advOptionsContent = new QWidget;
    auto *advV = new QVBoxLayout(m_advOptionsContent);
    advV->setSpacing(kCompactSpacing);
    advV->setContentsMargins(0, 0, 0, 0);

    auto *tolRow = new QHBoxLayout;
    auto *tolLb = new QLabel("容差");
    tolLb->setFixedWidth(56);
    m_tolSpin = new QDoubleSpinBox;
    m_tolSpin->setRange(0.05, 5.0);
    m_tolSpin->setValue(0.5);
    m_tolSpin->setSuffix(" °");
    m_tolSpin->setDecimals(2);
    tolRow->addWidget(tolLb);
    tolRow->addWidget(m_tolSpin, 1);
    advV->addLayout(tolRow);

    auto *arrRow = new QHBoxLayout;
    auto *arrLb = new QLabel("到位模式");
    arrLb->setFixedWidth(56);
    m_arrivalModeCombo = new QComboBox;
    m_arrivalModeCombo->addItem("hybrid", "hybrid");
    m_arrivalModeCombo->addItem("strict_04", "strict_04");
    m_arrivalModeCombo->addItem("angle", "angle");
    arrRow->addWidget(arrLb);
    arrRow->addWidget(m_arrivalModeCombo, 1);
    advV->addLayout(arrRow);

    m_requireHomingCheck = new QCheckBox("要求寻零完成");
    m_requireHomingCheck->setChecked(true);
    m_autoModeCheck = new QCheckBox("自动切换模式");
    m_autoModeCheck->setChecked(true);
    advV->addWidget(m_requireHomingCheck);
    advV->addWidget(m_autoModeCheck);

    auto *graceRow = new QHBoxLayout;
    auto *graceLb = new QLabel("DI04宽限");
    graceLb->setFixedWidth(56);
    m_di04GraceSpin = new QDoubleSpinBox;
    m_di04GraceSpin->setRange(0.0, 60.0);
    m_di04GraceSpin->setValue(5.0);
    m_di04GraceSpin->setSuffix(" s");
    graceRow->addWidget(graceLb);
    graceRow->addWidget(m_di04GraceSpin, 1);
    advV->addLayout(graceRow);

    auto *platRow = new QHBoxLayout;
    auto *platLb = new QLabel("平台采样");
    platLb->setFixedWidth(56);
    m_plateauNSpin = new QSpinBox;
    m_plateauNSpin->setRange(1, 20);
    m_plateauNSpin->setValue(5);
    platRow->addWidget(platLb);
    platRow->addWidget(m_plateauNSpin, 1);
    advV->addLayout(platRow);

    auto *advOuter = new QVBoxLayout(m_advOptionsGroup);
    advOuter->setContentsMargins(4, 8, 4, 4);
    advOuter->addWidget(m_advOptionsContent);
    m_advOptionsContent->setVisible(false);
    connect(m_advOptionsGroup, &QGroupBox::toggled, m_advOptionsContent, &QWidget::setVisible);
    v->addWidget(m_advOptionsGroup);
    
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
    auto *btnLogCsv = new QPushButton("导出CSV");
    btnLogCsv->setFixedWidth(kBtnMinWidth + 16);
    connect(btnLogCsv, &QPushButton::clicked, this, &MainWindow::exportLogCsv);
    btnBar->addWidget(btnLogCsv, 0, Qt::AlignRight);
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
    updateMotionInhibitBar(s);
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
    if (e) {
        QStringList ch;
        if (s.plcEstop) ch << QStringLiteral("PLC(33)");
        if (s.estop1) ch << QStringLiteral("E1(34)");
        if (s.estop2) ch << QStringLiteral("E2(35)");
        if (s.estop3) ch << QStringLiteral("E3(36)");
        const QString tip = QStringLiteral("急停触发: %1").arg(ch.join(QStringLiteral("、")));
        m_ledEstop.text->setText(QStringLiteral("急停触发"));
        m_ledEstop.text->setToolTip(tip);
        m_ledEstop.dot->setToolTip(tip);
    } else {
        m_ledEstop.text->setText(QStringLiteral("急停正常"));
        m_ledEstop.text->setToolTip(QString());
        m_ledEstop.dot->setToolTip(QString());
    }
    setLedColor(m_ledSafety,        !s.safetyRelayNotReady);
    bool airOk = s.air1PressureOk && s.air2PressureOk && !s.air1Low && !s.air2Low;
    setLedColor(m_ledAir,           airOk);
    bool brakesClosed = s.brakesOpen.empty()
        || std::none_of(s.brakesOpen.begin(), s.brakesOpen.end(), [](bool o) { return o; });
    setLedColor(m_ledBrakes,        brakesClosed);
    setLedColor(m_ledZeroSwitch,    s.zeroSwitch);
    setLedColor(m_ledLimitPos185,   s.limitPos185Ok);
    setLedColor(m_ledLimitNeg185,   s.limitNeg185Ok);
    setLedColor(m_ledAtPosLimit,    !s.atPosLimit);
    setLedColor(m_ledAtNegLimit,    !s.atNegLimit);
    setLedColor(m_ledAngleOut,      !s.angleOutOfRange);
    setLedColor(m_ledTargetOut,     !s.targetAngleOutOfRange);
    setLedColor(m_ledServoFault,    !s.servoFaultActive());
    for (int i = 0; i < 6; ++i) {
        const bool open = i < (int)s.brakesOpen.size() && s.brakesOpen[i];
        setLedColor(m_ledBrakeIndividual[i], !open);
    }
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

void MainWindow::updateMotionInhibitBar(const GantryStatus &s) {
    if (!m_motionInhibitBar) return;
    if (!m_client.isConnected()) {
        m_motionInhibitBar->setVisible(false);
        return;
    }
    m_motionInhibitBar->setVisible(true);
    if (s.motionInhibitEffective()) {
        const QString reason = describeMotionInhibitReason(s);
        m_motionInhibitBar->setText(QStringLiteral("运动禁止: %1").arg(reason));
        m_motionInhibitBar->setStyleSheet(
            "background-color:#4a1a1a; color:#ff9090; font-weight:bold;"
            "padding:4px 8px; border-radius:4px; font-size:9pt;");
        m_motionInhibitBar->setToolTip(reason);
    } else {
        m_motionInhibitBar->setText(QStringLiteral("运动允许"));
        m_motionInhibitBar->setStyleSheet(
            "background-color:#1a3a1a; color:#80e080; font-weight:bold;"
            "padding:4px 8px; border-radius:4px; font-size:9pt;");
        m_motionInhibitBar->setToolTip(QString());
    }
}

void MainWindow::updateConnectionStatusDisplay() {
    if (!m_connStatusLamp || !m_connStatusLabel) return;
    if (!m_client.isConnected()) {
        m_connStatusLamp->setStyleSheet(
            "background-color:#d04040; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText(QStringLiteral("已断开"));
        m_connStatusLabel->setStyleSheet("color:#d04040; font-weight:bold;");
        return;
    }
    if (m_client.isPlcConnected()) {
        m_connStatusLamp->setStyleSheet(
            "background-color:#30d050; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText(QStringLiteral("HTTP 已连接 | PLC 已连接"));
        m_connStatusLabel->setStyleSheet("color:#30d050; font-weight:bold;");
    } else {
        m_connStatusLamp->setStyleSheet(
            "background-color:#d0a030; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText(QStringLiteral("HTTP 已连接 | PLC 未连接"));
        m_connStatusLabel->setStyleSheet("color:#e0c040; font-weight:bold;");
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
    setLedColor(m_ledZeroSwitch,    false);
    setLedColor(m_ledLimitPos185,   false);
    setLedColor(m_ledLimitNeg185,   false);
    setLedColor(m_ledAtPosLimit,    false);
    setLedColor(m_ledAtNegLimit,    false);
    setLedColor(m_ledAngleOut,      false);
    setLedColor(m_ledTargetOut,     false);
    setLedColor(m_ledServoFault,    false);
    for (int i = 0; i < 6; ++i)
        setLedColor(m_ledBrakeIndividual[i], false);
    setLedColor(m_ledMotionInhibit, false);
    setLedColor(m_ledBeamPermit,    false);
    m_ledEstop.text->setText(QStringLiteral("急停正常"));
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
    const QString cmd = r.cmd;
    if (!r.ok) {
        onLogMessage(QStringLiteral("命令失败 [%1]: %2 [%3]").arg(cmd, r.error, r.errorCode));
        showApiErrorPopup(r);
    }

    if (cmd == QStringLiteral("verify")) {
        if (r.ok && !r.rawData.isEmpty())
            showVerifyResultDialog(r.rawData);
        return;
    }

    if (cmd == QStringLiteral("self-test")) {
        if (r.ok && r.commandSuccess) {
            onLogMessage(QStringLiteral("上电自检: 通过"));
            QMessageBox::information(this, QStringLiteral("上电自检"),
                QStringLiteral("自检通过"));
        } else if (!r.ok || !r.commandSuccess) {
            QString body = r.failures.isEmpty()
                ? r.error
                : QStringLiteral("• %1").arg(r.failures.join(QStringLiteral("\n• ")));
            onLogMessage(QStringLiteral("上电自检: 失败 — %1").arg(body));
            QMessageBox::warning(this, QStringLiteral("上电自检失败"), body);
        }
        return;
    }

    if (cmd == QStringLiteral("full")) {
        const QString detail = r.motionDetail.isEmpty() ? r.error : r.motionDetail;
        if (r.ok && r.commandSuccess) {
            onLogMessage(QStringLiteral("完整工作流: 成功 — %1").arg(detail));
            QMessageBox::information(this, QStringLiteral("完整工作流"),
                detail.isEmpty() ? QStringLiteral("工作流完成") : detail);
        } else {
            onLogMessage(QStringLiteral("完整工作流: 失败 — %1").arg(detail));
            QMessageBox::warning(this, QStringLiteral("完整工作流失败"), detail);
        }
        return;
    }

    if (cmd == QStringLiteral("home")) {
        const QString detail = r.motionDetail.isEmpty() ? r.error : r.motionDetail;
        if (r.ok && r.commandSuccess)
            onLogMessage(QStringLiteral("寻零: 完成 — %1").arg(detail));
        else if (!r.ok)
            onLogMessage(QStringLiteral("寻零: 失败 — %1").arg(detail));
        else
            onLogMessage(QStringLiteral("寻零: %1").arg(detail));
        return;
    }

    if (cmd == QStringLiteral("jog")) {
        const QString detail = r.motionDetail.isEmpty() ? r.error : r.motionDetail;
        if (r.ok && r.commandSuccess)
            onLogMessage(QStringLiteral("点动: 完成 — %1").arg(detail));
        else if (!r.ok)
            onLogMessage(QStringLiteral("点动: 失败 — %1").arg(detail));
        return;
    }

    if (cmd == QStringLiteral("position")) {
        const QString detail = r.motionDetail.isEmpty() ? r.error : r.motionDetail;
        if (r.ok && r.commandSuccess)
            onLogMessage(QStringLiteral("定角: 完成 — %1 (目标 %2°)")
                .arg(detail).arg(r.targetDeg, 0, 'f', 2));
        else if (!r.ok)
            onLogMessage(QStringLiteral("定角: 失败 — %1").arg(detail));
        return;
    }

    if (cmd == QStringLiteral("ping"))
        onLogMessage(r.pong ? "Ping: pong=true" : "Ping 错误: " + r.error);
    else if (cmd == QStringLiteral("snapshot"))
        onLogMessage(QString("快照: ok=%1 beam_permit=%2 %3")
            .arg(r.ok).arg(r.beamPermit ? "是" : "否").arg(r.beamPermitReason));
    else if (!r.ok && !r.error.isEmpty())
        onLogMessage(QString("命令 [%1]: %2").arg(cmd, r.error));
}

void MainWindow::showApiErrorPopup(const TcsResponse &r) {
    static const QSet<QString> kPopupCodes = {
        QStringLiteral("BUSY"),
        QStringLiteral("SAFETY_INHIBIT"),
        QStringLiteral("MOTION_FAILED"),
        QStringLiteral("SELF_TEST_FAILED"),
        QStringLiteral("WORKFLOW_FAILED"),
        QStringLiteral("NOT_CONNECTED"),
    };
    if (!kPopupCodes.contains(r.errorCode)) return;
    if (r.cmd == QStringLiteral("self-test") || r.cmd == QStringLiteral("full"))
        return; // 已有专用弹窗
    QMessageBox::warning(this, QStringLiteral("操作被拒绝"),
        QStringLiteral("[%1] %2").arg(r.errorCode, r.error));
}

void MainWindow::onLogMessage(const QString &msg) { appendLogRow(msg); }

void MainWindow::onConnectionError(const QString &err) { onLogMessage("通信错误: " + err); }

void MainWindow::updateControlsForConnectionMode() {
    const bool connected = m_client.isConnected();
    const bool motionBusy = m_client.isMotionInProgress();

    if (m_motionModbusBlock) m_motionModbusBlock->setEnabled(connected && !motionBusy);
    if (m_btnAuto) m_btnAuto->setEnabled(connected);
    if (m_btnManual) m_btnManual->setEnabled(connected);
    if (m_btnHome) m_btnHome->setEnabled(connected && !motionBusy);
    if (m_btnReset) m_btnReset->setEnabled(connected);
    if (m_btnEstop) m_btnEstop->setEnabled(connected);
    if (m_btnBrakesClose) m_btnBrakesClose->setEnabled(connected);
    if (m_btnBrakesOpen) m_btnBrakesOpen->setEnabled(connected);
    if (m_btnEstopRecover) m_btnEstopRecover->setEnabled(connected);
    if (m_btnSelfTest) m_btnSelfTest->setEnabled(connected && !motionBusy);
    if (m_btnWorkflow) m_btnWorkflow->setEnabled(connected && !motionBusy);
    if (m_btnMove) m_btnMove->setEnabled(connected && !motionBusy);
    if (m_btnVerify) m_btnVerify->setEnabled(connected && !motionBusy);
    if (m_btnDiscrete) m_btnDiscrete->setEnabled(connected);
}

void MainWindow::connectToBackend() {
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
    saveSettings();
    m_client.connectToBackend(host, static_cast<quint16>(port));
    m_chartTimer.start();
}

void MainWindow::disconnectFromBackend() {
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
    if (m_client.isMotionInProgress()) return;
    m_client.startHoming();
    setMotionButtonsEnabled(false);
}

void MainWindow::emergencyStop() {
    if (QMessageBox::warning(this, "确认急停",
            "确定要触发紧急停止吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.emergencyStop();
}

void MainWindow::jogFwd() {
    if (m_client.isMotionInProgress()) return;
    m_client.manualJog(true, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
    setMotionButtonsEnabled(false);
}
void MainWindow::jogRev() {
    if (m_client.isMotionInProgress()) return;
    m_client.manualJog(false, m_jogSpeedSpin->value(), m_jogSecondsSpin->value());
    setMotionButtonsEnabled(false);
}
void MainWindow::moveToPosition() {
    if (m_client.isMotionInProgress()) return;
    m_client.moveToPosition(m_targetAngleSpin->value(), m_targetSpeedSpin->value(),
                           m_timeoutSpin ? m_timeoutSpin->value() : 300.0,
                           currentPositionOptions());
    setMotionButtonsEnabled(false);
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
    if (m_client.isMotionInProgress()) return;
    m_client.runSelfTest();
    setMotionButtonsEnabled(false);
}

void MainWindow::runWorkflowFull() {
    if (m_client.isMotionInProgress()) return;
    QList<double> angles;
    WorkflowFullOptions opts;
    if (!showWorkflowDialog(angles, opts)) return;
    m_client.runWorkflowFull(angles, opts);
    setMotionButtonsEnabled(false);
}

void MainWindow::runPointTableVerify() {
    if (!m_client.isConnected()) return;
    m_client.runPointTableVerify();
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

void MainWindow::exportLogCsv() {
    if (!m_logTable || m_logTable->rowCount() == 0) return;
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出日志 CSV"),
        QStringLiteral("gantry_log.csv"),
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
    out << QStringLiteral("date,time,detail\n");
    for (int r = 0; r < m_logTable->rowCount(); ++r) {
        out << QStringLiteral("%1,%2,%3\n")
               .arg(m_logTable->item(r, 0) ? m_logTable->item(r, 0)->text() : QString())
               .arg(m_logTable->item(r, 1) ? m_logTable->item(r, 1)->text() : QString())
               .arg(m_logTable->item(r, 2) ? m_logTable->item(r, 2)->text() : QString());
    }
    file.close();
    onLogMessage(QStringLiteral("日志 CSV 已保存: %1").arg(path));
}

int MainWindow::pollIntervalMs() const {
    const int base = m_pollIntervalSpin ? m_pollIntervalSpin->value() : 1000;
    if (m_client.isMotionInProgress()
        || m_currentStatus.homingRunning
        || m_currentStatus.positionModeRunning
        || m_currentStatus.speedModeRunning)
        return std::min(base, 500);
    return base;
}

MotionPositionOptions MainWindow::currentPositionOptions() const {
    MotionPositionOptions o;
    if (m_tolSpin) o.tol = m_tolSpin->value();
    if (m_arrivalModeCombo)
        o.arrivalMode = m_arrivalModeCombo->currentData().toString();
    if (m_requireHomingCheck) o.requireHoming = m_requireHomingCheck->isChecked();
    if (m_autoModeCheck) o.autoMode = m_autoModeCheck->isChecked();
    if (m_di04GraceSpin) o.di04Grace = m_di04GraceSpin->value();
    if (m_plateauNSpin) o.plateauN = m_plateauNSpin->value();
    return o;
}

void MainWindow::loadSettings() {
    QSettings s(QStringLiteral("PGM"), QStringLiteral("GantryHMI"));
    if (m_hostEdit) m_hostEdit->setText(s.value(QStringLiteral("host"), QStringLiteral("127.0.0.1")).toString());
    if (m_portEdit) m_portEdit->setText(s.value(QStringLiteral("port"), 8080).toString());
    if (m_pollIntervalSpin) m_pollIntervalSpin->setValue(s.value(QStringLiteral("poll_ms"), 1000).toInt());
    if (m_chartWindowCombo) m_chartWindowCombo->setCurrentIndex(s.value(QStringLiteral("chart_window"), 0).toInt());
    if (m_jogSpeedSpin) m_jogSpeedSpin->setValue(s.value(QStringLiteral("jog_speed"), 3.0).toDouble());
    if (m_targetSpeedSpin) m_targetSpeedSpin->setValue(s.value(QStringLiteral("target_speed"), 3.0).toDouble());
    if (m_timeoutSpin) m_timeoutSpin->setValue(s.value(QStringLiteral("timeout"), 300.0).toDouble());
    if (m_tolSpin) m_tolSpin->setValue(s.value(QStringLiteral("tol"), 0.5).toDouble());
    if (m_arrivalModeCombo) {
        const QString mode = s.value(QStringLiteral("arrival_mode"), QStringLiteral("hybrid")).toString();
        const int idx = m_arrivalModeCombo->findData(mode);
        if (idx >= 0) m_arrivalModeCombo->setCurrentIndex(idx);
    }
    if (m_requireHomingCheck) m_requireHomingCheck->setChecked(s.value(QStringLiteral("require_homing"), true).toBool());
    if (m_autoModeCheck) m_autoModeCheck->setChecked(s.value(QStringLiteral("auto_mode"), true).toBool());
    if (m_di04GraceSpin) m_di04GraceSpin->setValue(s.value(QStringLiteral("di04_grace"), 5.0).toDouble());
    if (m_plateauNSpin) m_plateauNSpin->setValue(s.value(QStringLiteral("plateau_n"), 5).toInt());
    if (m_advOptionsGroup) m_advOptionsGroup->setChecked(s.value(QStringLiteral("adv_expanded"), false).toBool());
    if (m_advOptionsGroup && m_advOptionsContent)
        m_advOptionsContent->setVisible(m_advOptionsGroup->isChecked());
}

void MainWindow::saveSettings() {
    QSettings s(QStringLiteral("PGM"), QStringLiteral("GantryHMI"));
    if (m_hostEdit) s.setValue(QStringLiteral("host"), m_hostEdit->text().trimmed());
    if (m_portEdit) s.setValue(QStringLiteral("port"), m_portEdit->text().trimmed());
    if (m_pollIntervalSpin) s.setValue(QStringLiteral("poll_ms"), m_pollIntervalSpin->value());
    if (m_chartWindowCombo) s.setValue(QStringLiteral("chart_window"), m_chartWindowCombo->currentIndex());
    if (m_jogSpeedSpin) s.setValue(QStringLiteral("jog_speed"), m_jogSpeedSpin->value());
    if (m_targetSpeedSpin) s.setValue(QStringLiteral("target_speed"), m_targetSpeedSpin->value());
    if (m_timeoutSpin) s.setValue(QStringLiteral("timeout"), m_timeoutSpin->value());
    if (m_tolSpin) s.setValue(QStringLiteral("tol"), m_tolSpin->value());
    if (m_arrivalModeCombo)
        s.setValue(QStringLiteral("arrival_mode"), m_arrivalModeCombo->currentData().toString());
    if (m_requireHomingCheck) s.setValue(QStringLiteral("require_homing"), m_requireHomingCheck->isChecked());
    if (m_autoModeCheck) s.setValue(QStringLiteral("auto_mode"), m_autoModeCheck->isChecked());
    if (m_di04GraceSpin) s.setValue(QStringLiteral("di04_grace"), m_di04GraceSpin->value());
    if (m_plateauNSpin) s.setValue(QStringLiteral("plateau_n"), m_plateauNSpin->value());
    if (m_advOptionsGroup) s.setValue(QStringLiteral("adv_expanded"), m_advOptionsGroup->isChecked());
}

bool MainWindow::showWorkflowDialog(QList<double> &anglesOut, WorkflowFullOptions &optsOut) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("完整工作流"));
    auto *layout = new QVBoxLayout(&dlg);

    auto *angEdit = new QPlainTextEdit(QStringLiteral("0.0\n45.0\n90.0"));
    angEdit->setPlaceholderText(QStringLiteral("每行一个角度 (°)"));
    layout->addWidget(new QLabel(QStringLiteral("目标角度序列:")));
    layout->addWidget(angEdit);

    auto *spdSpin = new QDoubleSpinBox;
    spdSpin->setRange(0.1, 20.0);
    spdSpin->setValue(optsOut.speed);
    spdSpin->setSuffix(QStringLiteral(" °/s"));
    auto *tolSpin = new QDoubleSpinBox;
    tolSpin->setRange(0.05, 5.0);
    tolSpin->setValue(m_tolSpin ? m_tolSpin->value() : 0.5);
    tolSpin->setSuffix(QStringLiteral(" °"));
    auto *toSpin = new QDoubleSpinBox;
    toSpin->setRange(10.0, 600.0);
    toSpin->setValue(m_timeoutSpin ? m_timeoutSpin->value() : 300.0);
    toSpin->setSuffix(QStringLiteral(" s"));

    auto *paramGrid = new QGridLayout;
    paramGrid->addWidget(new QLabel(QStringLiteral("速度")), 0, 0);
    paramGrid->addWidget(spdSpin, 0, 1);
    paramGrid->addWidget(new QLabel(QStringLiteral("容差")), 1, 0);
    paramGrid->addWidget(tolSpin, 1, 1);
    paramGrid->addWidget(new QLabel(QStringLiteral("超时")), 2, 0);
    paramGrid->addWidget(toSpin, 2, 1);
    layout->addLayout(paramGrid);

    auto *skipCheck = new QCheckBox(QStringLiteral("跳过自检"));
    auto *resetCheck = new QCheckBox(QStringLiteral("失败时复位"));
    layout->addWidget(skipCheck);
    layout->addWidget(resetCheck);

    auto *arrCombo = new QComboBox;
    arrCombo->addItem(QStringLiteral("hybrid"), QStringLiteral("hybrid"));
    arrCombo->addItem(QStringLiteral("strict_04"), QStringLiteral("strict_04"));
    arrCombo->addItem(QStringLiteral("angle"), QStringLiteral("angle"));
    if (m_arrivalModeCombo) {
        const int idx = m_arrivalModeCombo->currentIndex();
        if (idx >= 0) arrCombo->setCurrentIndex(idx);
    }
    auto *graceSpin = new QDoubleSpinBox;
    graceSpin->setRange(0.0, 60.0);
    graceSpin->setValue(m_di04GraceSpin ? m_di04GraceSpin->value() : 5.0);
    graceSpin->setSuffix(QStringLiteral(" s"));
    auto *platSpin = new QSpinBox;
    platSpin->setRange(1, 20);
    platSpin->setValue(m_plateauNSpin ? m_plateauNSpin->value() : 5);

    auto *advGrid = new QGridLayout;
    advGrid->addWidget(new QLabel(QStringLiteral("到位模式")), 0, 0);
    advGrid->addWidget(arrCombo, 0, 1);
    advGrid->addWidget(new QLabel(QStringLiteral("DI04宽限")), 1, 0);
    advGrid->addWidget(graceSpin, 1, 1);
    advGrid->addWidget(new QLabel(QStringLiteral("平台采样")), 2, 0);
    advGrid->addWidget(platSpin, 2, 1);
    layout->addLayout(advGrid);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    anglesOut.clear();
    for (const auto &line : angEdit->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        bool convOk = false;
        double val = line.trimmed().toDouble(&convOk);
        if (convOk && std::abs(val) <= 185.0)
            anglesOut.append(val);
    }
    if (anglesOut.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("输入错误"),
            QStringLiteral("未解析到有效角度值。"));
        return false;
    }

    optsOut.speed = spdSpin->value();
    optsOut.tol = tolSpin->value();
    optsOut.timeout = toSpin->value();
    optsOut.skipSelfTest = skipCheck->isChecked();
    optsOut.resetOnFail = resetCheck->isChecked();
    optsOut.arrivalMode = arrCombo->currentData().toString();
    optsOut.di04Grace = graceSpin->value();
    optsOut.plateauN = platSpin->value();
    return true;
}

void MainWindow::showVerifyResultDialog(const QJsonObject &data) {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("点表巡检结果"));
    dlg.resize(720, 480);
    auto *layout = new QVBoxLayout(&dlg);

    const bool allOk = data.value(QStringLiteral("all_ok")).toBool(false);
    auto *summary = new QLabel(allOk
        ? QStringLiteral("巡检通过 (all_ok=true)")
        : QStringLiteral("巡检未完全通过 (all_ok=false)"));
    summary->setStyleSheet(allOk ? "color:#80e080; font-weight:bold;"
                                 : "color:#ff9090; font-weight:bold;");
    layout->addWidget(summary);

    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("类别"), QStringLiteral("名称/地址"), QStringLiteral("值"), QStringLiteral("状态")
    });
    table->horizontalHeader()->setStretchLastSection(true);

    auto addRow = [&](const QString &cat, const QString &name, const QString &val, bool ok) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto setCell = [&](int col, const QString &text, bool fail = false) {
            auto *item = new QTableWidgetItem(text);
            if (fail) item->setForeground(QBrush(QColor(255, 120, 120)));
            table->setItem(row, col, item);
        };
        setCell(0, cat);
        setCell(1, name);
        setCell(2, val);
        setCell(3, ok ? QStringLiteral("OK") : QStringLiteral("失败"), !ok);
    };

    for (const auto &e : data.value(QStringLiteral("errors")).toArray())
        addRow(QStringLiteral("错误"), e.toString(), QStringLiteral("-"), false);

    const auto addSection = [&](const char *key, const QString &cat) {
        for (const auto &v : data.value(QString::fromUtf8(key)).toArray()) {
            const QJsonObject o = v.toObject();
            const bool itemOk = o.value(QStringLiteral("ok")).toBool(true);
            const QString name = o.value(QStringLiteral("name")).toString(
                o.value(QStringLiteral("address_5digit")).toString());
            QString val;
            if (o.contains(QStringLiteral("value"))) {
                const QJsonValue jv = o.value(QStringLiteral("value"));
                val = jv.isBool() ? (jv.toBool() ? QStringLiteral("1") : QStringLiteral("0"))
                                  : QString::number(jv.toDouble());
            }
            addRow(cat, name, val, itemOk);
        }
    };
    addSection("discrete", QStringLiteral("离散"));
    addSection("input_registers", QStringLiteral("输入寄存器"));
    addSection("holding_registers", QStringLiteral("保持寄存器"));

    layout->addWidget(table, 1);

    auto *btnRow = new QHBoxLayout;
    auto *btnExport = new QPushButton(QStringLiteral("导出 JSON"));
    connect(btnExport, &QPushButton::clicked, &dlg, [&]() {
        QString path = QFileDialog::getSaveFileName(&dlg,
            QStringLiteral("导出巡检 JSON"), QStringLiteral("verify_result.json"),
            QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
            f.close();
            onLogMessage(QStringLiteral("巡检 JSON 已保存: %1").arg(path));
        }
    });
    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(btnExport);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    dlg.exec();
}

void MainWindow::showDiscreteMonitor() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("离散量监视 (00000~00069)"));
    dlg.resize(520, 560);
    auto *layout = new QVBoxLayout(&dlg);

    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("地址"), QStringLiteral("名称"), QStringLiteral("值"), QStringLiteral("备注")
    });
    table->horizontalHeader()->setStretchLastSection(true);

    const auto &bits = m_currentStatus.rawDiscreteBits;
    const int n = bits.empty() ? 70 : static_cast<int>(bits.size());
    for (int i = 0; i < n && i < 70; ++i) {
        const int row = table->rowCount();
        table->insertRow(row);
        const bool val = i < (int)bits.size() && bits[i];
        bool alert = false;
        if (i >= 33 && i <= 36 && val) alert = true;
        if ((i == 20 || i == 21) && !val) alert = true;
        if ((i == 38 || i == 39) && val) alert = true;
        if ((i == 42 || i == 43) && val) alert = true;
        if (i >= 61 && i <= 65 && val) alert = true;

        auto setCell = [&](int col, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            if (alert) item->setForeground(QBrush(QColor(255, 100, 100)));
            table->setItem(row, col, item);
        };
        setCell(0, QString::number(i).rightJustified(5, '0'));
        setCell(1, discreteInputLabel(i));
        setCell(2, val ? QStringLiteral("1") : QStringLiteral("0"));
        setCell(3, alert ? QStringLiteral("异常") : QString());
    }

    layout->addWidget(table, 1);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    dlg.exec();
}
