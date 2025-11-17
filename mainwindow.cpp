#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "audiocapture.h"
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QDir>

#include <QUdpSocket>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , is_running(false)
    , config(ConfigManager::instance())     // 配置类静态实例引用
    , audioTimer(new QTimer(this))
    , capture(new AudioCapture(0, this))
    , translator(new Translator(this))

{
    ui->setupUi(this);

    connect(audioTimer, &QTimer::timeout, this, &MainWindow::processAudio);
}

MainWindow::~MainWindow()
{
    delete ui;
    if (capture) {
        delete capture;
    }
}

void MainWindow::processAudio()
{
    if (!is_running || !capture)
        return;

    audioTimer->stop();

    bool caped = capture->cap();

    if (!is_running) {
        return;
    }

    if (caped) {
        // 网络IO得到识别文本
        QString result = recogniser->recognizeSpeech(*(capture->getBuffer()), 16000, 1);

        if (result.isEmpty()) {
            qDebug() << "mainwindow.cpp: empty Recogntion result";
            sendToOSC("(...)");
        }
        else {
            sendToOSC(result);
            QString finaltext = translator->translateText(result, config.targetLanguage);
            if(finaltext.isEmpty()){
                qDebug() << "mainwindow.cpp: empty Translation result";
                finaltext = "(...)";
            }
            sendToOSC(result + "\n" + finaltext);
        }
    }
    else {
        qDebug() << "no audio detected";
    }

    if (is_running) {
        audioTimer->start();
    }
}

void MainWindow::on_launchButton_clicked()
{
    if(is_running){
        if(recogniser)
            delete recogniser;
        is_running = false;
        audioTimer->stop();
        ui->launchButton->setText("启动!");

        if (capture) {
            capture->stop();
        }
    }
    else{

        // TODO：重新从配置文件加载配置到配置类

        recogniser = new SpeechRecogniser(this);

        is_running = true;
        ui->launchButton->setText("停止");

        processAudio();
    }
}

void MainWindow::sendToOSC(const QString& text)
{
    // VRChat默认OSC端口（9000）和本地地址
    const QString oscAddress = "/chatbox/input";
    const QHostAddress targetHost("127.0.0.1");
    const quint16 targetPort = 9000;

    // OSC协议格式：地址 + 类型标签 + 数据）
    QByteArray oscData;

    // 地址
    oscData.append(oscAddress.toUtf8());
    oscData.append('\0');   // 类型标签结束符

    // OSC协议要求补齐4字节对齐
    while (oscData.size() % 4 != 0) {
        oscData.append('\0');
    }

    // 类型标签,",sT" 表示：字符串 + 布尔值true
    oscData.append(",sT");
    oscData.append('\0');  // 类型标签结束符
    while (oscData.size() % 4 != 0) {
        oscData.append('\0');
    }

    // 字符串数据,null结尾，4字节对齐
    QByteArray textBytes = text.toUtf8();
    oscData.append(textBytes);
    oscData.append('\0');  // 字符串结束符
    while (oscData.size() % 4 != 0) {
        oscData.append('\0');
    }

    // 发送UDP数据
    QUdpSocket udpSocket;
    qint64 bytesSent = udpSocket.writeDatagram(oscData, targetHost, targetPort);

    if (bytesSent == -1) {
        qWarning() << "OSC发送失败：" << udpSocket.errorString();
    } else {
        qDebug() << "OSC发送成功：" << text << "（字节数：" << bytesSent << "）";
    }
}
