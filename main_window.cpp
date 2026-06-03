#include "main_window.h"
#include <cmath>

// ============================================================================
// 全局样式表: 工业暗色主题
// ============================================================================
static const char *kStyle = R"(
QMainWindow { background-color: #1a1a20; }
QGroupBox {
    font-weight: bold; color: #c0c0d0;
    border: 1px solid #3a3a45; border-radius: 6px;
    margin-top: 16px; padding-top: 16px;
    background-color: #22222a;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 14px; padding: 0 6px;
}
QLabel { color: #d0d0d8; }
QPushButton {
    background-color: #333340; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 4px;
    padding: 6px 14px; min-height: 22px;
}
QPushButton:hover { background-color: #3d3d4a; border-color: #6a6a78; }
QPushButton:pressed { background-color: #2a2a32; }
QPushButton#btnEstop {
    background-color: #5a2020; border: 2px solid #c04040;
    color: #ff8080; font-weight: bold;
}
QPushButton#btnEstop:hover { background-color: #6a2828; }
QPushButton#btnConnect {
    background-color: #204020; border-color: #40a040; color: #80ff80;
}
QPushButton#btnDisconnect {
    background-color: #4a3a10; border-color: #a08030; color: #ffd060;
}
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
    background-color: #2a2a32; color: #d0d0d8;
    border: 1px solid #4a4a55; border-radius: 3px; padding: 4px 6px;
}
QTextEdit {
    background-color: #18181e; color: #a0a8b0;
    border: 1px solid #3a3a45; border-radius: 4px;
    font-family: "Consolas", "Courier New", monospace; font-size: 10pt;
}
)";

// ============================================================================
// 构造 / 析构
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("PGM 旋转机架控制系统");
    resize(1280, 820);
    setMinimumSize(1024, 680);
    setStyleSheet(kStyle);
    buildUi();

    connect(&m_client, &GantryClient::connected, this, [this]() {
        m_connStatusLamp->setStyleSheet(
            "background-color:#30d050; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText("已连接");
        m_connStatusLabel->setStyleSheet("color:#30d050; font-weight:bold;");
        onLogMessage("=== 已连接 ===");
    });
    connect(&m_client, &GantryClient::disconnected, this, [this]() {
        m_connStatusLamp->setStyleSheet(
            "background-color:#d04040; border-radius:7px; min-width:14px; min-height:14px;");
        m_connStatusLabel->setText("已断开");
        m_connStatusLabel->setStyleSheet("color:#d04040; font-weight:bold;");
        m_pollTimer.stop();
        resetAllLeds();
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
// 主布局
// ============================================================================

void MainWindow::buildUi() {
    auto *cw = new QWidget(this);
    setCentralWidget(cw);

    auto *hSplit = new QSplitter(Qt::Horizontal, cw);
    hSplit->setHandleWidth(2);

    // 左面板
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(2, 2, 2, 2);
    auto *vs = new QSplitter(Qt::Vertical);
    vs->addWidget(buildTopPanel());
    vs->addWidget(buildChartPanel());
    vs->setStretchFactor(0, 4);
    vs->setStretchFactor(1, 3);
    ll->addWidget(vs);
    hSplit->addWidget(left);

    // 右面板
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(2, 2, 2, 2);
    rl->addWidget(buildConnectionPanel());
    rl->addWidget(buildControlPanel());
    rl->addWidget(buildLogPanel(), 1);
    hSplit->addWidget(right);

    hSplit->setStretchFactor(0, 2);
    hSplit->setStretchFactor(1, 2);

    auto *ml = new QVBoxLayout(cw);
    ml->setContentsMargins(4, 4, 4, 4);
    ml->addWidget(hSplit);
}

// ============================================================================
// 连接设置
// ============================================================================

QWidget *MainWindow::buildConnectionPanel() {
    auto *gb = new QGroupBox("连接设置");
    auto *l = new QHBoxLayout(gb);

    l->addWidget(new QLabel("主机:"));
    m_hostEdit = new QLineEdit("192.168.10.1");
    m_hostEdit->setFixedWidth(115);
    l->addWidget(m_hostEdit);

    l->addWidget(new QLabel("端口:"));
    m_portEdit = new QLineEdit("510");
    m_portEdit->setFixedWidth(55);
    l->addWidget(m_portEdit);

    l->addWidget(new QLabel("协议:"));
    m_connModeCombo = new QComboBox;
    m_connModeCombo->addItem("Modbus PLC", 0);
    m_connModeCombo->addItem("TCS JSON", 1);
    l->addWidget(m_connModeCombo);

    auto *btnC = new QPushButton("连接");
    btnC->setObjectName("btnConnect");
    connect(btnC, &QPushButton::clicked, this, &MainWindow::connectToPlc);
    l->addWidget(btnC);

    auto *btnD = new QPushButton("断开");
    btnD->setObjectName("btnDisconnect");
    connect(btnD, &QPushButton::clicked, this, &MainWindow::disconnectFromPlc);
    l->addWidget(btnD);

    m_connStatusLamp = new QLabel;
    m_connStatusLamp->setFixedSize(14, 14);
    m_connStatusLamp->setStyleSheet(
        "background-color:#d04040; border-radius:7px; min-width:14px; min-height:14px;");
    l->addWidget(m_connStatusLamp);
    m_connStatusLabel = new QLabel("已断开");
    m_connStatusLabel->setStyleSheet("color:#d04040; font-weight:bold;");
    l->addWidget(m_connStatusLabel);
    l->addStretch();
    return gb;
}

// ============================================================================
// 圆形 LED 状态灯工具
// ============================================================================

MainWindow::LedItem MainWindow::makeLed(const QString &label) {
    LedItem item;
    item.dot = new QLabel;
    item.dot->setFixedSize(10, 10);
    item.dot->setStyleSheet(
        "background-color:#555; border-radius:5px;"
        "min-width:10px; min-height:10px;");
    item.text = new QLabel(label);
    item.text->setStyleSheet("color:#aaa; font-size:9pt;");
    return item;
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
        on ? "color:#e0e0e8; font-size:9pt;" : "color:#bbb; font-size:9pt;");
}

// ============================================================================
// 顶部: 仪表盘 + 紧凑圆形状态灯 + 参数面板
// ============================================================================

QWidget *MainWindow::buildTopPanel() {
    auto *w = new QWidget;
    auto *hbox = new QHBoxLayout(w);
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(6);

    // --- 仪表盘 ---
    m_gauge = new GaugeWidget;
    m_gauge->setTitle("旋转机架角度");
    m_gauge->setFixedSize(260, 260);
    hbox->addWidget(m_gauge);

    // --- 中间竖列: 紧凑圆形状态指示灯 ---
    auto *ledGb = new QGroupBox("系统状态");
    ledGb->setFixedWidth(170);
    auto *ledLayout = new QVBoxLayout(ledGb);
    ledLayout->setContentsMargins(6, 4, 6, 4);
    ledLayout->setSpacing(3);

    auto makeRow = [&](const QString &name, LedItem &holder) {
        holder = makeLed(name);
        auto *row = new QWidget;
        row->setFixedHeight(20);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(2, 0, 2, 0);
        rl->setSpacing(5);
        rl->addWidget(holder.dot, 0, Qt::AlignVCenter);
        rl->addWidget(holder.text, 0, Qt::AlignVCenter);
        rl->addStretch();
        ledLayout->addWidget(row);
    };

    makeRow("自动模式",    m_ledAuto);
    makeRow("手动模式",    m_ledManual);
    makeRow("寻零运行",    m_ledHoming);
    makeRow("位置运行",    m_ledPosition);
    makeRow("电机运行",    m_ledMotor);
    makeRow("寻零完成",    m_ledHomingDone);
    makeRow("急停触发",    m_ledEstop);
    makeRow("安全继电器",  m_ledSafety);
    makeRow("气压异常",    m_ledAir);
    makeRow("运动禁止",    m_ledMotionInhibit);
    makeRow("可出束",      m_ledBeamPermit);

    ledLayout->addStretch();
    hbox->addWidget(ledGb);

    // --- 参数面板 ---
    auto *pgb = new QGroupBox("实时参数");
    auto *p = new QGridLayout(pgb);
    p->setSpacing(3);
    auto ap = [&](int r, const QString &t, QLabel *&h) {
        auto *lb = new QLabel(t + ":");
        lb->setStyleSheet("color:#888; font-size:9pt;");
        h = new QLabel("—");
        h->setStyleSheet("color:#e0e0e8; font-size:10pt; font-weight:bold;");
        p->addWidget(lb, r, 0, Qt::AlignRight);
        p->addWidget(h, r, 1, Qt::AlignLeft);
    };
    ap(0,  "伺服角度",    m_labelServoAngle);
    ap(1,  "ABS_01角度",  m_labelAbs01Angle);
    ap(2,  "当前速度",    m_labelCurrentSpeed);
    ap(3,  "位置给定",    m_labelPositionSetpoint);
    ap(4,  "速度给定",    m_labelSpeedSetpoint);
    ap(5,  "伺服1扭矩",   m_labelServo1Torque);
    ap(6,  "伺服2扭矩",   m_labelServo2Torque);
    ap(7,  "串动1",       m_labelSlip1);
    ap(8,  "串动2",       m_labelSlip2);
    ap(9,  "剪切力",      m_labelShearForce);
    ap(10, "急停过冲",    m_labelEstopOvershoot);
    hbox->addWidget(pgb);

    return w;
}

// ============================================================================
// 控制按钮
// ============================================================================

QWidget *MainWindow::buildControlPanel() {
    auto *gb = new QGroupBox("运动控制");
    auto *g = new QGridLayout(gb);
    g->setSpacing(5);

    // 模式
    g->addWidget(new QLabel("<b>模式</b>"), 0, 0, 1, 2);
    auto *btnAuto = new QPushButton("自动模式");
    connect(btnAuto, &QPushButton::clicked, this, &MainWindow::setAutoMode);
    g->addWidget(btnAuto, 1, 0);
    auto *btnManual = new QPushButton("手动模式");
    connect(btnManual, &QPushButton::clicked, this, &MainWindow::setManualMode);
    g->addWidget(btnManual, 1, 1);
    auto *btnHome = new QPushButton("寻零");
    connect(btnHome, &QPushButton::clicked, this, &MainWindow::startHoming);
    g->addWidget(btnHome, 2, 0);
    auto *btnRst = new QPushButton("复位故障");
    connect(btnRst, &QPushButton::clicked, this, &MainWindow::resetFault);
    g->addWidget(btnRst, 2, 1);

    // 点动
    g->addWidget(new QLabel("<b>点动</b>"), 3, 0, 1, 2);
    g->addWidget(new QLabel("速度:"), 4, 0);
    m_jogSpeedSpin = new QDoubleSpinBox;
    m_jogSpeedSpin->setRange(0.1, 20.0); m_jogSpeedSpin->setValue(3.0);
    m_jogSpeedSpin->setSuffix(" °/s");  m_jogSpeedSpin->setDecimals(1);
    g->addWidget(m_jogSpeedSpin, 4, 1);

    g->addWidget(new QLabel("时长:"), 5, 0);
    m_jogSecondsSpin = new QDoubleSpinBox;
    m_jogSecondsSpin->setRange(0.1, 60.0); m_jogSecondsSpin->setValue(1.0);
    m_jogSecondsSpin->setSuffix(" s"); m_jogSecondsSpin->setDecimals(1);
    g->addWidget(m_jogSecondsSpin, 5, 1);

    auto *btnFwd = new QPushButton("▶ 正转");
    btnFwd->setStyleSheet("color:#80ff80;");
    connect(btnFwd, &QPushButton::clicked, this, &MainWindow::jogFwd);
    g->addWidget(btnFwd, 6, 0);
    auto *btnRev = new QPushButton("◀ 反转");
    btnRev->setStyleSheet("color:#ff8080;");
    connect(btnRev, &QPushButton::clicked, this, &MainWindow::jogRev);
    g->addWidget(btnRev, 6, 1);
    auto *btnStop = new QPushButton("■ 停止");
    connect(btnStop, &QPushButton::clicked, this, &MainWindow::stopManual);
    g->addWidget(btnStop, 6, 2);

    // 定位
    g->addWidget(new QLabel("<b>定位</b>"), 7, 0, 1, 2);
    g->addWidget(new QLabel("目标角度:"), 8, 0);
    m_targetAngleSpin = new QDoubleSpinBox;
    m_targetAngleSpin->setRange(-185.0, 185.0); m_targetAngleSpin->setValue(0.0);
    m_targetAngleSpin->setSuffix(" °"); m_targetAngleSpin->setDecimals(2);
    g->addWidget(m_targetAngleSpin, 8, 1);

    g->addWidget(new QLabel("速度:"), 9, 0);
    m_targetSpeedSpin = new QDoubleSpinBox;
    m_targetSpeedSpin->setRange(0.1, 20.0); m_targetSpeedSpin->setValue(3.0);
    m_targetSpeedSpin->setSuffix(" °/s"); m_targetSpeedSpin->setDecimals(1);
    g->addWidget(m_targetSpeedSpin, 9, 1);

    auto *btnMove = new QPushButton("执行定位 →");
    btnMove->setStyleSheet(
        "background-color:#203050; border-color:#4060a0; color:#80b0ff;");
    connect(btnMove, &QPushButton::clicked, this, &MainWindow::moveToPosition);
    g->addWidget(btnMove, 10, 0, 1, 2);

    // 安全操作
    g->addWidget(new QLabel("<b>安全操作</b>"), 11, 0, 1, 2);
    auto *btnEsp = new QPushButton("⚠ 紧急停止");
    btnEsp->setObjectName("btnEstop");
    connect(btnEsp, &QPushButton::clicked, this, &MainWindow::emergencyStop);
    g->addWidget(btnEsp, 12, 0, 1, 2);
    auto *btnBrk = new QPushButton("关闭全部制动器");
    connect(btnBrk, &QPushButton::clicked, this, &MainWindow::closeBrakes);
    g->addWidget(btnBrk, 13, 0, 1, 2);

    return gb;
}

// ============================================================================
// 实时曲线
// ============================================================================

QWidget *MainWindow::buildChartPanel() {
    auto *gb = new QGroupBox("角度实时曲线");

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

    auto *l = new QVBoxLayout(gb);
    l->setContentsMargins(2, 2, 2, 2);
    l->addWidget(cv);
    return gb;
}

// ============================================================================
// 日志 — QTableWidget 三列表格
// ============================================================================

QWidget *MainWindow::buildLogPanel() {
    auto *gb = new QGroupBox("通信日志");
    auto *l = new QVBoxLayout(gb);

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
        "QScrollBar:vertical { background:#2C2C34; width:10px; border:1px solid #3E3E46; border-radius:5px; }"
        "QScrollBar::handle:vertical { background:#55555E; border-radius:4px; min-height:24px; }"
        "QScrollBar::handle:vertical:hover { background:#6A6A75; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar:horizontal { background:#2C2C34; height:10px; border:1px solid #3E3E46; border-radius:5px; }"
        "QScrollBar::handle:horizontal { background:#55555E; border-radius:4px; min-width:24px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; }"
    );
    m_logTable->setColumnWidth(0, 80);
    m_logTable->setColumnWidth(1, 90);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    l->addWidget(m_logTable);

    auto *btnBar = new QHBoxLayout;
    auto *btnCl = new QPushButton("清空");
    btnCl->setFixedWidth(60);
    connect(btnCl, &QPushButton::clicked, this, [this]() { m_logTable->setRowCount(0); });
    btnBar->addStretch();
    btnBar->addWidget(btnCl);
    l->addLayout(btnBar);

    return gb;
}

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
        if (it) { it->setBackground(kHL); QFont f = it->font(); f.setBold(true); it->setFont(f); }
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
    if (rr >= m_logTable->rowCount()) { m_lastHighlightRow = -1; return; }
    static const QColor kBg(44, 44, 52);
    for (int c = 0; c < 3; ++c) {
        auto *it = m_logTable->item(rr, c);
        if (it) { it->setBackground(kBg); QFont f = it->font(); f.setBold(false); it->setFont(f); }
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
    setLedColor(m_ledHoming,        s.homingRunning,    true);
    setLedColor(m_ledPosition,      s.positionModeRunning, true);
    setLedColor(m_ledMotor,         s.motorRunning,     true);
    setLedColor(m_ledHomingDone,    s.homingDone);
    bool e = s.plcEstop || s.estop1 || s.estop2 || s.estop3;
    setLedColor(m_ledEstop,         !e);
    setLedColor(m_ledSafety,        !s.safetyRelayNotReady);
    bool airOk = s.air1PressureOk && s.air2PressureOk && !s.air1Low && !s.air2Low;
    setLedColor(m_ledAir,           airOk);
    setLedColor(m_ledMotionInhibit, !s.motionInhibit());
    auto [bp, bpr] = s.beamPermit();
    setLedColor(m_ledBeamPermit,    bp);
    m_ledBeamPermit.text->setText(bp ? "可出束" : QString("可出束(%1)").arg(bpr));
}

void MainWindow::resetAllLeds() {
    setLedColor(m_ledAuto,          false);
    setLedColor(m_ledManual,        false);
    setLedColor(m_ledHoming,        false);
    setLedColor(m_ledPosition,      false);
    setLedColor(m_ledMotor,         false);
    setLedColor(m_ledHomingDone,    false);
    setLedColor(m_ledEstop,         false);
    setLedColor(m_ledSafety,        false);
    setLedColor(m_ledAir,           false);
    setLedColor(m_ledMotionInhibit, false);
    setLedColor(m_ledBeamPermit,    false);
    m_ledBeamPermit.text->setText("可出束");
    m_gauge->setAngle(0.0);
    m_gauge->setTargetAngle(std::numeric_limits<double>::quiet_NaN());
}

void MainWindow::updateAngleDisplay(const GantryStatus &s) {
    m_gauge->setAngle(s.abs01AngleDeg.value_or(s.servoAngleDeg.value_or(0.0)));
}

void MainWindow::updateParameterDisplay(const GantryStatus &s) {
    auto f = [](auto v, int d = 3) {
        return v.has_value() ? QString::number(*v, 'f', d) : "—";
    };
    auto fd = [](auto v) { return v.has_value() ? QString::number(*v, 'f', 3) + "°" : "—"; };
    m_labelServoAngle->setText(    fd(s.servoAngleDeg));
    m_labelAbs01Angle->setText(    fd(s.abs01AngleDeg));
    m_labelCurrentSpeed->setText(  f(s.servoCurrentSpeed, 2));
    m_labelPositionSetpoint->setText(fd(s.positionSetpoint));
    m_labelSpeedSetpoint->setText( f(s.speedSetpoint, 2));
    m_labelServo1Torque->setText(  f(s.servo1Torque, 2));
    m_labelServo2Torque->setText(  f(s.servo2Torque, 2));
    m_labelSlip1->setText(         f(s.axialSlip1, 3));
    m_labelSlip2->setText(         f(s.axialSlip2, 3));
    m_labelShearForce->setText(    f(s.shearForce, 2));
    m_labelEstopOvershoot->setText(fd(s.estopOvershoot));
}

void MainWindow::updateChart(double angle) {
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
            double y = m_angleSeries->at(i).y(); lo = std::min(lo, y); hi = std::max(hi, y);
        }
        double m = std::max(5.0, (hi - lo) * 0.2);
        m_angleAxis->setRange(lo - m, hi + m);
    }
}

// ============================================================================
// 槽函数
// ============================================================================

void MainWindow::onCommandResponse(const TcsResponse &r) {
    if (r.cmd == "ping")
        onLogMessage(r.pong ? "Ping: pong=true" : "Ping 错误: " + r.error);
    else if (r.cmd == "snapshot")
        onLogMessage(QString("快照: beam_permit=%1").arg(r.beamPermit ? "是" : "否"));
    else if (r.cmd == "move")
        onLogMessage(QString("运动: %1 | %2").arg(r.motionComplete ? "完成" : "失败", r.motionDetail));
}

void MainWindow::onLogMessage(const QString &msg) { appendLogRow(msg); }

void MainWindow::onConnectionError(const QString &err) { onLogMessage("通信错误: " + err); }

void MainWindow::connectToPlc() {
    QString host = m_hostEdit->text().trimmed();
    int port = m_portEdit->text().toInt();
    if (m_connModeCombo->currentData().toInt() == 0)
        m_client.connectToPlc(host, static_cast<quint16>(port));
    else
        m_client.connectToTcsService(host, static_cast<quint16>(port));
    m_pollTimer.start(200); m_chartTimer.start();
}

void MainWindow::disconnectFromPlc() {
    m_pollTimer.stop(); resetAllLeds(); m_client.disconnect();
}

void MainWindow::emergencyStop() {
    if (QMessageBox::warning(this, "确认急停",
            "确定要触发紧急停止吗？", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
        m_client.emergencyStop();
}

void MainWindow::jogFwd() { m_client.manualJog(true, m_jogSpeedSpin->value(), m_jogSecondsSpin->value()); }
void MainWindow::jogRev() { m_client.manualJog(false, m_jogSpeedSpin->value(), m_jogSecondsSpin->value()); }
void MainWindow::moveToPosition() { m_client.moveToPosition(m_targetAngleSpin->value(), m_targetSpeedSpin->value(), 300.0); }
