#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QSettings>
#include <QObject>
#include <QVariant>
#include <QAudioDevice>

class ConfigManager : public QObject
{
    Q_OBJECT
    friend class MainWindow;

public:
    explicit ConfigManager(QObject *parent = nullptr);
    ~ConfigManager();

    // 单例模式访问
    static ConfigManager& instance();

    void loadFileToManager();
    void loadManagerToFile();

    // getter方法
    QAudioDevice getAudioDevice() const { return device; }

    QString getXunFeiAppId() const { return xunFeiAppId; }
    QString getXunFeiApiKey() const { return xunFeiApiKey; }
    QString getXunFeiApiSecret() const { return xunFeiApiSecret; }

    QString getDeepseekApiKey() const { return DeepseekApiKey; }

    QString getAudioDeviceId() const { return audioDeviceId; }
    float getVadThreshold() const { return vadThreshold; }
    int getMinSilenceDuration() const {return minSilenceDuration; }
    int getSampleRate() const { return 16000; }         // 采样率必须是16000，不设置成员变量

    QString getTargetHost() const { return targetHost; }
    quint16 getTargetPort() const { return targetPort; }

    int getTargetLanguage() const { return targetLanguage; }

signals:
    void configChanged(const QString& key, const QVariant& value);

private:

    // 声音录制设置
    QAudioDevice device;
    QString audioDeviceId;  // 设备描述（用于记录）

    float vadThreshold;        // 0~1
    int minSilenceDuration;    // 单位：ms

    // VRChat OSC 配置
    QString targetHost;
    quint16 targetPort;

    // API 配置
    QString xunFeiAppId;      // 讯飞App ID
    QString xunFeiApiKey;     // 讯飞API Key
    QString xunFeiApiSecret;  // 讯飞API Secret

    QString DeepseekApiKey;   // Deepseek API Key

    int targetLanguage;   // 目标翻译语言

};

#endif // CONFIGMANAGER_H
