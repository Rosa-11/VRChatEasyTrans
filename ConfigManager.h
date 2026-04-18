#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QMutex>
#include <QAudioDevice>

class ConfigManager : public QObject
{
    Q_OBJECT

private:
    explicit ConfigManager(QObject *parent = nullptr);

    // 全局唯一静态锁
    static QMutex m_globalMutex;

    double m_vadThreshold;
    int    m_minSilenceDuration;
    int    m_targetPort;
    QString m_targetHost;
    QString m_xunFeiAppId;
    QString m_xunFeiApiSecret;
    QString m_xunFeiApiKey;
    QString m_DeepseekApiKey;
    QString m_targetLanguage;
    QString m_device;

    int     sampleRate;

public:
    static ConfigManager& getInstance();

    // 禁止拷贝
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // 配置读写
    void loadFileToManager();
    void loadManagerToFile();

    // ===================== 自动加锁的安全 get/set =====================
    double getVadThreshold() const;
    void setVadThreshold(double value);

    int getMinSilenceDuration() const;
    void setMinSilenceDuration(int value);

    int getTargetPort() const;
    void setTargetPort(int value);

    QString getTargetHost() const;
    void setTargetHost(const QString& value);

    QString getXunFeiAppId() const;
    void setXunFeiAppId(const QString& value);

    QString getXunFeiApiSecret() const;
    void setXunFeiApiSecret(const QString& value);

    QString getXunFeiApiKey() const;
    void setXunFeiApiKey(const QString& value);

    QString getDeepseekApiKey() const;
    void setDeepseekApiKey(const QString& value);

    QString getTargetLanguage() const;
    void setTargetLanguage(QString value);

    QString getDevice() const;
    void setDevice(const QString& value);

    int getSampleRate() const;
};

#endif
