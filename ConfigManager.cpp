#include "ConfigManager.h"
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QMediaDevices>
#include <QDir>
#include <QCoreApplication>

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{}

ConfigManager::~ConfigManager(){}
ConfigManager& ConfigManager::instance()
{
    static ConfigManager instance;
    return instance;
}

void ConfigManager::loadFileToManager(){
    // 设置配置文件路径
    QDir exeDir(QCoreApplication::applicationDirPath());
    QString configPath = exeDir.absoluteFilePath("config.ini");

    QSettings settings(configPath, QSettings::IniFormat);

    vadThreshold = settings.value("vadThreshold", 0.015).toDouble();
    minSilenceDuration = settings.value("minSilenceDuration", 800).toInt();
    targetPort = settings.value("targetPort", 9000).toInt();
    targetHost = settings.value("targetHost", "127.0.0.1").toString();
    xunFeiAppId = settings.value("xunFeiAppId", "").toString();
    xunFeiApiSecret = settings.value("xunFeiApiSecret", "").toString();
    xunFeiApiKey = settings.value("xunFeiApiKey", "").toString();
    DeepseekApiKey = settings.value("DeepseekApiKey", "").toString();
    targetLanguage = settings.value("targetLanguage", 0).toInt();

    audioDeviceId = settings.value("audioDeviceId", 0).toString();

}

void ConfigManager::loadManagerToFile(){
    // 设置配置文件路径
    QDir exeDir(QCoreApplication::applicationDirPath());
    QString configPath = exeDir.absoluteFilePath("config.ini");

    QSettings settings(configPath, QSettings::IniFormat);

    settings.setValue("vadThreshold", vadThreshold);
    settings.setValue("minSilenceDuration", minSilenceDuration);
    settings.setValue("targetPort", targetPort);
    settings.setValue("targetHost", targetHost);
    settings.setValue("xunFeiAppId", xunFeiAppId);
    settings.setValue("xunFeiApiSecret", xunFeiApiSecret);
    settings.setValue("xunFeiApiKey", xunFeiApiKey);
    settings.setValue("DeepseekApiKey", DeepseekApiKey);
    settings.setValue("targetLanguage", targetLanguage);

    settings.setValue("audioDeviceId", audioDeviceId);

    settings.sync();
}
