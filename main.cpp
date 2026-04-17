#include "mainwindow.h"
#include "ConfigManager.h"
#include "AudioCapture.h"
#include "speechrecogniser.h"
#include "translator.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QMediaDevices>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator qTranslator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "VRChatEasyTrans-AI_" + QLocale(locale).name();
        if (qTranslator.load(":/i18n/" + baseName)) {
            a.installTranslator(&qTranslator);
            break;
        }
    }

    ConfigManager::getInstance().loadFileToManager();

    // 2. 创建主窗口（UI线程）
    MainWindow w;

    // 3. 创建音频采集模块（主线程，需要访问硬件）
    AudioCapture audioCapture;
    if (!audioCapture.initialize()) {
        return -1;
    }

    // 4. 创建语音识别模块（子线程）
    QThread recogniserThread;
    SpeechRecogniser recogniser;
    recogniser.moveToThread(&recogniserThread);

    // 5. 创建翻译模块（子线程，可与识别共用或独立线程）
    QThread translatorThread;
    Translator translator;
    translator.moveToThread(&translatorThread);

    // ========== 信号槽连接 ==========

    // 5.1 音频采集 → 语音识别
    QObject::connect(&audioCapture, &AudioCapture::audioDataReady,
                     &recogniser, &SpeechRecogniser::processAudioData);
    QObject::connect(&audioCapture, &AudioCapture::recordingFinished,
                     &recogniser, &SpeechRecogniser::finishRecognition);

    // 5.2 语音识别 → 翻译（异步）
    QObject::connect(&recogniser, &SpeechRecogniser::recognitionCompleted,
                     &translator, &Translator::translateTextAsync);

    // 5.4 错误处理（可选）
    QObject::connect(&audioCapture, &AudioCapture::error,
                     &w, &MainWindow::onError);
    QObject::connect(&recogniser, &SpeechRecogniser::error,
                     &w, &MainWindow::onError);
    QObject::connect(&translator, &Translator::translationError,
                     &w, &MainWindow::onError);

    QObject::connect(&w, &MainWindow::startRecordingRequested,
                     &audioCapture, &AudioCapture::start);
    QObject::connect(&w, &MainWindow::stopRecordingRequested,
                     &audioCapture, &AudioCapture::stop);

    // 6. 启动子线程并初始化工作对象
    recogniserThread.start();
    translatorThread.start();

    // 在工作线程中执行初始化（确保对象在正确线程中创建资源）
    QTimer::singleShot(0, &recogniser, &SpeechRecogniser::initialize);
    // Translator 无需显式初始化，构造时已创建 NetworkAccessManager

    // 7. 显示窗口
    w.show();

    // 8. 程序退出时清理
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&]() {
        recogniserThread.quit();
        translatorThread.quit();
        recogniserThread.wait();
        translatorThread.wait();
    });

    return a.exec();
}
