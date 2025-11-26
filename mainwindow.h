#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_horizontalSlider_valueChanged(int value);

    QString on_lineEdit_textChanged();

    void on_pushButton_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_3_clicked();

    void on_pushButton_4_clicked();

    void on_pushButton_6_clicked();

    void on_pushButton_5_clicked();

    QString  on_lineEdit_2_textChanged();

    void on_pushButton_7_clicked();

    void on_pushButton_9_clicked();

    void on_pushButton_8_clicked();

     QString  on_lineEdit_3_textChanged();



     void on_pushButton_10_clicked();

     void on_pushButton_11_clicked();

     void on_pushButton_12_clicked();

     void on_pushButton_13_clicked();

     void on_pushButton_14_clicked();

     void on_pushButton_clearurl_clicked();



 private:
    Ui::MainWindow *ui;
};
#endif // MAINWINDOW_H
