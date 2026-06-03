#pragma once
#include <QtWidgets>
#include <cmath>
#include <vector>

// ============================================================================
// GaugeWidget — 弧形仪表盘控件 (汽车仪表盘风格)
// 显示旋转机架当前角度: 弧形刻度 + 指针 + 中心数值
// ============================================================================

class GaugeWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double angle READ angle WRITE setAngle NOTIFY angleChanged)
public:
    explicit GaugeWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(200, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    double angle() const { return m_angle; }
    void setAngle(double deg) {
        if (!std::isfinite(deg)) return;
        m_angle = deg;
        update();
        emit angleChanged(m_angle);
        // 历史缓存 (最多 100 点)
        m_history.push_back(deg);
        if (m_history.size() > 100) m_history.erase(m_history.begin());
    }
    void setTargetAngle(double deg) { m_targetAngle = deg; update(); }
    void setTitle(const QString &t) { m_title = t; update(); }

    const std::vector<double> &history() const { return m_history; }
    void clearHistory() { m_history.clear(); update(); }

signals:
    void angleChanged(double angle);

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int w = width(), h = height();
        const int side = std::min(w, h);
        const int cx = w / 2, cy = h / 2;
        const int radius = side / 2 - 15;

        // 背景圆
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 30, 36));
        p.drawEllipse(QPoint(cx, cy), radius + 8, radius + 8);

        drawScale(p, cx, cy, radius);
        drawNeedle(p, cx, cy, radius);
        drawValue(p, cx, cy);

        // 标题
        QFont f = font();
        f.setPointSize(10);
        p.setFont(f);
        p.setPen(QColor(180, 180, 190));
        p.drawText(QRect(cx - radius, cy + radius / 3 + 5, radius * 2, 24),
                   Qt::AlignHCenter, m_title);

        // 目标角度标记
        if (std::isfinite(m_targetAngle) && m_targetAngle >= m_minAngle
            && m_targetAngle <= m_maxAngle) {
            double ratio = (m_targetAngle - m_minAngle) / (m_maxAngle - m_minAngle);
            double rad = m_startRad - ratio * m_spanRad;
            int tx = cx + int((radius - 6) * std::cos(rad));
            int ty = cy - int((radius - 6) * std::sin(rad));
            p.setPen(QPen(QColor(0, 210, 255), 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPoint(tx, ty), 5, 5);
        }
    }

private:
    void drawScale(QPainter &p, int cx, int cy, int r) {
        const int n = 10;
        // 主刻度
        for (int i = 0; i <= n; ++i) {
            double ratio = double(i) / n;
            double rad = m_startRad - ratio * m_spanRad;
            double deg = m_minAngle + ratio * (m_maxAngle - m_minAngle);
            double x1 = cx + (r - 15) * std::cos(rad);
            double y1 = cy - (r - 15) * std::sin(rad);
            double x2 = cx + (r - 3) * std::cos(rad);
            double y2 = cy - (r - 3) * std::sin(rad);
            bool danger = std::abs(deg) > 165.0;
            p.setPen(QPen(danger ? QColor(220, 60, 60) : QColor(140, 140, 155), 2));
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
            // 标签
            QFont f = font();
            f.setPointSize(8);
            p.setFont(f);
            p.setPen(QColor(200, 200, 210));
            double lr = r - 28;
            p.drawText(QRectF(cx + lr * std::cos(rad) - 22, cy - lr * std::sin(rad) - 9,
                              44, 18), Qt::AlignCenter,
                       QString::number(deg, 'f', 0));
        }
        // 次级刻度
        p.setPen(QPen(QColor(70, 70, 80), 1));
        for (int i = 0; i <= n * 5; ++i) {
            if (i % 5 == 0) continue;
            double ratio = double(i) / (n * 5);
            double rad = m_startRad - ratio * m_spanRad;
            double x1 = cx + (r - 10) * std::cos(rad);
            double y1 = cy - (r - 10) * std::sin(rad);
            double x2 = cx + (r - 3) * std::cos(rad);
            double y2 = cy - (r - 3) * std::sin(rad);
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }

    void drawNeedle(QPainter &p, int cx, int cy, int r) {
        double clamped = std::clamp(m_angle, m_minAngle, m_maxAngle);
        double ratio = (clamped - m_minAngle) / (m_maxAngle - m_minAngle);
        double rad = m_startRad - ratio * m_spanRad;
        int len = r - 25;
        int ex = cx + int(len * std::cos(rad));
        int ey = cy - int(len * std::sin(rad));
        // 阴影
        p.setPen(QPen(QColor(0, 0, 0, 80), 5));
        p.drawLine(cx + 1, cy + 1, ex + 1, ey + 1);
        // 本体 (渐变色)
        QLinearGradient g(QPointF(cx, cy), QPointF(ex, ey));
        g.setColorAt(0.0, QColor(255, 80, 80));
        g.setColorAt(1.0, QColor(255, 180, 60));
        p.setPen(QPen(g, 3));
        p.drawLine(cx, cy, ex, ey);
        // 中心圆
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(50, 50, 55));
        p.drawEllipse(QPoint(cx, cy), 12, 12);
        p.setBrush(QColor(200, 200, 210));
        p.drawEllipse(QPoint(cx, cy), 6, 6);
    }

    void drawValue(QPainter &p, int cx, int cy) {
        QFont f = font();
        f.setPointSize(16); f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRect(cx - 55, cy + 30, 110, 28), Qt::AlignCenter,
                   QString::number(m_angle, 'f', 2));
        f.setPointSize(9); f.setBold(false);
        p.setFont(f);
        p.setPen(QColor(150, 150, 165));
        p.drawText(QRect(cx - 35, cy + 50, 70, 18), Qt::AlignCenter, "°");
        if (std::isfinite(m_targetAngle)) {
            p.setPen(QColor(0, 200, 255));
            p.drawText(QRect(cx - 55, cy + 60, 110, 18), Qt::AlignCenter,
                       "目标: " + QString::number(m_targetAngle, 'f', 2) + "°");
        }
    }

    double m_angle = 0.0;
    double m_minAngle = -185.0;
    double m_maxAngle = 185.0;
    double m_targetAngle = std::numeric_limits<double>::quiet_NaN();
    QString m_title = "机架角度";
    const double m_startRad = 3.14159 * 0.75;   // 135° (左下)
    const double m_spanRad  = 3.14159 * 1.5;    // 270° 跨域
    std::vector<double> m_history;
};
