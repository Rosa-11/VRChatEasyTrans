#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMediaDevices>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QDir>

#include <QUdpSocket>

QString language[] = {"英语", "日语", "韩语", "俄语", "法语", "德语", "西班牙语" };

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , is_running(false)
    , config(ConfigManager::instance())     // 配置类静态实例引用
    , audioTimer(new QTimer(this))
    , capture(new AudioCapture(this))
    , translator(new Translator(this))

{
    ui->setupUi(this);

    config.loadFileToManager();             // 从配置文件读取配置到管理类

    applyConfigToUi();                      // 将配置从管理类应用到UI

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
        QString result = recogniser->recognizeSpeech(*(capture->getBuffer()), config.getSampleRate(), 1);

        if (result.isEmpty()) {
            // qDebug() << "mainwindow.cpp: empty Recogntion result";
            ui->debug->append("请调整静音阈值以减少杂音，确保讲普通话");
            sendToOSC("喵~✨");
        }
        else {
            sendToOSC(result);
            QString finaltext = translator->translateText(result, language[config.getTargetLanguage()]);
            if(finaltext.isEmpty()){
                // qDebug() << "mainwindow.cpp: empty Translation result";
                ui->debug->append("Deepseek 表示不会翻译\n");
                finaltext = "(...)";
            }
            sendToOSC(result + "\n" + finaltext);
        }
    }
    else {
        // qDebug() << "no audio detected";
        ui->debug->append("讲话声音大一点...\n");
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
        if(ui->ApiKeyInput->text().isEmpty() ||
            ui->AppIdInput->text().isEmpty() ||
            ui->SecretKeyInput->text().isEmpty() ||
            ui->DeepseekApiKeyInput->text().isEmpty()
            ){
            ui->debug->append("未配置 API 信息，请访问GitHub页面或查看使用说明（辛辛苦苦写的文档怎么没人看QAQ）");
            return ;
        }

        applyUiToConfig();                  // 将UI修改应用到管理类

        config.loadManagerToFile();         // 将管理类里面的配置写入配置文件

        recogniser = new SpeechRecogniser(this);

        is_running = true;
        ui->launchButton->setText("停止");
        ui->debug->clear();
        ui->debug->append("程序启动\n正在使用设备"+QMediaDevices::audioInputs()[config.audioDeviceId].description());

        processAudio();
    }
}

void MainWindow::sendToOSC(const QString& text)
{
    const QString oscAddress = "/chatbox/input";
    const QHostAddress targetHost(config.getTargetHost());
    const quint16 targetPort = config.getTargetPort();

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
        // qWarning() << "OSC发送失败：" << udpSocket.errorString();
        ui->debug->append( "OSC发送失败：" + udpSocket.errorString());
    } else {
        // qWarning() << "OSC发送成功：" << text << "（字节数：" << bytesSent << "）";
        ui->debug->append( "OSC：" + text);
    }
}

// 从ConfigManager初始化UI
void MainWindow::applyConfigToUi(){
    QList<QAudioDevice> devicelist = QMediaDevices::audioInputs();
    for(QAudioDevice i : devicelist){
        ui->deviceCombo->addItem(i.description());
    }
    ui->ApiKeyInput->setText(config.getXunFeiApiKey());
    ui->SecretKeyInput->setText(config.getXunFeiApiSecret());
    ui->AppIdInput->setText(config.getXunFeiAppId());
    ui->DeepseekApiKeyInput->setText(config.getDeepseekApiKey());
    ui->oscHostInput->setText(config.getTargetHost());
    ui->oscPortInput->setText(QString::number(config.getTargetPort()));
    ui->silentTimeInput->setText(QString::number(config.getMinSilenceDuration()));
    ui->vadInput->setText(QString::number(config.getVadThreshold()*100));
    ui->languageCombo->setCurrentIndex(config.getTargetLanguage());
    ui->deviceCombo->setCurrentIndex(config.getAudioDeviceId());
}

// 将UI应用到ConfigManager
void MainWindow::applyUiToConfig(){
    config.xunFeiApiKey = ui->ApiKeyInput->text();
    config.xunFeiApiSecret = ui->SecretKeyInput->text();
    config.xunFeiAppId = ui->AppIdInput->text();
    config.DeepseekApiKey = ui->DeepseekApiKeyInput->text();
    config.targetHost = ui->oscHostInput->text();
    config.targetPort = ui->oscPortInput->text().toInt();
    config.minSilenceDuration = ui->silentTimeInput->text().toInt();
    config.vadThreshold = ui->vadInput->text().toFloat()/100;
    config.targetLanguage = ui->languageCombo->currentIndex();
    config.audioDeviceId = ui->deviceCombo->currentIndex();
}
