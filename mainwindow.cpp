#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMediaDevices>
#include <QDebug>
#include <QTimer> ///////////////
#include <QDateTime>////////////
#include <QDir>

#include <QUdpSocket>

#define MAX_LANGUAGE_COUNT 7
QString language[] = {"英语", "日语", "韩语", "俄语", "法语", "德语", "西班牙语" };

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , is_running(false)
    //, audioTimer(new QTimer(this))
{
    ui->setupUi(this);

    config.loadFileToManager();             // 从配置文件读取配置到管理类
    applyConfigToUi();                      // 将配置从管理类应用到UI
}

MainWindow::~MainWindow(){}

void MainWindow::on_launchButton_clicked()
{
    if(is_running){
        ui->debug->append("\n结束\n");

        is_running = false;
        ui->launchButton->setText("启动!");
        emit __stop__();
    }
    else{
        if(ui->ApiKeyInput->text().isEmpty() ||
            ui->AppIdInput->text().isEmpty() ||
            ui->SecretKeyInput->text().isEmpty() ||
            ui->DeepseekApiKeyInput->text().isEmpty()
            ){
            ui->debug->append("未配置 API 信息，请访问GitHub页面或查看使用说明");
            return ;
        }

        applyUiToConfig();                  // 将UI修改应用到管理类
        config.loadManagerToFile();         // 将管理类里面的配置写入配置文件（注意！要先更新配置再启动）
        is_running = true;

        ui->launchButton->setText("停止");
        ui->debug->clear();
        ui->debug->append("程序启动\n正在使用设备"+config.getDevice());

        emit __start__();                   // 启动  （注意！要先更新配置再启动）
    }
}

// 从ConfigManager初始化UI
void MainWindow::applyConfigToUi(){
    int tmpId = 0;
    QList<QAudioDevice> devicelist = QMediaDevices::audioInputs();
    for (int i=0;i<devicelist.size();i++) {
        ui->deviceCombo->addItem(devicelist[i].description());
        if(devicelist[i].isDefault())
            tmpId = i;
    }
    ui->deviceCombo->setCurrentIndex(tmpId);
    ui->ApiKeyInput->setText(config.getXunFeiApiKey());
    ui->SecretKeyInput->setText(config.getXunFeiApiSecret());
    ui->AppIdInput->setText(config.getXunFeiAppId());
    ui->DeepseekApiKeyInput->setText(config.getDeepseekApiKey());
    ui->oscHostInput->setText(config.getTargetHost());
    ui->oscPortInput->setText(QString::number(config.getTargetPort()));
    ui->silentTimeInput->setText(QString::number(config.getMinSilenceDuration()));
    ui->vadInput->setText(QString::number(config.getVadThreshold()*100));
    for(int i=0;i<MAX_LANGUAGE_COUNT;i++)
        if(config.getTargetLanguage()[0] == language[i][0])
            tmpId = i;
    ui->languageCombo->setCurrentIndex(tmpId);
}

// 将UI应用到ConfigManager
void MainWindow::applyUiToConfig(){
    config.setDevice(ui->deviceCombo->currentText());
    config.setXunFeiApiKey(ui->ApiKeyInput->text());
    config.setXunFeiApiSecret(ui->SecretKeyInput->text());
    config.setXunFeiAppId(ui->AppIdInput->text());
    config.setDeepseekApiKey(ui->DeepseekApiKeyInput->text());
    config.setTargetHost(ui->oscHostInput->text());
    config.setTargetPort(ui->oscPortInput->text().toInt());
    config.setMinSilenceDuration(ui->silentTimeInput->text().toInt());
    config.setVadThreshold(ui->vadInput->text().toFloat()/100);
    config.setTargetLanguage(ui->languageCombo->currentText());
}

void MainWindow::onError(const QString& errorMessage){
    qDebug() << errorMessage;
    ui->debug->append("[ERROR]" + errorMessage);
}

void MainWindow::onDebug(const QString& debugMessage){
    ui->debug->append(debugMessage);
}
