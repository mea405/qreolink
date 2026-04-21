#include "MainWindow.h"

#include "MpvWidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , cameras_(loadCameras())
{
    setWindowTitle(QStringLiteral("qreolink"));
    resize(1400, 900);
    buildUi();
    updateLayoutAndStreams();
}

QVector<CameraConfig> MainWindow::loadCameras()
{
    QSettings settings(QStringLiteral("qreolink"), QStringLiteral("qreolink"));
    const int count = settings.value(QStringLiteral("cameras/count"), 4).toInt();

    QVector<CameraConfig> list;
    list.reserve(count);

    for (int i = 0; i < count; ++i) {
        const QString key = QStringLiteral("cameras/%1/").arg(i);
        CameraConfig camera;
        camera.name = settings.value(key + QStringLiteral("name"),
                                     QStringLiteral("Camera %1").arg(i + 1)).toString();
        camera.host = settings.value(key + QStringLiteral("host"),
                                     QStringLiteral("192.168.11.%1").arg(226 + i)).toString();
        camera.username = settings.value(key + QStringLiteral("username"),
                                         QStringLiteral("admin")).toString();
        camera.password = settings.value(key + QStringLiteral("password"),
                                         QStringLiteral("change-me")).toString();
        camera.mainPath = settings.value(key + QStringLiteral("mainPath"),
                                         QStringLiteral("h264Preview_01_main")).toString();
        camera.subPath = settings.value(key + QStringLiteral("subPath"),
                                        QStringLiteral("h264Preview_01_sub")).toString();
        list.push_back(camera);
    }

    return list;
}

void MainWindow::saveCameras(const QVector<CameraConfig>& cameras)
{
    QSettings settings(QStringLiteral("qreolink"), QStringLiteral("qreolink"));
    settings.setValue(QStringLiteral("cameras/count"), cameras.size());

    for (int i = 0; i < cameras.size(); ++i) {
        const QString key = QStringLiteral("cameras/%1/").arg(i);
        settings.setValue(key + QStringLiteral("name"), cameras[i].name);
        settings.setValue(key + QStringLiteral("host"), cameras[i].host);
        settings.setValue(key + QStringLiteral("username"), cameras[i].username);
        settings.setValue(key + QStringLiteral("password"), cameras[i].password);
        settings.setValue(key + QStringLiteral("mainPath"), cameras[i].mainPath);
        settings.setValue(key + QStringLiteral("subPath"), cameras[i].subPath);
    }
}

void MainWindow::buildUi()
{
    central_ = new QWidget(this);
    setCentralWidget(central_);

    auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (singleIndex_ >= 0) {
            singleIndex_ = -1;
            updateLayoutAndStreams();
        }
    });

    auto* outer = new QVBoxLayout(central_);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    auto* topBar = new QHBoxLayout();
    topBar->addStretch();
    auto* settingsButton = new QPushButton(QStringLiteral("Settings"), central_);
    topBar->addWidget(settingsButton);
    outer->addLayout(topBar);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);

    grid_ = new QGridLayout();
    grid_->setSpacing(8);
    outer->addLayout(grid_);

    for (int i = 0; i < cameras_.size(); ++i) {
        Tile tile;
        tile.container = new QWidget(central_);
        auto* layout = new QVBoxLayout(tile.container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* header = new QHBoxLayout();
        tile.title = new QLabel(cameras_[i].name, tile.container);
        tile.toggleButton = new QPushButton(QStringLiteral("Single"), tile.container);
        tile.audioButton = new QPushButton(QStringLiteral("Audio"), tile.container);
        tile.audioButton->setCheckable(true);
        tile.audioButton->setVisible(false);
        tile.audioButton->setToolTip(QStringLiteral("Enable audio from the main stream (single view only)"));
        header->addWidget(tile.title);
        header->addStretch();
        header->addWidget(tile.audioButton);
        header->addWidget(tile.toggleButton);

        tile.player = new MpvWidget(tile.container);
        tile.status = new QLabel(QStringLiteral("Idle"), tile.container);
        tile.status->setStyleSheet(QStringLiteral("color: #b0b0b0;"));
        layout->addLayout(header);
        layout->addWidget(tile.player, 1);
        layout->addWidget(tile.status);

        connect(tile.toggleButton, &QPushButton::clicked, this, [this, i]() {
            toggleSingleView(i);
        });
        connect(tile.audioButton, &QPushButton::toggled, this, [this, i](bool on) {
            if (singleIndex_ != i || i < 0 || i >= tiles_.size() || tiles_[i].player == nullptr) {
                return;
            }
            tiles_[i].player->setAudioEnabled(on);
        });
        connect(tile.player, &MpvWidget::statusChanged, this, [this, i](const QString& status) {
            if (i >= 0 && i < tiles_.size() && tiles_[i].status != nullptr) {
                tiles_[i].status->setText(status);
            }
        });

        tiles_.push_back(tile);
    }
}

void MainWindow::toggleSingleView(int cameraIndex)
{
    if (singleIndex_ == cameraIndex) {
        singleIndex_ = -1;
    } else {
        singleIndex_ = cameraIndex;
    }

    updateLayoutAndStreams();
}

void MainWindow::openSettingsDialog()
{
    struct CameraFields {
        QLineEdit* name = nullptr;
        QLineEdit* host = nullptr;
        QLineEdit* username = nullptr;
        QLineEdit* password = nullptr;
        QLineEdit* mainPath = nullptr;
        QLineEdit* subPath = nullptr;
    };

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Camera Settings"));
    dialog.resize(700, 560);

    auto* root = new QVBoxLayout(&dialog);
    QVector<CameraFields> fields;
    fields.reserve(cameras_.size());

    for (int i = 0; i < cameras_.size(); ++i) {
        CameraFields cameraFields;
        auto* group = new QGroupBox(QStringLiteral("Camera %1").arg(i + 1), &dialog);
        auto* form = new QFormLayout(group);

        cameraFields.name = new QLineEdit(cameras_[i].name, group);
        cameraFields.host = new QLineEdit(cameras_[i].host, group);
        cameraFields.username = new QLineEdit(cameras_[i].username, group);
        cameraFields.password = new QLineEdit(cameras_[i].password, group);
        cameraFields.password->setEchoMode(QLineEdit::Normal);
        cameraFields.mainPath = new QLineEdit(cameras_[i].mainPath, group);
        cameraFields.subPath = new QLineEdit(cameras_[i].subPath, group);

        form->addRow(QStringLiteral("Name"), cameraFields.name);
        form->addRow(QStringLiteral("Host"), cameraFields.host);
        form->addRow(QStringLiteral("Username"), cameraFields.username);
        form->addRow(QStringLiteral("Password"), cameraFields.password);
        form->addRow(QStringLiteral("Main path"), cameraFields.mainPath);
        form->addRow(QStringLiteral("Sub path"), cameraFields.subPath);

        root->addWidget(group);
        fields.push_back(cameraFields);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<CameraConfig> updated = cameras_;
    for (int i = 0; i < updated.size(); ++i) {
        updated[i].name = fields[i].name->text().trimmed();
        updated[i].host = fields[i].host->text().trimmed();
        updated[i].username = fields[i].username->text().trimmed();
        updated[i].password = fields[i].password->text();
        updated[i].mainPath = fields[i].mainPath->text().trimmed();
        updated[i].subPath = fields[i].subPath->text().trimmed();
    }

    cameras_ = updated;
    saveCameras(cameras_);

    for (int i = 0; i < tiles_.size() && i < cameras_.size(); ++i) {
        tiles_[i].title->setText(cameras_[i].name);
    }

    if (singleIndex_ >= cameras_.size()) {
        singleIndex_ = -1;
    }
    updateLayoutAndStreams();
}

void MainWindow::updateLayoutAndStreams()
{
    while (QLayoutItem* item = grid_->takeAt(0)) {
        item->widget();
        delete item;
    }

    if (singleIndex_ < 0) {
        for (int i = 0; i < tiles_.size(); ++i) {
            const int row = i / 2;
            const int col = i % 2;
            tiles_[i].container->setVisible(true);
            tiles_[i].toggleButton->setText(QStringLiteral("Single"));
            if (tiles_[i].audioButton != nullptr) {
                tiles_[i].audioButton->blockSignals(true);
                tiles_[i].audioButton->setChecked(false);
                tiles_[i].audioButton->setVisible(false);
                tiles_[i].audioButton->blockSignals(false);
            }
            grid_->addWidget(tiles_[i].container, row, col);
            tiles_[i].player->play(cameras_[i].streamUrl(StreamType::Sub));
            tiles_[i].player->setAudioEnabled(false);
        }
        return;
    }

    for (int i = 0; i < tiles_.size(); ++i) {
        if (i == singleIndex_) {
            tiles_[i].container->setVisible(true);
            tiles_[i].toggleButton->setText(QStringLiteral("Back to Grid"));
            if (tiles_[i].audioButton != nullptr) {
                tiles_[i].audioButton->setVisible(true);
            }
            grid_->addWidget(tiles_[i].container, 0, 0, 2, 2);
            tiles_[i].player->play(cameras_[i].streamUrl(StreamType::Main));
            if (tiles_[i].audioButton != nullptr) {
                tiles_[i].player->setAudioEnabled(tiles_[i].audioButton->isChecked());
            }
        } else {
            tiles_[i].player->stop();
            tiles_[i].container->setVisible(false);
            tiles_[i].toggleButton->setText(QStringLiteral("Single"));
            if (tiles_[i].audioButton != nullptr) {
                tiles_[i].audioButton->blockSignals(true);
                tiles_[i].audioButton->setChecked(false);
                tiles_[i].audioButton->setVisible(false);
                tiles_[i].audioButton->blockSignals(false);
            }
        }
    }
}
