#include "mainwindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QtWidgets>

int main(int argc, char *argv[])
{

qputenv("QTWEBENGINE_DISABLE_GPU", "1");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--disable-gpu "
            "--disable-gpu-compositing "
            "--disable-software-rasterizer "
            "--disable-features=UseOzonePlatform");

    qputenv("QT_QPA_PLATFORM", "xcb");      // fallback с Wayland
    qputenv("QT_DISABLE_VULKAN", "1");      // на всякий случай
    qputenv("QT_QUICK_BACKEND", "software"); 

    QApplication app(argc, argv); // переименовал переменную в app для ясности
    
    MainWindow w;
    w.show(); // показываем окно ДО вычисления размеров окна
    
    // Получение размеров экрана с использованием QScreen
    const auto primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen)
        return EXIT_FAILURE; // защита от ситуации отсутствия экранов
        
    const QRect availableGeometry = primaryScreen->availableGeometry();
    int screenWidth = availableGeometry.width();
    int screenHeight = availableGeometry.height();
    
    // Теперь получаем размер видимого окна после отображения (необходимый шаг!)
    int windowWidth = w.frameGeometry().width();
    int windowHeight = w.frameGeometry().height();
    
    // Центрирование окна относительно центра экрана
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;
    w.move(x, y);
    
    return app.exec();
}