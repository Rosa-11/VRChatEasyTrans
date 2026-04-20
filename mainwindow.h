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

private:
    Ui::MainWindow *ui;

    ConfigManager &config = ConfigManager::getInstance();

    bool is_running;

    void applyConfigToUi();
    void applyUiToConfig();

private slots:
    void on_launchButton_clicked();

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void onError(const QString& errorMessage);
    void onDebug(const QString& debugMessage);

signals:
    void __start__();
    void __stop__();
};

#endif // MAINWINDOW_H
