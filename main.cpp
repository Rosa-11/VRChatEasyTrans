#include "mainwindow.h"
#include "ConfigManager.h"
#include "AudioCapture.h"
#include "speechrecogniser.h"
#include "translator.h"
#include "solooscbroadcaster.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ─── 国际化翻译文件加载 ────────────────────────────────────────────────
    QTranslator qTranslator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "VRChatEasyTrans-AI_" + QLocale(locale).name();
        if (qTranslator.load(":/i18n/" + baseName)) {
            a.installTranslator(&qTranslator);
            break;
        }
    }

    // ─── 配置管理器初始化 ──────────────────────────────────────────────────
    // 必须在 QApplication 创建之后调用（需要 applicationDirPath()）
    ConfigManager::getInstance().loadFileToManager();

    // ─── 主窗口 ────────────────────────────────────────────────────────────
    MainWindow w;

    // ─── 工作对象创建 ──────────────────────────────────────────────────────
    // AudioCapture 留在主线程
    AudioCapture audioCapture;

    // SpeechRecogniser 独立线程：WebSocket 收发不阻塞主线程
    QThread recogniserThread;
    SpeechRecogniser recogniser;
    recogniser.moveToThread(&recogniserThread);

    // Translator 独立线程：HTTP 请求不阻塞主线程
    QThread translatorThread;
    Translator translator;
    translator.moveToThread(&translatorThread);

    // OSC基于UDP协议，不阻塞线程，可以放进Translator线程
    // QThread oscThread;
    SoloOscBroadcaster oscBroadcaster;
    oscBroadcaster.moveToThread(&translatorThread);

    // ─── 信号与槽连接 ──────────────────────────────────────────────────────

    // 音频采集 → 语音识别（跨线程，自动 QueuedConnection）
    QObject::connect(&audioCapture, &AudioCapture::startRecognition,
                     &recogniser,   &SpeechRecogniser::onStartRecognition);
    QObject::connect(&audioCapture, &AudioCapture::sendAudioChunk,
                     &recogniser,   &SpeechRecogniser::onSendAudioChunk);
    QObject::connect(&audioCapture, &AudioCapture::stopRecognition,
                     &recogniser,   &SpeechRecogniser::onStopRecognition);

    // 语音识别 → 翻译（跨线程）
    QObject::connect(&recogniser,  &SpeechRecogniser::recognitionCompleted,
                     &translator,  &Translator::translateTextAsync);

    // 翻译 → OSC 广播（跨线程）
    QObject::connect(&translator,    &Translator::translationFinished,
                     &oscBroadcaster, &SoloOscBroadcaster::sendToOSC);

    // 错误信息 → 主窗口显示
    QObject::connect(&audioCapture, &AudioCapture::error,  &w, &MainWindow::onError);
    QObject::connect(&recogniser,   &SpeechRecogniser::error, &w, &MainWindow::onError);
    QObject::connect(&translator,   &Translator::translationError, &w, &MainWindow::onError);

    // 调试信息 → 主窗口显示
    QObject::connect(&audioCapture, &AudioCapture::debug,  &w, &MainWindow::onDebug);
    QObject::connect(&recogniser,   &SpeechRecogniser::debug, &w, &MainWindow::onDebug);
    QObject::connect(&translator,   &Translator::debug,    &w, &MainWindow::onDebug);

    // 主窗口启动按钮 → 各模块初始化
    QObject::connect(&w, &MainWindow::__start__, &audioCapture,   &AudioCapture::initialize);
    QObject::connect(&w, &MainWindow::__start__, &recogniser,     &SpeechRecogniser::initialize);
    QObject::connect(&w, &MainWindow::__start__, &translator,     &Translator::initialize);
    QObject::connect(&w, &MainWindow::__start__, &oscBroadcaster, &SoloOscBroadcaster::initialize);

    // 主窗口停止按钮 → 音频采集停止
    QObject::connect(&w, &MainWindow::__stop__, &audioCapture, &AudioCapture::stop);

    // ─── 启动子线程 ────────────────────────────────────────────────────────
    // 线程只负责提供事件循环，工作对象的初始化由 __start__ 信号触发
    recogniserThread.start();
    translatorThread.start();
    // oscThread.start();

    // ─── 显示主窗口 ────────────────────────────────────────────────────────
    w.show();

    // ─── 程序退出清理 ──────────────────────────────────────────────────────
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&]() {
        // 通知各线程退出事件循环
        recogniserThread.quit();
        translatorThread.quit();
        // oscThread.quit();
        // 等待线程完全退出（避免析构时仍有后台操作）
        recogniserThread.wait();
        translatorThread.wait();
        // oscThread.wait();
    });

    return a.exec();
}
