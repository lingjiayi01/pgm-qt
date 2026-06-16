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
#include <deque>

// ============================================================================
// MainWindow — PGM 旋转机架 Qt6 主控制界面
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
    void connectToPlc();
    void disconnectFromPlc();
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
    void exportChartPng();
    void exportChartCsv();

private:
    void buildUi();
    QWidget *buildTitleBar();
    QWidget *buildStatusPanel();
    QWidget *buildGaugePanel();
    QWidget *buildParametersPanel();
    QWidget *buildRightPanel();
    QWidget *buildConnectionPanel();
    QWidget *buildMotionPanel();
    QWidget *buildSafetyPanel();
    QWidget *buildChartPanel();
    QWidget *buildLogPanel();

    struct LedItem {
        QLabel *dot = nullptr;
        QLabel *text = nullptr;
    };

    static void compactGroupLayout(QLayout *layout);
    static void styleUniformButtons(const QList<QPushButton *> &buttons, int minWidth = 72);
    void addLedToGrid(QGridLayout *grid, int row, int col, LedItem &item, const QString &label);
    void addParamCell(QGridLayout *grid, int row, int colBase,
                      const QString &name, QLabel *&valueOut);
    LedItem makeLed(const QString &label);
    void setLedColor(LedItem &led, bool on, bool running = false);
    void updateStatusLeds(const GantryStatus &s);
    void resetAllLeds();
    void updateAngleDisplay(const GantryStatus &s);
    void updateParameterDisplay(const GantryStatus &s);
    void updateChart(double angle);
    void updateControlsForConnectionMode();
    void setMotionButtonsEnabled(bool enabled);
    int chartWindowSeconds() const;

    GantryClient m_client;
    QTimer m_pollTimer;

    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLabel *m_connStatusLamp = nullptr;
    QLabel *m_connStatusLabel = nullptr;

    GaugeWidget *m_gauge = nullptr;

    LedItem m_ledAuto, m_ledManual, m_ledSpeed, m_ledHoming, m_ledPosition;
    LedItem m_ledMotor, m_ledHomingDone, m_ledEstop, m_ledSafety;
    LedItem m_ledAir, m_ledBrakes, m_ledMotionInhibit, m_ledBeamPermit;

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

    QWidget *m_motionModbusBlock = nullptr;
    QPushButton *m_btnAuto = nullptr, *m_btnManual = nullptr, *m_btnHome = nullptr;
    QPushButton *m_btnReset = nullptr, *m_btnEstop = nullptr, *m_btnBrakesClose = nullptr;
    QPushButton *m_btnBrakesOpen = nullptr, *m_btnEstopRecover = nullptr;
    QPushButton *m_btnSelfTest = nullptr, *m_btnWorkflow = nullptr;
    QPushButton *m_btnMove = nullptr;
    bool m_motionBusy = false;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QLineSeries *m_angleSeries = nullptr;
    QScatterSeries *m_targetScatter = nullptr;
    QDateTimeAxis *m_timeAxis = nullptr;
    QValueAxis *m_angleAxis = nullptr;
    QElapsedTimer m_chartTimer;
    QComboBox *m_chartWindowCombo = nullptr;
    static constexpr int kMaxChartPoints = 1500;
    static constexpr int kParamNameColWidth = 88;

    QTableWidget *m_logTable = nullptr;
    int m_lastHighlightRow = -1;
    void appendLogRow(const QString &msg);
    void clearLastHighlight();
    static constexpr int kMaxLogRows = 500;

    GantryStatus m_currentStatus;
};
