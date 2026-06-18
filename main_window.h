#pragma once
#include "gantry_data.h"
#include "gantry_client.h"
#include "gauge_widget.h"
#include <QtWidgets>
#include <QtCharts>
#include <QChart>
#include <QLineSeries>
#include <QScatterSeries>
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QChartView>
#include <array>
#include <deque>

// ============================================================================
// MainWindow — PGM 旋转机架 Qt6 主控制界面（侧边栏 + 堆叠页）
// ============================================================================

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onStatusUpdated(const GantryStatus &status);
    void onCommandResponse(const TcsResponse &resp);
    void onLogMessage(const QString &msg);
    void onConnectionError(const QString &err);
    void connectToBackend();
    void disconnectFromBackend();
    void setAutoMode()   { m_client.setAutoMode(); }
    void setManualMode() { m_client.setManualMode(); }
    void startHoming();
    void resetFault()    { m_client.resetFault(); }
    void emergencyStop();
    void jogFwd();
    void jogRev();
    void moveToPosition();
    void stopManual()    { m_client.stopManualMotion(); }
    void closeBrakes()   { m_client.closeAllBrakes(); }
    void openBrakes();
    void recoverEstop();
    void runSelfTest();
    void runWorkflowFull();
    void runPointTableVerify();
    void showDiscreteMonitor();
    void exportChartPng();
    void exportChartCsv();
    void exportLogCsv();

private:
    enum PageIndex {
        PageConsole = 0,
        PageParams,
        PageChart,
        PageLog,
        PageStatus,
        PageAbout,
        PageCount
    };

    void buildUi();
    QWidget *buildTitleBar();
    QWidget *buildSidebar();
    void buildStackedPages();
    QWidget *buildPageConsole();
    QWidget *buildPageParameters();
    QWidget *buildPageChart();
    QWidget *buildPageLog();
    QWidget *buildPageStatus();
    QWidget *buildPageAbout();
    void initChart();
    void switchPage(int index);
    void setNavActive(int index);

    struct LedItem {
        QLabel *dot = nullptr;
        QLabel *text = nullptr;
    };

    static QLabel *makeSectionLabel(const QString &html);
    static void styleUniformButtons(const QList<QPushButton *> &buttons, int minWidth = 72);
    void addLedToGrid(QGridLayout *grid, int row, int col, LedItem &item, const QString &label,
                      const QString &tooltip = QString());
    LedItem makeLed(const QString &label, int fontPt = 8);
    void setLedColor(LedItem &led, bool on, bool running = false);
    void syncStatusPageLeds();
    void updateStatusLeds(const GantryStatus &s);
    void updateMotionInhibitBar(const GantryStatus &s);
    void updateConnectionStatusDisplay();
    void updateAboutConnectionText();
    void resetAllLeds();
    void updateAngleDisplay(const GantryStatus &s);
    void updateParameterDisplay(const GantryStatus &s);
    void updateChart(double angle);
    void updateControlsForConnectionMode();
    void setMotionButtonsEnabled(bool enabled);
    void showApiErrorPopup(const TcsResponse &r);
    bool showWorkflowDialog(QList<double> &anglesOut, WorkflowFullOptions &optsOut);
    void showVerifyResultDialog(const QJsonObject &data);
    void loadSettings();
    void saveSettings();
    int chartWindowSeconds() const;
    int pollIntervalMs() const;
    MotionPositionOptions currentPositionOptions() const;

    GantryClient m_client;
    QTimer m_pollTimer;

    QStackedWidget *m_stack = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QList<QPushButton *> m_navButtons;

    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLabel *m_connStatusLamp = nullptr;
    QLabel *m_connStatusLabel = nullptr;
    QLabel *m_motionInhibitBar = nullptr;
    QLabel *m_aboutConnLabel = nullptr;

    GaugeWidget *m_gauge = nullptr;

    LedItem m_ledAuto, m_ledManual, m_ledSpeed, m_ledHoming, m_ledPosition;
    LedItem m_ledMotor, m_ledHomingDone, m_ledEstop, m_ledSafety;
    LedItem m_ledAir, m_ledBrakes, m_ledMotionInhibit, m_ledBeamPermit;
    LedItem m_ledLimitPos185, m_ledLimitNeg185;
    LedItem m_ledAtPosLimit, m_ledAtNegLimit;
    LedItem m_ledAngleOut, m_ledTargetOut, m_ledZeroSwitch, m_ledServoFault;
    LedItem m_ledBrakeIndividual[6];
    std::array<LedItem, 27> m_statusLeds{};

    QLabel *m_labelServoAngle = nullptr, *m_labelAbs01Angle = nullptr;
    QLabel *m_labelAbs02Angle = nullptr;
    QLabel *m_labelCurrentSpeed = nullptr;
    QLabel *m_labelPositionSetpoint = nullptr, *m_labelSpeedSetpoint = nullptr;
    QLabel *m_labelServo1Torque = nullptr, *m_labelServo2Torque = nullptr;
    QLabel *m_labelSlip1 = nullptr, *m_labelSlip2 = nullptr;
    QLabel *m_labelShearForce = nullptr, *m_labelEstopOvershoot = nullptr;

    QDoubleSpinBox *m_targetAngleSpin = nullptr, *m_targetSpeedSpin = nullptr;
    QDoubleSpinBox *m_jogSpeedSpin = nullptr, *m_jogSecondsSpin = nullptr;
    QDoubleSpinBox *m_timeoutSpin = nullptr;
    QDoubleSpinBox *m_tolSpin = nullptr;
    QDoubleSpinBox *m_di04GraceSpin = nullptr;
    QSpinBox *m_plateauNSpin = nullptr;
    QComboBox *m_arrivalModeCombo = nullptr;
    QCheckBox *m_requireHomingCheck = nullptr;
    QCheckBox *m_autoModeCheck = nullptr;
    QWidget *m_advOptionsContent = nullptr;

    QWidget *m_motionModbusBlock = nullptr;
    QPushButton *m_btnAuto = nullptr, *m_btnManual = nullptr, *m_btnHome = nullptr;
    QPushButton *m_btnReset = nullptr, *m_btnEstop = nullptr, *m_btnBrakesClose = nullptr;
    QPushButton *m_btnBrakesOpen = nullptr, *m_btnEstopRecover = nullptr;
    QPushButton *m_btnSelfTest = nullptr, *m_btnWorkflow = nullptr;
    QPushButton *m_btnMove = nullptr;
    QPushButton *m_btnVerify = nullptr;
    QPushButton *m_btnDiscrete = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QLineSeries *m_angleSeries = nullptr;
    QScatterSeries *m_targetScatter = nullptr;
    QDateTimeAxis *m_timeAxis = nullptr;
    QValueAxis *m_angleAxis = nullptr;
    QElapsedTimer m_chartTimer;
    QComboBox *m_chartWindowCombo = nullptr;
    QSpinBox *m_pollIntervalSpin = nullptr;
    static constexpr int kMaxChartPoints = 1500;

    QTableWidget *m_logTable = nullptr;
    int m_lastHighlightRow = -1;
    void appendLogRow(const QString &msg);
    void clearLastHighlight();
    static constexpr int kMaxLogRows = 500;

    GantryStatus m_currentStatus;
};
