#pragma once

#include "CameraConfig.h"

#include <QMainWindow>
#include <QVector>

class MpvWidget;
class ReolinkCgiClient;
class QGridLayout;
class QPushButton;
class QLabel;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    struct Tile {
        QWidget* container = nullptr;
        QLabel* title = nullptr;
        QLabel* status = nullptr;
        MpvWidget* player = nullptr;
        QPushButton* toggleButton = nullptr;
        QPushButton* audioButton = nullptr;
        QPushButton* rebootButton = nullptr;
    };

    QVector<CameraConfig> loadCameras();
    void saveCameras(const QVector<CameraConfig>& cameras);
    void buildUi();
    void updateLayoutAndStreams();
    void toggleSingleView(int cameraIndex);
    void singleViewArrowNavigate(int qtKey);
    void openSettingsDialog();
    void rebootCamera(int cameraIndex);
    void setRebootButtonsEnabled(bool enabled);

    QVector<CameraConfig> cameras_;
    QVector<Tile> tiles_;
    ReolinkCgiClient* cgiClient_ = nullptr;

    QWidget* central_ = nullptr;
    QGridLayout* grid_ = nullptr;
    int singleIndex_ = -1;
};
