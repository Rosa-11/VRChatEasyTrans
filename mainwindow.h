#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ConfigManager.h"
#include "audiocapture.h"
#include "speechrecogniser.h"
#include "translator.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

private slots:
    void on_launchButton_clicked();

private:
    Ui::MainWindow *ui;

    ConfigManager &config;
    AudioCapture *capture;
    SpeechRecogniser *recogniser;
    Translator *translator;

    bool is_running;
    QTimer* audioTimer;

    void sendToOSC(const QString& text);  // 发送文字到VRChat

    void applyConfigToUi();
    void applyUiToConfig();

public slots:
    void processAudio();

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
};

#endif // MAINWINDOW_H
