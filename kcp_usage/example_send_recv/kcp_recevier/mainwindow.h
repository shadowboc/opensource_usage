#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "clientWorker.h"
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void startClient();
private slots:
    void on_pushButton_clicked();

private:
    Ui::MainWindow *ui;
    ClientWorker* m_client;
    QThread m_worker;
};
#endif // MAINWINDOW_H
