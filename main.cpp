#include "mainwindow.h"

#include <QApplication>
#include <QtWidgets>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    int screenWidth = QApplication::desktop()->screenGeometry().width();
    int screenHeight = QApplication::desktop()->screenGeometry().height();
    int windowWidth = w.width();
    int windowHeight = w.height();
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;
    w.move(x, y);
    w.show();
    return a.exec();
}
