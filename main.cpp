#include "mainwindow.h"
#include "ConfigManager.h"
#include "AudioCapture.h"
#include "speechrecogniser.h"
#include "translator.h"
#include "solooscbroadcaster.h"

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

    // 必须先创建 QApplication，因为 ConfigManager 需要访问 applicationDirPath()
    ConfigManager::getInstance().loadFileToManager();   //懒汉模式初始化 ConfigManager

    MainWindow w;

    // 其他类必须在 ConfigManager 初始化之后创建
    AudioCapture audioCapture;

    QThread recogniserThread;
    SpeechRecogniser recogniser;
    recogniser.moveToThread(&recogniserThread);

    QThread translatorThread;
    Translator translator;
    translator.moveToThread(&translatorThread);

    QThread oscThread;
    SoloOscBroadcaster oscBroadcaster;
    oscBroadcaster.moveToThread(&translatorThread);

    // ========== 信号与槽 ==========

    // 音频采集 → 语音识别
    QObject::connect(&audioCapture, &AudioCapture::audioDataReady,
                     &recogniser, &SpeechRecogniser::processAudioData);
    QObject::connect(&audioCapture, &AudioCapture::recordingFinished,
                     &recogniser, &SpeechRecogniser::finishRecognition);

    // 语音识别 → 翻译
    QObject::connect(&recogniser, &SpeechRecogniser::recognitionCompleted,
                     &translator, &Translator::translateTextAsync);

    // 翻译 → OSC
    QObject::connect(&translator, &Translator::translationFinished,
                     &oscBroadcaster, &SoloOscBroadcaster::sendToOSC);

    // 错误处理
    QObject::connect(&audioCapture, &AudioCapture::error,
                     &w, &MainWindow::onError);
    QObject::connect(&recogniser, &SpeechRecogniser::error,
                     &w, &MainWindow::onError);
    QObject::connect(&translator, &Translator::translationError,
                     &w, &MainWindow::onError);

    QObject::connect(&audioCapture, &AudioCapture::debug,
                     &w, &MainWindow::onDebug);
    QObject::connect(&recogniser, &SpeechRecogniser::debug,
                     &w, &MainWindow::onDebug);
    QObject::connect(&translator, &Translator::debug,
                     &w, &MainWindow::onDebug);

    // 启动时初始化/更新配置到各线程，AudioCapture初始化后调用start
    QObject::connect(&w, &MainWindow::__start__,
                     &audioCapture, &AudioCapture::initializeAndStart);
    QObject::connect(&w, &MainWindow::__start__,
                     &recogniser, &SpeechRecogniser::initialize);
    QObject::connect(&w, &MainWindow::__start__,
                     &translator, &Translator::initialize);
    QObject::connect(&w, &MainWindow::__start__,
                     &oscBroadcaster, &SoloOscBroadcaster::initialize);


    QObject::connect(&w, &MainWindow::__stop__,
                     &audioCapture, &AudioCapture::stop);

    // 启动子线程并初始化工作对象
    recogniserThread.start();
    translatorThread.start();
    oscThread.start();

    // 在工作线程中执行初始化（确保对象在正确线程中创建资源）
    //QTimer::singleShot(0, &recogniser, &SpeechRecogniser::initialize);

    w.show();

    // 程序退出时清理
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&]() {
        recogniserThread.quit();
        translatorThread.quit();
        oscThread.quit();
        recogniserThread.wait();
        translatorThread.wait();
        oscThread.wait();
    });

    return a.exec();
}
