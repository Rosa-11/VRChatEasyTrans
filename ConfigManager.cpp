#include "ConfigManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QMutexLocker>

// 全局唯一锁初始化
QMutex ConfigManager::m_globalMutex;

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

// ===================== 读配置文件（自动加锁） =====================
void ConfigManager::loadFileToManager() {
    QMutexLocker locker(&m_globalMutex);

    QDir exeDir(QCoreApplication::applicationDirPath());
    QString configPath = exeDir.absoluteFilePath("config.ini");
    QSettings settings(configPath, QSettings::IniFormat);

    m_vadThreshold       = settings.value("vadThreshold", 0.015).toDouble();
    m_minSilenceDuration = settings.value("minSilenceDuration", 800).toInt();
    m_targetPort         = settings.value("targetPort", 9000).toInt();
    m_targetHost         = settings.value("targetHost", "127.0.0.1").toString();
    m_xunFeiAppId        = settings.value("xunFeiAppId", "").toString();
    m_xunFeiApiSecret    = settings.value("xunFeiApiSecret", "").toString();
    m_xunFeiApiKey       = settings.value("xunFeiApiKey", "").toString();
    m_DeepseekApiKey     = settings.value("DeepseekApiKey", "").toString();
    m_targetLanguage     = settings.value("targetLanguage", "").toString();
    m_device             = settings.value("device", "").toString();
}

// ===================== 写配置文件（自动加锁） =====================
void ConfigManager::loadManagerToFile() {
    QMutexLocker locker(&m_globalMutex);

    QDir exeDir(QCoreApplication::applicationDirPath());
    QString configPath = exeDir.absoluteFilePath("config.ini");
    QSettings settings(configPath, QSettings::IniFormat);

    settings.setValue("vadThreshold", m_vadThreshold);
    settings.setValue("minSilenceDuration", m_minSilenceDuration);
    settings.setValue("targetPort", m_targetPort);
    settings.setValue("targetHost", m_targetHost);
    settings.setValue("xunFeiAppId", m_xunFeiAppId);
    settings.setValue("xunFeiApiSecret", m_xunFeiApiSecret);
    settings.setValue("xunFeiApiKey", m_xunFeiApiKey);
    settings.setValue("DeepseekApiKey", m_DeepseekApiKey);
    settings.setValue("targetLanguage", m_targetLanguage);
    settings.setValue("device", m_device);
    settings.sync();
}

// ===================== 自动加锁 GET / SET =====================
double ConfigManager::getVadThreshold() const {
    QMutexLocker locker(&m_globalMutex);
    return m_vadThreshold;
}
void ConfigManager::setVadThreshold(double value) {
    QMutexLocker locker(&m_globalMutex);
    m_vadThreshold = value;
}

int ConfigManager::getMinSilenceDuration() const {
    QMutexLocker locker(&m_globalMutex);
    return m_minSilenceDuration;
}
void ConfigManager::setMinSilenceDuration(int value) {
    QMutexLocker locker(&m_globalMutex);
    m_minSilenceDuration = value;
}

int ConfigManager::getTargetPort() const {
    QMutexLocker locker(&m_globalMutex);
    return m_targetPort;
}
void ConfigManager::setTargetPort(int value) {
    QMutexLocker locker(&m_globalMutex);
    m_targetPort = value;
}

QString ConfigManager::getTargetHost() const {
    QMutexLocker locker(&m_globalMutex);
    return m_targetHost;
}
void ConfigManager::setTargetHost(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_targetHost = value;
}

QString ConfigManager::getXunFeiAppId() const {
    QMutexLocker locker(&m_globalMutex);
    return m_xunFeiAppId;
}
void ConfigManager::setXunFeiAppId(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_xunFeiAppId = value;
}

QString ConfigManager::getXunFeiApiSecret() const {
    QMutexLocker locker(&m_globalMutex);
    return m_xunFeiApiSecret;
}
void ConfigManager::setXunFeiApiSecret(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_xunFeiApiSecret = value;
}

QString ConfigManager::getXunFeiApiKey() const {
    QMutexLocker locker(&m_globalMutex);
    return m_xunFeiApiKey;
}
void ConfigManager::setXunFeiApiKey(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_xunFeiApiKey = value;
}

QString ConfigManager::getDeepseekApiKey() const {
    QMutexLocker locker(&m_globalMutex);
    return m_DeepseekApiKey;
}
void ConfigManager::setDeepseekApiKey(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_DeepseekApiKey = value;
}

QString ConfigManager::getTargetLanguage() const {
    QMutexLocker locker(&m_globalMutex);
    return m_targetLanguage;
}
void ConfigManager::setTargetLanguage(QString value) {
    QMutexLocker locker(&m_globalMutex);
    m_targetLanguage = value;
}

QString ConfigManager::getDevice() const {
    QMutexLocker locker(&m_globalMutex);
    return m_device;
}
void ConfigManager::setDevice(const QString& value) {
    QMutexLocker locker(&m_globalMutex);
    m_device = value;
}
int ConfigManager::getSampleRate() {
    QMutexLocker locker(&m_globalMutex);
    return 16000;
}
