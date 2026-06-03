#include <QApplication>
#include "main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("PGM Gantry Frontend");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("PGM");

    // 全局暗色调色板
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(26, 26, 32));
    darkPalette.setColor(QPalette::WindowText, QColor(208, 208, 216));
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 36));
    darkPalette.setColor(QPalette::AlternateBase, QColor(38, 38, 46));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(50, 50, 58));
    darkPalette.setColor(QPalette::ToolTipText, QColor(208, 208, 216));
    darkPalette.setColor(QPalette::Text, QColor(208, 208, 216));
    darkPalette.setColor(QPalette::Button, QColor(40, 40, 48));
    darkPalette.setColor(QPalette::ButtonText, QColor(208, 208, 216));
    darkPalette.setColor(QPalette::BrightText, QColor(255, 80, 60));
    darkPalette.setColor(QPalette::Link, QColor(80, 160, 255));
    darkPalette.setColor(QPalette::Highlight, QColor(60, 100, 200));
    darkPalette.setColor(QPalette::HighlightedText, QColor(240, 240, 248));
    app.setPalette(darkPalette);

    MainWindow w;
    w.show();

    return app.exec();
}
