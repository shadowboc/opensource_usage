#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow), m_client(nullptr)
{
    qDebug() << "[MainWindow] enter, Qthread=" << QThread::currentThread();
    ui->setupUi(this);
    m_client = new ClientWorker();
    m_client->moveToThread(&m_worker);
    connect(&m_worker, &QThread::finished, m_client, &QObject::deleteLater);
    connect(this, &MainWindow::startClient, m_client, &ClientWorker::onStartClient, Qt::BlockingQueuedConnection);
    connect(this, &MainWindow::stopClient, m_client, &ClientWorker::onStopClient, Qt::BlockingQueuedConnection);
    m_worker.start();
}

MainWindow::~MainWindow()
{
    delete ui;
    m_worker.quit();
    m_worker.wait();
    m_client = nullptr;
}


void MainWindow::on_pushButton_clicked()
{
    if (m_client) {
        emit startClient();
    } else {

    }

}

void MainWindow::on_pushButton_2_clicked()
{
    if (m_client) {
        emit stopClient();
    } else {

    }
}
