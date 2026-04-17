#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThreadPool>
#include <QAtomicInt>
#include "ConfigManager.h"

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

    bool is_running;
    QTimer* audioTimer;

    void sendToOSC(const QString& text);  // 发送文字到VRChat

    void applyConfigToUi();
    void applyUiToConfig();


signals:
    void startRecordingRequested();
    void stopRecordingRequested();

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void onError(const QString& errorMessage);
    void onDebug(const QString& debugMessage);
};

#endif // MAINWINDOW_H
