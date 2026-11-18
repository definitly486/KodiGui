#include "mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QtWidgets>

int main(int argc, char *argv[])
{

    // Keep GPU acceleration disabled on systems where Chromium GPU startup
    // is unreliable, but leave Chromium's software rasterizer available.
    qputenv("QTWEBENGINE_DISABLE_GPU", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--disable-gpu "
            "--disable-gpu-compositing "
            "--disable-features=UseOzonePlatform");

    qputenv("QT_QPA_PLATFORM", "xcb");      // fallback с Wayland
    qputenv("QT_DISABLE_VULKAN", "1");      // на всякий случай
    qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv); // переименовал переменную в app для ясности

    MainWindow w;
    // Use the available screen width so the tab captions are visible on startup.
    // QTabBar keeps its scroll buttons as a fallback on small displays.
    w.showMaximized();

    return app.exec();
}
