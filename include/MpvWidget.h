#pragma once

#include <QOpenGLWidget>
#include <QString>

class QMouseEvent;

#include <atomic>

struct mpv_handle;
struct mpv_render_context;
class QOpenGLContext;

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit MpvWidget(QWidget* parent = nullptr);
    ~MpvWidget() override;

    void play(const QString& url);
    void stop();
    void setAudioEnabled(bool enabled);

signals:
    void statusChanged(const QString& status);
    void clicked();

private:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    bool initMpv();
    void loadCurrentUrl();
    void processEvents();
    void emitStatus(const QString& status);
    bool initRenderContext();
    void cleanupRenderContext();
    void onContextAboutToBeDestroyed();
    static void onMpvWakeup(void* ctx);
    static void onUpdate(void* ctx);
    static void* getProcAddress(void* ctx, const char* name);

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* mpvRender_ = nullptr;
    QString lastStatus_;
    QString currentUrl_;
    bool mpvInitialized_ = false;
    QOpenGLContext* glContext_ = nullptr;
    std::atomic_bool processEventsQueued_ = false;
};
