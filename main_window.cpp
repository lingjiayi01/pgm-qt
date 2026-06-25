#include "main_window.h"
#include "login_dialog.h"
#include <QStringConverter>
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
constexpr int kMinWindowWidth = 1024;
constexpr int kMinWindowHeight = 600;
constexpr int kDefaultWindowWidth = 1360;
constexpr int kDefaultWindowHeight = 860;
constexpr int kTitleBarHeight = 44;
constexpr int kLayoutSpacing = 8;
constexpr int kCompactSpacing = 4;
constexpr int kBtnMinWidth = 72;
constexpr int kTouchBtnMinHeight = 40;
constexpr int kTouchBtnMaxHeight = 48;
constexpr int kGaugeMinSize = 180;

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
QLabel { color: #d0d0d8; }
QPushButton {
    background-color: #333340; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 4px;
    padding: 6px 10px; min-height: 36px; max-height: 48px;
}
QPushButton:hover { background-color: #3d3d4a; border-color: #6a6a78; }
QPushButton:pressed { background-color: #2a2a32; }
QPushButton:checked {
    background-color: #2a4060; border-color: #5090d0; color: #ffffff;
}
QPushButton#btnEstop {
    background-color: #b82020; border: 1px solid #ff5050;
    color: #ffffff; font-weight: bold;
    min-height: 44px;
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
QWidget#titleBar, QWidget#sidebar {
    background-color: #141820;
}
QWidget#titleBar { border-bottom: 2px solid #2a3040; }
QWidget#sidebar { border-right: 1px solid #2a3040; }
QWidget#contentArea { background-color: #1a1a20; }
QLabel#titleText {
    color: #e8ecf4; font-size: 16pt; font-weight: bold;
}
QLabel#titleSub {
    color: #8090a8; font-size: 9pt;
}
QLabel.sectionHeader {
    color: #a0a8c0; font-size: 9pt; font-weight: bold;
    padding-top: 4px;
}
QLabel.paramNameLarge {
    color: #9090a8; font-size: 12pt;
}
QLabel.paramValueLarge {
    color: #f0f0f8; font-size: 14pt; font-weight: bold;
}
)";

// ============================================================================
// 布局辅助
// ============================================================================

void MainWindow::styleUniformButtons(const QList<QPushButton *> &buttons, int minWidth) {
    for (auto *btn : buttons) {
        if (!btn) continue;
        if (minWidth > 0) btn->setMinimumWidth(minWidth);
        btn->setMinimumHeight(kTouchBtnMinHeight);
        btn->setMaximumHeight(kTouchBtnMaxHeight);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

QLabel *MainWindow::makeSectionLabel(const QString &html) {
    auto *lb = new QLabel(html);
    lb->setObjectName(QStringLiteral("sectionHeader"));
    lb->setProperty("class", "sectionHeader");
    lb->setStyleSheet(QStringLiteral("color:#a0a8c0; font-size:9pt; font-weight:bold; padding-top:4px;"));
    return lb;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("PGM 旋转机架控制系统");
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);
    resize(kDefaultWindowWidth, kDefaultWindowHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    connect(&m_client, &GantryClient::authenticationRequired,
            this, &MainWindow::onAuthenticationRequired);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]() {
        m_client.pollStatus();
        const int ms = pollIntervalMs();
        if (m_pollTimer.interval() != ms)
            m_pollTimer.setInterval(ms);
    });

    connect(&m_client, &GantryClient::motionFinished, this, [this]() {
        setMotionButtonsEnabled(true);
    });

    if (m_pollIntervalSpin) {
        connect(m_pollIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { saveSettings(); });
    }
    if (m_chartWindowCombo) {
        connect(m_chartWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { saveSettings(); });
    }

    connect(&m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClockDisplay);
    m_clockTimer.start(1000);
}

MainWindow::~MainWindow() {
    saveSettings();
    m_pollTimer.stop();
    m_client.disconnect();
}

// ============================================================================
// 主布局 — 顶栏 + 左(仪表/曲线) + 中(控制) + 右(传感器/报警)
// ============================================================================

void MainWindow::buildUi() {
    auto *cw = new QWidget(this);
    setCentralWidget(cw);

    auto *root = new QVBoxLayout(cw);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    root->addWidget(buildTopBar());

    auto *body = new QHBoxLayout;
    body->setSpacing(6);
    body->setContentsMargins(6, 6, 6, 6);
    auto *left = buildLeftPanel();
    auto *center = buildCenterPanel();
    auto *right = buildRightPanel();
    for (auto *p : {left, center, right}) {
        p->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    body->addWidget(left, 4);
    body->addWidget(center, 3);
    body->addWidget(right, 4);
    root->addLayout(body, 1);

    updateControlsForConnectionMode();
    updateClockDisplay();
}

QWidget *MainWindow::buildTopBar() {
    auto *bar = new QWidget;
    bar->setObjectName("titleBar");
    bar->setFixedHeight(kTitleBarHeight);

    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(12, 0, 12, 0);
    h->setSpacing(10);

    m_localRadio = new QRadioButton(QStringLiteral("本地"));
    m_remoteRadio = new QRadioButton(QStringLiteral("远程"));
    m_remoteRadio->setChecked(true);
    m_localRadio->setToolTip(QStringLiteral("本地直连（本轮未实现）"));
    m_localRadio->setEnabled(false);
    auto *modeGrp = new QButtonGroup(bar);
    modeGrp->addButton(m_localRadio);
    modeGrp->addButton(m_remoteRadio);
    h->addWidget(m_localRadio);
    h->addWidget(m_remoteRadio);

    auto *title = new QLabel(QStringLiteral("PGM 旋转机架控制系统"));
    title->setObjectName("titleText");
    h->addWidget(title);

    m_motionInhibitBar = new QLabel(QStringLiteral("运动允许"));
    m_motionInhibitBar->setAlignment(Qt::AlignCenter);
    m_motionInhibitBar->setStyleSheet(
        "background-color:#1a3a1a; color:#80e080; font-weight:bold;"
        "padding:2px 10px; border-radius:3px; font-size:8pt;");
    h->addWidget(m_motionInhibitBar);

    h->addStretch();

    m_userLabel = new QLabel;
    m_userLabel->setStyleSheet(QStringLiteral("color:#90a8c8; font-size:9pt;"));
    h->addWidget(m_userLabel);

    m_timeLabel = new QLabel;
    m_timeLabel->setStyleSheet("color:#90a0b8; font-size:10pt;");
    h->addWidget(m_timeLabel);

    auto *logo = new QLabel(QStringLiteral("PGM"));
    logo->setStyleSheet(
        "color:#5090d0; font-size:14pt; font-weight:bold;"
        "border:1px solid #4060a0; border-radius:4px; padding:2px 8px;");
    h->addWidget(logo);

    m_connStatusLamp = new QLabel;
    m_connStatusLamp->setFixedSize(10, 10);
    m_connStatusLamp->setStyleSheet("background-color:#d04040; border-radius:5px;");
    m_connStatusLabel = new QLabel(QStringLiteral("已断开"));
    m_connStatusLabel->setStyleSheet("color:#d04040; font-size:9pt;");
    h->addWidget(m_connStatusLamp);
    h->addWidget(m_connStatusLabel);

    return bar;
}

void MainWindow::addFormReading(QFormLayout *form, const QString &name, QLabel *&out,
                                  const QString &unit) {
    auto *nameLb = new QLabel(name);
    nameLb->setStyleSheet("color:#888898; font-size:9pt;");
    out = new QLabel(QStringLiteral("—"));
    out->setStyleSheet("color:#e8e8f0; font-size:10pt; font-weight:bold;");
    if (unit.isEmpty()) {
        form->addRow(nameLb, out);
    } else {
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(out);
        auto *u = new QLabel(unit);
        u->setStyleSheet("color:#707080; font-size:9pt;");
        row->addWidget(u);
        row->addStretch();
        auto *wrap = new QWidget;
        wrap->setLayout(row);
        form->addRow(nameLb, wrap);
    }
}

QWidget *MainWindow::buildLeftPanel() {
    auto *panel = new QWidget;
    auto *v = new QVBoxLayout(panel);
    v->setSpacing(kLayoutSpacing);
    v->setContentsMargins(0, 0, 0, 0);

    // 仪表数据
    auto *instr = new QWidget;
    auto *instrV = new QVBoxLayout(instr);
    instrV->setSpacing(kCompactSpacing);
    instrV->addWidget(makeSectionLabel(QStringLiteral("<b>仪表数据</b>")));

    m_gauge = new GaugeWidget;
    m_gauge->setTitle(QStringLiteral("旋转机架"));
    m_gauge->setMinimumSize(kGaugeMinSize, kGaugeMinSize);
    instrV->addWidget(m_gauge, 1, Qt::AlignHCenter);

    auto *readForm = new QFormLayout;
    readForm->setSpacing(3);
    readForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    addFormReading(readForm, QStringLiteral("ABS_01"), m_labelAbs01Angle, QStringLiteral("°"));
    addFormReading(readForm, QStringLiteral("伺服角"), m_labelServoAngle, QStringLiteral("°"));
    addFormReading(readForm, QStringLiteral("当前速度"), m_labelCurrentSpeed, QStringLiteral("°/s"));
    addFormReading(readForm, QStringLiteral("扭矩1"), m_labelServo1Torque, QStringLiteral("Nm"));
    addFormReading(readForm, QStringLiteral("扭矩2"), m_labelServo2Torque, QStringLiteral("Nm"));
    instrV->addLayout(readForm);
    v->addWidget(instr, 2);

    // 曲线区（Qt Charts 实现速度/力矩切换）
    auto *curveBox = new QWidget;
    auto *curveV = new QVBoxLayout(curveBox);
    curveV->setSpacing(kCompactSpacing);
    curveV->addWidget(makeSectionLabel(QStringLiteral("<b>实时曲线</b>")));

    auto *curveBar = new QHBoxLayout;
    m_btnCurveSpeed = new QPushButton(QStringLiteral("速度曲线"));
    m_btnCurveTorque = new QPushButton(QStringLiteral("力矩曲线"));
    m_btnCurveSpeed->setCheckable(true);
    m_btnCurveTorque->setCheckable(true);
    m_btnCurveSpeed->setChecked(true);
    auto *curveGrp = new QButtonGroup(curveBox);
    curveGrp->addButton(m_btnCurveSpeed, 0);
    curveGrp->addButton(m_btnCurveTorque, 1);
    curveGrp->setExclusive(true);
    connect(m_btnCurveSpeed, &QPushButton::clicked, this, &MainWindow::switchCurveToSpeed);
    connect(m_btnCurveTorque, &QPushButton::clicked, this, &MainWindow::switchCurveToTorque);
    curveBar->addWidget(m_btnCurveSpeed);
    curveBar->addWidget(m_btnCurveTorque);
    curveBar->addStretch();
    auto *winLb = new QLabel(QStringLiteral("窗口"));
    m_chartWindowCombo = new QComboBox;
    m_chartWindowCombo->addItems({QStringLiteral("30s"), QStringLiteral("1min"),
                                  QStringLiteral("2min"), QStringLiteral("5min")});
    m_chartWindowCombo->setFixedWidth(64);
    curveBar->addWidget(winLb);
    curveBar->addWidget(m_chartWindowCombo);
    auto *btnPng = new QPushButton(QStringLiteral("截图"));
    auto *btnCsv = new QPushButton(QStringLiteral("CSV"));
    connect(btnPng, &QPushButton::clicked, this, &MainWindow::exportChartPng);
    connect(btnCsv, &QPushButton::clicked, this, &MainWindow::exportChartCsv);
    curveBar->addWidget(btnPng);
    curveBar->addWidget(btnCsv);
    curveV->addLayout(curveBar);

    initTrendChart();
    m_trendChartView = new QChartView(m_trendChart);
    m_trendChartView->setRenderHint(QPainter::Antialiasing);
    m_trendChartView->setMinimumHeight(160);
    m_trendChartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    curveV->addWidget(m_trendChartView, 1);
    v->addWidget(curveBox, 3);

    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return panel;
}

QWidget *MainWindow::buildCenterPanel() {
    auto *panel = new QWidget;
    auto *v = new QVBoxLayout(panel);
    v->setSpacing(kLayoutSpacing);
    v->setContentsMargins(4, 0, 4, 0);

    v->addWidget(makeSectionLabel(QStringLiteral("<b>通信连接</b>")));
    auto *connForm = new QFormLayout;
    connForm->setSpacing(4);
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_portEdit = new QLineEdit(QStringLiteral("8080"));
    m_portEdit->setMaximumWidth(72);
    m_pollIntervalSpin = new QSpinBox;
    m_pollIntervalSpin->setRange(300, 5000);
    m_pollIntervalSpin->setValue(1000);
    m_pollIntervalSpin->setSuffix(QStringLiteral(" ms"));
    connForm->addRow(QStringLiteral("主机"), m_hostEdit);
    connForm->addRow(QStringLiteral("端口"), m_portEdit);
    connForm->addRow(QStringLiteral("轮询"), m_pollIntervalSpin);
    v->addLayout(connForm);

    auto *connBtns = new QHBoxLayout;
    auto *btnC = new QPushButton(QStringLiteral("连接"));
    btnC->setObjectName("btnConnect");
    auto *btnD = new QPushButton(QStringLiteral("断开"));
    btnD->setObjectName("btnDisconnect");
    connect(btnC, &QPushButton::clicked, this, &MainWindow::connectToBackend);
    connect(btnD, &QPushButton::clicked, this, &MainWindow::disconnectFromBackend);
    styleUniformButtons({btnC, btnD}, 0);
    connBtns->addWidget(btnC, 1);
    connBtns->addWidget(btnD, 1);
    v->addLayout(connBtns);

    v->addSpacing(4);
    m_motionModbusBlock = new QWidget;
    auto *motV = new QVBoxLayout(m_motionModbusBlock);
    motV->setSpacing(kCompactSpacing);
    motV->setContentsMargins(0, 0, 0, 0);

    // 模式
    motV->addWidget(makeSectionLabel(QStringLiteral("<b>模式选择</b>")));
    auto *modeRow = new QHBoxLayout;
    m_btnAuto = new QPushButton(QStringLiteral("自动"));
    m_btnManual = new QPushButton(QStringLiteral("手动"));
    connect(m_btnAuto, &QPushButton::clicked, this, &MainWindow::setAutoMode);
    connect(m_btnManual, &QPushButton::clicked, this, &MainWindow::setManualMode);
    styleUniformButtons({m_btnAuto, m_btnManual}, 0);
    modeRow->addWidget(m_btnAuto, 1);
    modeRow->addWidget(m_btnManual, 1);
    m_ledAuto = makeLed(QStringLiteral("自动模式"));
    m_ledManual = makeLed(QStringLiteral("手动模式"));
    modeRow->addWidget(m_ledAuto.dot);
    modeRow->addWidget(m_ledAuto.text);
    modeRow->addWidget(m_ledManual.dot);
    modeRow->addWidget(m_ledManual.text);
    motV->addLayout(modeRow);

    // 启动/转向
    motV->addWidget(makeSectionLabel(QStringLiteral("<b>启动 / 转向</b>")));
    auto *jogG = new QGridLayout;
    jogG->setSpacing(kCompactSpacing);
    m_jogSpeedSpin = new QDoubleSpinBox;
    m_jogSpeedSpin->setRange(0.1, 20.0);
    m_jogSpeedSpin->setValue(3.0);
    m_jogSpeedSpin->setSuffix(QStringLiteral(" °/s"));
    m_jogSecondsSpin = new QDoubleSpinBox;
    m_jogSecondsSpin->setRange(0.1, 60.0);
    m_jogSecondsSpin->setValue(1.0);
    m_jogSecondsSpin->setSuffix(QStringLiteral(" s"));
    auto *btnFwd = new QPushButton(QStringLiteral("正转"));
    auto *btnRev = new QPushButton(QStringLiteral("反转"));
    auto *btnHome = new QPushButton(QStringLiteral("寻零"));
    connect(btnFwd, &QPushButton::clicked, this, &MainWindow::jogFwd);
    connect(btnRev, &QPushButton::clicked, this, &MainWindow::jogRev);
    m_btnHome = btnHome;
    connect(m_btnHome, &QPushButton::clicked, this, &MainWindow::startHoming);
    styleUniformButtons({btnFwd, btnRev, m_btnHome}, 0);
    jogG->addWidget(new QLabel(QStringLiteral("速度")), 0, 0);
    jogG->addWidget(m_jogSpeedSpin, 0, 1);
    jogG->addWidget(new QLabel(QStringLiteral("时长")), 0, 2);
    jogG->addWidget(m_jogSecondsSpin, 0, 3);
    jogG->addWidget(btnFwd, 1, 0);
    jogG->addWidget(btnRev, 1, 1);
    jogG->addWidget(m_btnHome, 1, 2, 1, 2);
    motV->addLayout(jogG);

    // 给定值
    motV->addWidget(makeSectionLabel(QStringLiteral("<b>给定值</b>")));
    auto *setG = new QGridLayout;
    setG->setSpacing(kCompactSpacing);
    m_targetAngleSpin = new QDoubleSpinBox;
    m_targetAngleSpin->setRange(-185.0, 185.0);
    m_targetAngleSpin->setSuffix(QStringLiteral(" °"));
    m_targetSpeedSpin = new QDoubleSpinBox;
    m_targetSpeedSpin->setRange(0.1, 20.0);
    m_targetSpeedSpin->setValue(3.0);
    m_targetSpeedSpin->setSuffix(QStringLiteral(" °/s"));
    m_timeoutSpin = new QDoubleSpinBox;
    m_timeoutSpin->setRange(10.0, 600.0);
    m_timeoutSpin->setValue(300.0);
    m_timeoutSpin->setSuffix(QStringLiteral(" s"));
    setG->addWidget(new QLabel(QStringLiteral("位置")), 0, 0);
    setG->addWidget(m_targetAngleSpin, 0, 1);
    setG->addWidget(new QLabel(QStringLiteral("速度")), 0, 2);
    setG->addWidget(m_targetSpeedSpin, 0, 3);
    setG->addWidget(new QLabel(QStringLiteral("超时")), 1, 0);
    setG->addWidget(m_timeoutSpin, 1, 1);
    m_btnMove = new QPushButton(QStringLiteral("执行定角"));
    m_btnMove->setObjectName("btnMove");
    connect(m_btnMove, &QPushButton::clicked, this, &MainWindow::moveToPosition);
    styleUniformButtons({m_btnMove}, 0);
    setG->addWidget(m_btnMove, 1, 2, 1, 2);
    motV->addLayout(setG);

    // 复位/停止
    motV->addWidget(makeSectionLabel(QStringLiteral("<b>复位 / 停止</b>")));
    auto *stopG = new QGridLayout;
    m_btnReset = new QPushButton(QStringLiteral("故障复位"));
    auto *btnStop = new QPushButton(QStringLiteral("停止"));
    m_btnEstop = new QPushButton(QStringLiteral("触发急停"));
    m_btnEstop->setObjectName("btnEstop");
    connect(m_btnReset, &QPushButton::clicked, this, &MainWindow::resetFault);
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopManual);
    connect(m_btnEstop, &QPushButton::clicked, this, &MainWindow::emergencyStop);
    styleUniformButtons({m_btnReset, btnStop, m_btnEstop}, 0);
    stopG->addWidget(m_btnReset, 0, 0);
    stopG->addWidget(btnStop, 0, 1);
    stopG->addWidget(m_btnEstop, 0, 2);
    motV->addLayout(stopG);

    // 状态
    auto *statRow = new QHBoxLayout;
    m_ledHomingDone = makeLed(QStringLiteral("寻零完成"));
    statRow->addWidget(m_ledHomingDone.dot);
    statRow->addWidget(m_ledHomingDone.text);
    m_labelMotorStatus = new QLabel(QStringLiteral("电机：停止"));
    m_labelMotorStatus->setStyleSheet("color:#c0c0d0; font-size:10pt; font-weight:bold;");
    statRow->addWidget(m_labelMotorStatus);
    statRow->addStretch();
    motV->addLayout(statRow);

    // 工作流 / 工程师
    motV->addWidget(makeSectionLabel(QStringLiteral("<b>工作流 / 工具</b>")));
    auto *toolG = new QGridLayout;
    m_btnSelfTest = new QPushButton(QStringLiteral("上电自检"));
    m_btnWorkflow = new QPushButton(QStringLiteral("完整工作流"));
    m_btnVerify = new QPushButton(QStringLiteral("点表巡检"));
    m_btnDiscrete = new QPushButton(QStringLiteral("离散量"));
    connect(m_btnSelfTest, &QPushButton::clicked, this, &MainWindow::runSelfTest);
    connect(m_btnWorkflow, &QPushButton::clicked, this, &MainWindow::runWorkflowFull);
    connect(m_btnVerify, &QPushButton::clicked, this, &MainWindow::runPointTableVerify);
    connect(m_btnDiscrete, &QPushButton::clicked, this, &MainWindow::showDiscreteMonitor);
    styleUniformButtons({m_btnSelfTest, m_btnWorkflow, m_btnVerify, m_btnDiscrete}, 0);
    toolG->addWidget(m_btnSelfTest, 0, 0);
    toolG->addWidget(m_btnWorkflow, 0, 1);
    toolG->addWidget(m_btnVerify, 1, 0);
    toolG->addWidget(m_btnDiscrete, 1, 1);
    motV->addLayout(toolG);

    v->addWidget(m_motionModbusBlock);
    v->addStretch(1);

    // 隐藏高级参数控件
    m_advOptionsContent = new QWidget;
    m_tolSpin = new QDoubleSpinBox;
    m_tolSpin->setValue(0.5);
    m_arrivalModeCombo = new QComboBox;
    m_arrivalModeCombo->addItem("hybrid", "hybrid");
    m_arrivalModeCombo->addItem("strict_04", "strict_04");
    m_arrivalModeCombo->addItem("angle", "angle");
    m_requireHomingCheck = new QCheckBox;
    m_requireHomingCheck->setChecked(true);
    m_autoModeCheck = new QCheckBox;
    m_autoModeCheck->setChecked(true);
    m_di04GraceSpin = new QDoubleSpinBox;
    m_di04GraceSpin->setValue(5.0);
    m_plateauNSpin = new QSpinBox;
    m_plateauNSpin->setValue(5);
    m_advOptionsContent->setVisible(false);
    m_advOptionsContent->setMaximumHeight(0);
    v->addWidget(m_advOptionsContent);

    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return panel;
}

void MainWindow::addSensorLedRow(QFormLayout *form, const QString &name, LedItem &item) {
    item = makeLed(QStringLiteral("—"));
    auto *row = new QWidget;
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);
    hl->addWidget(item.dot);
    hl->addWidget(item.text, 1);
    auto *nameLb = new QLabel(name);
    nameLb->setStyleSheet("color:#888898; font-size:9pt;");
    form->addRow(nameLb, row);
}

QWidget *MainWindow::buildRightPanel() {
    auto *panel = new QWidget;
    auto *v = new QVBoxLayout(panel);
    v->setSpacing(kLayoutSpacing);
    v->setContentsMargins(0, 0, 0, 0);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(kLayoutSpacing);

    // 传感器信号
    auto *sensorBox = new QWidget;
    auto *sensorV = new QVBoxLayout(sensorBox);
    sensorV->setContentsMargins(0, 0, 0, 0);
    sensorV->addWidget(makeSectionLabel(QStringLiteral("<b>传感器信号</b>")));
    auto *sensorForm = new QFormLayout;
    sensorForm->setSpacing(4);
    addSensorLedRow(sensorForm, QStringLiteral("急停"), m_ledEstop);
    addSensorLedRow(sensorForm, QStringLiteral("安全继电器"), m_ledSafety);
    addSensorLedRow(sensorForm, QStringLiteral("气压"), m_ledAir);
    addSensorLedRow(sensorForm, QStringLiteral("零位开关"), m_ledZeroSwitch);
    addSensorLedRow(sensorForm, QStringLiteral("+185°极限"), m_ledLimitPos185);
    addSensorLedRow(sensorForm, QStringLiteral("-185°极限"), m_ledLimitNeg185);
    addSensorLedRow(sensorForm, QStringLiteral("正极限"), m_ledAtPosLimit);
    addSensorLedRow(sensorForm, QStringLiteral("负极限"), m_ledAtNegLimit);
    addSensorLedRow(sensorForm, QStringLiteral("运动允许"), m_ledMotionInhibit);
    addSensorLedRow(sensorForm, QStringLiteral("出束许可"), m_ledBeamPermit);
    sensorV->addLayout(sensorForm);
    topRow->addWidget(sensorBox, 1);

    // 制动器控制
    auto *brakeBox = new QWidget;
    auto *brakeV = new QVBoxLayout(brakeBox);
    brakeV->setContentsMargins(0, 0, 0, 0);
    brakeV->addWidget(makeSectionLabel(QStringLiteral("<b>制动器控制</b>")));
    auto *brakeG = new QGridLayout;
    brakeG->setSpacing(kCompactSpacing);
    m_btnBrakesClose = new QPushButton(QStringLiteral("关闭制动"));
    m_btnBrakesOpen = new QPushButton(QStringLiteral("打开制动"));
    m_btnEstopRecover = new QPushButton(QStringLiteral("急停恢复"));
    connect(m_btnBrakesClose, &QPushButton::clicked, this, &MainWindow::closeBrakes);
    connect(m_btnBrakesOpen, &QPushButton::clicked, this, &MainWindow::openBrakes);
    connect(m_btnEstopRecover, &QPushButton::clicked, this, &MainWindow::recoverEstop);
    styleUniformButtons({m_btnBrakesClose, m_btnBrakesOpen, m_btnEstopRecover}, 0);
    brakeG->addWidget(m_btnBrakesClose, 0, 0);
    brakeG->addWidget(m_btnBrakesOpen, 0, 1);
    brakeG->addWidget(m_btnEstopRecover, 1, 0, 1, 2);
    auto *brakeForm = new QFormLayout;
    brakeForm->setSpacing(kCompactSpacing);
    brakeForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    for (int i = 0; i < 6; ++i) {
        m_ledBrakeIndividual[i] = makeLed(QStringLiteral("—"));
        auto *cell = new QWidget;
        auto *hl = new QHBoxLayout(cell);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(m_ledBrakeIndividual[i].dot);
        hl->addWidget(m_ledBrakeIndividual[i].text);
        hl->addStretch();
        const QString name = QStringLiteral("制动%1").arg(i + 1);
        const QString tip = QStringLiteral("000%1 %2打开检测")
                                .arg(11 + i, 2, 10, QChar('0'))
                                .arg(name);
        brakeForm->addRow(name, cell);
        cell->setToolTip(tip);
        if (m_ledBrakeIndividual[i].dot)
            m_ledBrakeIndividual[i].dot->setToolTip(tip);
        if (m_ledBrakeIndividual[i].text)
            m_ledBrakeIndividual[i].text->setToolTip(tip);
    }
    brakeG->addLayout(brakeForm, 2, 0, 1, 2);
    brakeV->addLayout(brakeG);
    topRow->addWidget(brakeBox, 1);

    v->addLayout(topRow, 2);

    // 报警/日志表
    v->addWidget(makeSectionLabel(QStringLiteral("<b>报警与日志</b>")));
    m_logTable = new QTableWidget(0, 3);
    m_logTable->setHorizontalHeaderLabels({
        QStringLiteral("日期"), QStringLiteral("时间"), QStringLiteral("详情")
    });
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setStyleSheet(
        "QTableWidget { background-color:#2C2C34; color:#FFFFFF;"
        "  border:1px solid #44444E; gridline-color:#3E3E46;"
        "  font-family:Consolas,monospace; font-size:9pt; }"
        "QHeaderView::section { background-color:#1E2A3A; color:#FFFFFF;"
        "  border:1px solid #3A4A5A; padding:4px; font-weight:bold; }"
        "QTableWidget::item:selected { background-color:#3A5A8A; }");
    m_logTable->setColumnWidth(0, 92);
    m_logTable->setColumnWidth(1, 100);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(m_logTable, 3);

    auto *logBar = new QHBoxLayout;
    logBar->addStretch();
    auto *btnCsv = new QPushButton(QStringLiteral("导出CSV"));
    auto *btnClr = new QPushButton(QStringLiteral("清空"));
    connect(btnCsv, &QPushButton::clicked, this, &MainWindow::exportLogCsv);
    connect(btnClr, &QPushButton::clicked, this, [this]() {
        if (m_logTable) m_logTable->setRowCount(0);
    });
    logBar->addWidget(btnCsv);
    logBar->addWidget(btnClr);
    v->addLayout(logBar);

    // 隐藏标签（数据刷新用，不占布局）
    m_labelAbs02Angle = new QLabel(panel);
    m_labelPositionSetpoint = new QLabel(panel);
    m_labelSpeedSetpoint = new QLabel(panel);
    m_labelSlip1 = new QLabel(panel);
    m_labelSlip2 = new QLabel(panel);
    m_labelShearForce = new QLabel(panel);
    m_labelEstopOvershoot = new QLabel(panel);
    for (QLabel *lb : {m_labelAbs02Angle, m_labelPositionSetpoint, m_labelSpeedSetpoint,
                       m_labelSlip1, m_labelSlip2, m_labelShearForce, m_labelEstopOvershoot})
        lb->hide();

    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return panel;
}

void MainWindow::initTrendChart() {
    m_speedSeries = new QLineSeries;
    m_speedSeries->setName(QStringLiteral("速度 °/s"));
    m_speedSeries->setColor(QColor(60, 180, 255));
    m_torque1Series = new QLineSeries;
    m_torque1Series->setName(QStringLiteral("扭矩1 Nm"));
    m_torque1Series->setColor(QColor(255, 160, 60));
    m_torque2Series = new QLineSeries;
    m_torque2Series->setName(QStringLiteral("扭矩2 Nm"));
    m_torque2Series->setColor(QColor(180, 100, 255));

    m_trendChart = new QChart;
    m_trendChart->addSeries(m_speedSeries);
    m_trendChart->addSeries(m_torque1Series);
    m_trendChart->addSeries(m_torque2Series);
    m_trendChart->setAnimationOptions(QChart::NoAnimation);
    m_trendChart->legend()->setLabelColor(QColor(200, 200, 210));
    m_trendChart->setBackgroundBrush(QColor(24, 24, 30));
    m_trendChart->setPlotAreaBackgroundBrush(QColor(20, 20, 26));
    m_trendChart->setPlotAreaBackgroundVisible(true);

    m_timeAxis = new QDateTimeAxis;
    m_timeAxis->setFormat(QStringLiteral("HH:mm:ss"));
    m_timeAxis->setLabelsColor(QColor(160, 160, 170));
    m_timeAxis->setGridLineColor(QColor(60, 60, 70));
    m_trendChart->addAxis(m_timeAxis, Qt::AlignBottom);

    m_valueAxis = new QValueAxis;
    m_valueAxis->setLabelsColor(QColor(160, 160, 170));
    m_valueAxis->setGridLineColor(QColor(60, 60, 70));
    m_trendChart->addAxis(m_valueAxis, Qt::AlignLeft);

    for (auto *s : {m_speedSeries, m_torque1Series, m_torque2Series}) {
        s->attachAxis(m_timeAxis);
        s->attachAxis(m_valueAxis);
    }
    refreshCurveVisibility();
}

void MainWindow::switchCurveToSpeed() {
    m_curveMode = CurveSpeed;
    if (m_btnCurveSpeed) m_btnCurveSpeed->setChecked(true);
    refreshCurveVisibility();
}

void MainWindow::switchCurveToTorque() {
    m_curveMode = CurveTorque;
    if (m_btnCurveTorque) m_btnCurveTorque->setChecked(true);
    refreshCurveVisibility();
}

void MainWindow::refreshCurveVisibility() {
    if (!m_speedSeries || !m_torque1Series || !m_torque2Series || !m_trendChart) return;
    const bool speed = (m_curveMode == CurveSpeed);
    m_speedSeries->setVisible(speed);
    m_torque1Series->setVisible(!speed);
    m_torque2Series->setVisible(!speed);
    if (m_valueAxis) {
        if (speed)
            m_valueAxis->setTitleText(QStringLiteral("°/s"));
        else
            m_valueAxis->setTitleText(QStringLiteral("Nm"));
    }
    m_trendChart->update();
}

// ============================================================================
// LED 工具
// ============================================================================

MainWindow::LedItem MainWindow::makeLed(const QString &label, int fontPt) {
    LedItem item;
    item.dot = new QLabel;
    item.dot->setFixedSize(8, 8);
    item.dot->setStyleSheet(
        "background-color:#555; border-radius:4px;"
        "min-width:8px; min-height:8px;");
    item.text = new QLabel(label);
    item.text->setStyleSheet(
        QStringLiteral("color:#b0b0b8; font-size:%1pt;").arg(fontPt));
    item.text->setMinimumWidth(0);
    return item;
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
        on ? "color:#e0e0e8; font-size:9pt;" : "color:#b0b0b8; font-size:9pt;");
}

void MainWindow::updateClockDisplay() {
    if (m_timeLabel)
        m_timeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}
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
    updateMotorStatusText(s);
    updateMotionInhibitBar(s);
    updateAngleDisplay(s);
    updateParameterDisplay(s);
    updateTrendCharts(s);
    if (s.positionSetpoint.has_value()
        && ModbusFloat::isDisplayableAngle(s.positionSetpoint)) {
        m_gauge->setTargetAngle(*s.positionSetpoint);
    } else {
        m_gauge->setTargetAngle(std::numeric_limits<double>::quiet_NaN());
    }
}

void MainWindow::updateMotorStatusText(const GantryStatus &s) {
    if (!m_labelMotorStatus) return;
    QStringList parts;
    if (s.motorRunning) parts << QStringLiteral("运行");
    if (s.homingRunning) parts << QStringLiteral("寻零中");
    if (s.positionModeRunning) parts << QStringLiteral("定位中");
    if (s.speedModeRunning) parts << QStringLiteral("速度模式");
    if (parts.isEmpty()) parts << QStringLiteral("停止");
    m_labelMotorStatus->setText(QStringLiteral("电机：%1").arg(parts.join(QStringLiteral(" / "))));
}

void MainWindow::updateStatusLeds(const GantryStatus &s) {
    setLedColor(m_ledAuto,       s.autoModeActive);
    setLedColor(m_ledManual,     s.manualModeActive);
    setLedColor(m_ledHomingDone, s.homingDone);

    bool e = s.plcEstop || s.estop1 || s.estop2 || s.estop3;
    setLedColor(m_ledEstop, !e);
    if (m_ledEstop.text) {
        if (e) {
            QStringList ch;
            if (s.plcEstop) ch << QStringLiteral("PLC(33)");
            if (s.estop1) ch << QStringLiteral("E1(34)");
            if (s.estop2) ch << QStringLiteral("E2(35)");
            if (s.estop3) ch << QStringLiteral("E3(36)");
            const QString tip = QStringLiteral("急停触发: %1").arg(ch.join(QStringLiteral("、")));
            m_ledEstop.text->setText(QStringLiteral("触发"));
            m_ledEstop.text->setToolTip(tip);
            m_ledEstop.dot->setToolTip(tip);
        } else {
            m_ledEstop.text->setText(QStringLiteral("正常"));
            m_ledEstop.text->setToolTip(QString());
            m_ledEstop.dot->setToolTip(QString());
        }
    }

    setLedColor(m_ledSafety,     !s.safetyRelayNotReady);
    bool airOk = s.air1PressureOk && s.air2PressureOk && !s.air1Low && !s.air2Low;
    setLedColor(m_ledAir,        airOk);
    setLedColor(m_ledZeroSwitch, s.zeroSwitch);
    setLedColor(m_ledLimitPos185, s.limitPos185Ok);
    setLedColor(m_ledLimitNeg185, s.limitNeg185Ok);
    setLedColor(m_ledAtPosLimit,  !s.atPosLimit);
    setLedColor(m_ledAtNegLimit,  !s.atNegLimit);
    setLedColor(m_ledMotionInhibit, !s.motionInhibitEffective());
    for (int i = 0; i < 6; ++i) {
        const bool open = i < (int)s.brakesOpen.size() && s.brakesOpen[i];
        setLedColor(m_ledBrakeIndividual[i], !open);
        if (m_ledBrakeIndividual[i].text)
            m_ledBrakeIndividual[i].text->setText(
                open ? QStringLiteral("打开") : QStringLiteral("关闭"));
    }

    auto [bp, bpr] = s.beamPermit();
    setLedColor(m_ledBeamPermit, bp);
    if (m_ledBeamPermit.text) {
        m_ledBeamPermit.text->setText(bp ? QStringLiteral("许可") : QStringLiteral("禁止"));
        if (!bp && !bpr.isEmpty()) {
            m_ledBeamPermit.text->setToolTip(bpr);
            m_ledBeamPermit.dot->setToolTip(bpr);
        } else {
            m_ledBeamPermit.text->setToolTip(kBeamPermitLedTooltip);
            m_ledBeamPermit.dot->setToolTip(kBeamPermitLedTooltip);
        }
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
            "background-color:#d04040; border-radius:6px; min-width:12px; min-height:12px;");
        m_connStatusLabel->setText(QStringLiteral("已断开"));
        m_connStatusLabel->setStyleSheet("color:#d04040; font-size:9pt; font-weight:bold;");
    } else {
        m_connStatusLamp->setStyleSheet(
            "background-color:#30d050; border-radius:6px; min-width:12px; min-height:12px;");
        m_connStatusLabel->setText(QStringLiteral("已连接"));
        m_connStatusLabel->setStyleSheet("color:#30d050; font-size:9pt; font-weight:bold;");
    }
}

void MainWindow::resetAllLeds() {
    setLedColor(m_ledAuto,       false);
    setLedColor(m_ledManual,     false);
    setLedColor(m_ledHomingDone, false);
    setLedColor(m_ledEstop,      false);
    setLedColor(m_ledSafety,     false);
    setLedColor(m_ledAir,        false);
    setLedColor(m_ledZeroSwitch, false);
    setLedColor(m_ledLimitPos185,false);
    setLedColor(m_ledLimitNeg185,false);
    setLedColor(m_ledAtPosLimit, false);
    setLedColor(m_ledAtNegLimit, false);
    setLedColor(m_ledMotionInhibit, false);
    setLedColor(m_ledBeamPermit, false);
    for (int i = 0; i < 6; ++i) {
        setLedColor(m_ledBrakeIndividual[i], false);
        if (m_ledBrakeIndividual[i].text)
            m_ledBrakeIndividual[i].text->setText(QStringLiteral("—"));
    }
    if (m_labelMotorStatus)
        m_labelMotorStatus->setText(QStringLiteral("电机：停止"));
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
        if (lb) lb->setText(text);
    };
    auto fmtAngle = [](const std::optional<double> &v) -> QString {
        if (!ModbusFloat::isDisplayableAngle(v)) return QStringLiteral("—");
        return QString::number(*v, 'f', 2);
    };
    auto fmtNum = [](const std::optional<double> &v, int dec = 2) -> QString {
        if (!ModbusFloat::isReasonableScalar(v)) return QStringLiteral("—");
        return QString::number(*v, 'f', dec);
    };
    if (!m_labelServoAngle) return;
    setVal(m_labelServoAngle,       fmtAngle(s.servoAngleDeg));
    setVal(m_labelAbs01Angle,       fmtAngle(s.abs01AngleDeg));
    setVal(m_labelAbs02Angle,       fmtAngle(s.abs02AngleDeg));
    setVal(m_labelCurrentSpeed,     fmtNum(s.servoCurrentSpeed));
    setVal(m_labelPositionSetpoint, fmtAngle(s.positionSetpoint));
    setVal(m_labelSpeedSetpoint,    fmtNum(s.speedSetpoint));
    setVal(m_labelServo1Torque,     fmtNum(s.servo1Torque));
    setVal(m_labelServo2Torque,     fmtNum(s.servo2Torque));
    setVal(m_labelSlip1,            fmtNum(s.axialSlip1));
    setVal(m_labelSlip2,            fmtNum(s.axialSlip2));
    setVal(m_labelShearForce,       fmtNum(s.shearForce));
    setVal(m_labelEstopOvershoot,   fmtAngle(s.estopOvershoot));
}

void MainWindow::updateTrendCharts(const GantryStatus &s) {
    if (!m_speedSeries || !m_timeAxis || !m_valueAxis) return;
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 t = now.toMSecsSinceEpoch();

    auto appendSeries = [&](QLineSeries *series, double y) {
        if (!series) return;
        series->append(t, y);
        while (series->count() > kMaxChartPoints)
            series->remove(0);
    };

    const double spd = ModbusFloat::isReasonableScalar(s.servoCurrentSpeed)
        ? *s.servoCurrentSpeed : 0.0;
    appendSeries(m_speedSeries, spd);
    appendSeries(m_torque1Series, ModbusFloat::isReasonableScalar(s.servo1Torque) ? *s.servo1Torque : 0.0);
    appendSeries(m_torque2Series, ModbusFloat::isReasonableScalar(s.servo2Torque) ? *s.servo2Torque : 0.0);

    m_timeAxis->setRange(now.addSecs(-chartWindowSeconds()), now);

    double ymin = 0, ymax = 1;
    if (m_curveMode == CurveSpeed && m_speedSeries->count() > 0) {
        for (const auto &p : m_speedSeries->points()) {
            ymin = std::min(ymin, p.y());
            ymax = std::max(ymax, p.y());
        }
    } else {
        for (auto *ser : {m_torque1Series, m_torque2Series}) {
            for (const auto &p : ser->points()) {
                ymin = std::min(ymin, p.y());
                ymax = std::max(ymax, p.y());
            }
        }
    }
    if (ymax - ymin < 1.0) ymax = ymin + 1.0;
    m_valueAxis->setRange(ymin - 0.5, ymax + 0.5);
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

void MainWindow::applyAuthSession(const AuthSession &session) {
    m_client.setAuthToken(session.token);
    m_client.setAuthProfile(session.username, session.role);
    if (m_userLabel) {
        m_userLabel->setText(
            QStringLiteral("%1 (%2)").arg(session.username, session.role));
        m_userLabel->setToolTip(QStringLiteral("当前登录用户"));
    }
}

void MainWindow::onAuthenticationRequired() {
    if (m_reloginRequested)
        return;
    m_reloginRequested = true;
    m_pollTimer.stop();
    m_client.disconnect();
    QMessageBox::warning(this, QStringLiteral("登录失效"),
                         QStringLiteral("登录已失效，请重新登录。"));
    QApplication::exit(kReloginExitCode);
}

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
    if (!m_trendChartView) return;
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出曲线截图"),
        QStringLiteral("gantry_trend.png"),
        QStringLiteral("PNG (*.png);;All Files (*)"));
    if (path.isEmpty()) return;
    QPixmap pix = m_trendChartView->grab();
    pix.save(path, "PNG");
    onLogMessage(QStringLiteral("曲线截图已保存: %1").arg(path));
}

void MainWindow::exportChartCsv() {
    QLineSeries *series = (m_curveMode == CurveSpeed) ? m_speedSeries : m_torque1Series;
    if (!series) return;
    QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出曲线 CSV"),
        QStringLiteral("gantry_trend.csv"),
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
    out << QStringLiteral("timestamp_ms,value\n");
    for (int i = 0; i < series->count(); ++i) {
        const auto &pt = series->at(i);
        out << QStringLiteral("%1,%2\n").arg(pt.x(), 0, 'f', 0).arg(pt.y(), 0, 'f', 4);
    }
    file.close();
    onLogMessage(QStringLiteral("曲线 CSV 已保存: %1 (%2 点)")
        .arg(path).arg(series->count()));
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
