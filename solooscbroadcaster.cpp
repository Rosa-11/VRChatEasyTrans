#include "solooscbroadcaster.h"
#include <QDebug>
#include <QUdpSocket>
#include "ConfigManager.h"

SoloOscBroadcaster::SoloOscBroadcaster() {}

void SoloOscBroadcaster::initialize(){
    qDebug() << "OSC initialize";
    ConfigManager& config = ConfigManager::getInstance();
    targetHost = QHostAddress(config.getTargetHost());
    targetPort = (quint16)config.getTargetPort();
}
void SoloOscBroadcaster::sendToOSC(const QString& text)
{
    const QString oscAddress = "/chatbox/input";

    // OSC协议格式：地址路径 + 类型标签 + 数据）
    QByteArray oscData;

    // 地址路径
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
}
