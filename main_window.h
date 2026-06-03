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
#include <vector>

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
    void startHoming()   { m_client.startHoming(); }
    void resetFault()    { m_client.resetFault(); }
    void emergencyStop();
    void jogFwd();
    void jogRev();
    void moveToPosition();
    void stopManual()    { m_client.stopManualMotion(); }
    void closeBrakes()   { m_client.closeAllBrakes(); }

private:
    void buildUi();
    QWidget *buildTopPanel();
    QWidget *buildControlPanel();
    QWidget *buildChartPanel();
    QWidget *buildLogPanel();
    QWidget *buildConnectionPanel();

    // 圆形指示灯工具
    struct LedItem {
        QLabel *dot = nullptr;
        QLabel *text = nullptr;
    };
    LedItem makeLed(const QString &label);
    void setLedColor(LedItem &led, bool on, bool running = false);
    void updateStatusLeds(const GantryStatus &s);
    void resetAllLeds();
    void updateAngleDisplay(const GantryStatus &s);
    void updateParameterDisplay(const GantryStatus &s);
    void updateChart(double angle);

    GantryClient m_client;
    QTimer m_pollTimer;

    // 连接参数
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QComboBox *m_connModeCombo = nullptr;
    QLabel *m_connStatusLamp = nullptr;
    QLabel *m_connStatusLabel = nullptr;

    // 仪表盘
    GaugeWidget *m_gauge = nullptr;

    // 圆形状态灯
    LedItem m_ledAuto, m_ledManual, m_ledHoming, m_ledPosition;
    LedItem m_ledMotor, m_ledHomingDone, m_ledEstop, m_ledSafety;
    LedItem m_ledAir, m_ledMotionInhibit, m_ledBeamPermit;

    // 参数显示
    QLabel *m_labelServoAngle = nullptr, *m_labelAbs01Angle = nullptr;
    QLabel *m_labelCurrentSpeed = nullptr;
    QLabel *m_labelPositionSetpoint = nullptr, *m_labelSpeedSetpoint = nullptr;
    QLabel *m_labelServo1Torque = nullptr, *m_labelServo2Torque = nullptr;
    QLabel *m_labelSlip1 = nullptr, *m_labelSlip2 = nullptr;
    QLabel *m_labelShearForce = nullptr, *m_labelEstopOvershoot = nullptr;

    // 控制输入
    QDoubleSpinBox *m_targetAngleSpin = nullptr, *m_targetSpeedSpin = nullptr;
    QDoubleSpinBox *m_jogSpeedSpin = nullptr, *m_jogSecondsSpin = nullptr;

    // 曲线
    QChart *m_chart = nullptr;
    QLineSeries *m_angleSeries = nullptr;
    QScatterSeries *m_targetScatter = nullptr;
    QDateTimeAxis *m_timeAxis = nullptr;
    QValueAxis *m_angleAxis = nullptr;
    QElapsedTimer m_chartTimer;

    // 日志表格
    QTableWidget *m_logTable = nullptr;
    int m_lastHighlightRow = -1;
    void appendLogRow(const QString &msg);
    void clearLastHighlight();
    static constexpr int kMaxLogRows = 500;

    GantryStatus m_currentStatus;
};
