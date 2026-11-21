#ifdef _WIN32
#  include <winsock2.h>
#endif

#include "mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QtWidgets>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QByteArray>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#  include <winsock2.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

int main(int argc, char *argv[])
{
    // Enable OpenWV's Rust diagnostics and capture process output for the Okko tab.
    qputenv("RUST_LOG", "info");
#ifndef Q_OS_WIN
    int logPipe[2] = {-1, -1};
    if (::pipe(logPipe) == 0) {
        const int flags = ::fcntl(logPipe[0], F_GETFL, 0);
        if (flags >= 0)
            ::fcntl(logPipe[0], F_SETFL, flags | O_NONBLOCK);

        if (::dup2(logPipe[1], STDOUT_FILENO) >= 0 &&
            ::dup2(logPipe[1], STDERR_FILENO) >= 0) {
            ::close(logPipe[1]);
            qputenv("KODIGUI_LOG_READ_FD", QByteArray::number(logPipe[0]));
        } else {
            ::close(logPipe[0]);
            ::close(logPipe[1]);
        }
    }
#else
    // Windows builds keep diagnostics on the normal process streams. The POSIX
    // non-blocking pipe used by the embedded OpenWV log viewer is Linux-only.
    qInfo() << "[OpenWV] stdout/stderr pipe capture is unavailable on Windows.";
    WSADATA winsockData{};
    const int winsockResult = WSAStartup(MAKEWORD(2, 2), &winsockData);
    if (winsockResult != 0) {
        qCritical() << "WSAStartup failed:" << winsockResult;
        return EXIT_FAILURE;
    }
#endif

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
#ifdef Q_OS_WIN
        widevinePath = QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation)).filePath(
                QStringLiteral("widevine/widevinecdm.dll"));
#else
        widevinePath = QDir::home().filePath(
            QStringLiteral(".local/share/KodiGui/widevine/libwidevinecdm.so"));
#endif
    }

    const QFileInfo widevineInfo(widevinePath);
    if (widevineInfo.isFile() && widevineInfo.isReadable()) {
        widevinePath.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        widevinePath.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        chromiumFlags += QStringLiteral(" --widevine-path=\"%1\"").arg(widevinePath);
        qInfo().noquote() << "[Widevine] CDM configured:"
                          << widevineInfo.absoluteFilePath();
    } else {
        qInfo().noquote() << "[Widevine] CDM not found; DRM playback is unavailable."
                          << "Expected:" << widevineInfo.absoluteFilePath()
                          << "or set KODIGUI_WIDEVINE_PATH.";
    }

    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags.toLocal8Bit());
#ifndef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "xcb");
#endif
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

    const int result = app.exec();
#ifdef Q_OS_WIN
    WSACleanup();
#endif
    return result;
}
