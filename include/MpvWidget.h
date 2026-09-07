#pragma once

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QString>

class QMouseEvent;
class QTimer;

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
    void destroyMpv();
    void beginPlayback();
    void loadCurrentUrl();
    void applyAudioSetting();
    void processEvents();
    void emitStatus(const QString& status);
    bool initRenderContext();
    void cleanupRenderContext();
    void onContextAboutToBeDestroyed();
    void scheduleReconnect(const QString& reason);
    void cancelReconnect();
    void tryReconnect();
    void flushDecoderAfterError();
    static void onMpvWakeup(void* ctx);
    static void onUpdate(void* ctx);
    static void* getProcAddress(void* ctx, const char* name);

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* mpvRender_ = nullptr;
    QString lastStatus_;
    QString currentUrl_;
    bool mpvInitialized_ = false;
    bool intentionalStop_ = false;
    bool streamLoadIssued_ = false;
    bool audioEnabled_ = false;
    bool reconnectPending_ = false;
    int reconnectAttempt_ = 0;
    int playbackEpoch_ = 0;
    QTimer* reconnectTimer_ = nullptr;
    QElapsedTimer decoderFlushTimer_;
    QOpenGLContext* glContext_ = nullptr;
    std::atomic_bool processEventsQueued_ = false;
};
