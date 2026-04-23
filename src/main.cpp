#include "MainWindow.h"

#include <clocale>
#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QtGlobal>

int main(int argc, char* argv[])
{
    // libmpv requires C numeric locale, otherwise mpv_create can fail.
    qputenv("LC_NUMERIC", QByteArrayLiteral("C"));
    std::setlocale(LC_NUMERIC, "C");

    // Respect session default platform plugin. Forcing xcb can cause
    // input/focus issues on display hotplug in mixed DPI setups.

    // Avoid fractional-scale coordinate drift on monitor hotplug.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qreolink"));
    app.setDesktopFileName(QStringLiteral("qreolink"));
    QLocale::setDefault(QLocale::c());
    std::setlocale(LC_NUMERIC, "C");

    MainWindow window;
    const QIcon appIcon = QIcon::fromTheme(QStringLiteral("qreolink"));
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
        window.setWindowIcon(appIcon);
    }
    window.show();

    return QApplication::exec();
}
