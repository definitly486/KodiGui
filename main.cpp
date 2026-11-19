#include "mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QtWidgets>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

int main(int argc, char *argv[])
{
    qputenv("QTWEBENGINE_DISABLE_GPU", "1");

    QString chromiumFlags = QStringLiteral(
        "--disable-gpu "
        "--disable-gpu-compositing "
        "--disable-software-rasterizer "
        "--disable-features=UseOzonePlatform");

    // Qt WebEngine loads an external Widevine CDM using --widevine-path.
    // OpenWV is built separately; its device identity must remain local.
    QString widevinePath = qEnvironmentVariable("KODIGUI_WIDEVINE_PATH");
    if (widevinePath.isEmpty()) {
        widevinePath = QDir::home().filePath(
            QStringLiteral(".local/share/KodiGui/widevine/libwidevinecdm.so"));
    }

    const QFileInfo widevineInfo(widevinePath);
    if (widevineInfo.isFile() && widevineInfo.isReadable()) {
        widevinePath.replace(QStringLiteral("\\\\"), QStringLiteral("\\\\\\\\"));
        widevinePath.replace(QStringLiteral("\\\""), QStringLiteral("\\\\\""));
        chromiumFlags += QStringLiteral(" --widevine-path=\\\"%1\\\"").arg(widevinePath);
        qInfo().noquote() << "[Widevine] CDM configured:"
                          << widevineInfo.absoluteFilePath();
    } else {
        qInfo().noquote() << "[Widevine] CDM not found; DRM playback is unavailable."
                          << "Expected:" << widevineInfo.absoluteFilePath()
                          << "or set KODIGUI_WIDEVINE_PATH.";
    }

    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags.toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("QT_DISABLE_VULKAN", "1");
    qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv);
    MainWindow w;
    w.show();

    const auto primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen)
        return EXIT_FAILURE;

    const QRect availableGeometry = primaryScreen->availableGeometry();
    const int screenWidth = availableGeometry.width();
    const int screenHeight = availableGeometry.height();
    const int windowWidth = w.frameGeometry().width();
    const int windowHeight = w.frameGeometry().height();
    w.move((screenWidth - windowWidth) / 2, (screenHeight - windowHeight) / 2);

    return app.exec();
}
