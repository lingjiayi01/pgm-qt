#include <QApplication>
#include "login_dialog.h"
#include "main_window.h"

namespace {
void applyDarkPalette(QApplication &app) {
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
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("PGM Gantry Frontend");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("PGM");

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    applyDarkPalette(app);

    const bool windowed = app.arguments().contains(QStringLiteral("--windowed"));

    for (;;) {
        LoginDialog loginDlg;
        if (loginDlg.exec() != QDialog::Accepted)
            return 0;

        MainWindow w;
        w.applyAuthSession(loginDlg.session());

        if (windowed)
            w.show();
        else
            w.showMaximized();

        const int code = app.exec();
        if (code != kReloginExitCode)
            return code;
    }
}
