#include "MpvWidget.h"

#include <clocale>
#include <QDebug>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace {
constexpr int kReconnectBaseMs = 1000;
constexpr int kReconnectMaxMs = 30000;
}

MpvWidget::MpvWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &MpvWidget::tryReconnect);
    initMpv();
}

void MpvWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

MpvWidget::~MpvWidget()
{
    cancelReconnect();

    if (mpv_ != nullptr) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    }

    cleanupRenderContext();

    if (mpv_ != nullptr) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

bool MpvWidget::initMpv()
{
    if (mpvInitialized_) {
        return true;
    }

    std::setlocale(LC_NUMERIC, "C");
    mpv_ = mpv_create();
    if (mpv_ == nullptr) {
        qWarning() << "mpv_create failed";
        return false;
    }

    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "msg-level", "all=warn");
    // Live RTSP: do not freeze on the last frame after disconnect.
    mpv_set_option_string(mpv_, "keep-open", "no");
    mpv_set_option_string(mpv_, "profile", "low-latency");
    mpv_set_option_string(mpv_, "untimed", "yes");
    mpv_set_option_string(mpv_, "cache", "yes");
    mpv_set_option_string(mpv_, "cache-secs", "3");
    mpv_set_option_string(mpv_, "cache-pause", "no");
    mpv_set_option_string(mpv_, "demuxer-max-bytes", "33554432");
    mpv_set_option_string(mpv_, "demuxer-max-back-bytes", "16777216");
    mpv_set_option_string(mpv_, "rtsp-transport", "tcp");
    // Fail stalled TCP/RTSP sessions so END_FILE fires and reconnect can run.
    mpv_set_option_string(mpv_, "network-timeout", "10");
    mpv_set_option_string(mpv_, "demuxer-lavf-o", "stimeout=10000000,rw_timeout=10000000");
    mpv_set_option_string(mpv_, "audio", "no");
    mpv_set_option_string(mpv_, "hwdec", "no");
    mpv_set_option_string(mpv_, "vd-lavc-threads", "2");
    mpv_set_option_string(mpv_, "fbo-format", "rgba8");
    mpv_set_option_string(mpv_, "vo", "libmpv");

    if (mpv_initialize(mpv_) < 0) {
        qWarning() << "mpv_initialize failed";
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_request_log_messages(mpv_, "info");
    mpv_set_wakeup_callback(mpv_, &MpvWidget::onMpvWakeup, this);

    mpvInitialized_ = true;
    return true;
}

void MpvWidget::initializeGL()
{
    QOpenGLContext* currentContext = context();
    if (currentContext != glContext_) {
        cleanupRenderContext();
        glContext_ = currentContext;
        if (glContext_ != nullptr) {
            connect(glContext_,
                    &QOpenGLContext::aboutToBeDestroyed,
                    this,
                    &MpvWidget::onContextAboutToBeDestroyed,
                    Qt::DirectConnection);
        }
    }

    if (initMpv() && initRenderContext()) {
        loadCurrentUrl();
    }
}

void MpvWidget::play(const QString& url)
{
    // Same target (playing or reconnecting): do not reset backoff or reload.
    if (!url.isEmpty() && url == currentUrl_ && !intentionalStop_) {
        return;
    }

    intentionalStop_ = false;
    cancelReconnect();
    reconnectAttempt_ = 0;
    currentUrl_ = url;
    emitStatus(QStringLiteral("Connecting..."));

    if (!initMpv()) {
        emitStatus(QStringLiteral("mpv init failed"));
        return;
    }

    if (mpvRender_ == nullptr) {
        update();
        return;
    }

    loadCurrentUrl();
}

void MpvWidget::loadCurrentUrl()
{
    if (mpv_ == nullptr || currentUrl_.isEmpty() || intentionalStop_) {
        return;
    }

    const QByteArray utf8 = currentUrl_.toUtf8();
    const char* cmd[] = {"loadfile", utf8.constData(), "replace", nullptr};
    if (mpv_command(mpv_, cmd) < 0) {
        qWarning() << "Failed to start stream:" << currentUrl_;
        emitStatus(QStringLiteral("Failed to start stream command"));
        scheduleReconnect(QStringLiteral("load failed"));
    }
}

void MpvWidget::stop()
{
    intentionalStop_ = true;
    cancelReconnect();
    reconnectAttempt_ = 0;

    if (mpv_ == nullptr) {
        currentUrl_.clear();
        return;
    }

    const char* cmd[] = {"stop", nullptr};
    mpv_command(mpv_, cmd);
    setAudioEnabled(false);
    emitStatus(QStringLiteral("Stopped"));
    currentUrl_.clear();
}

void MpvWidget::setAudioEnabled(bool enabled)
{
    if (mpv_ == nullptr) {
        return;
    }
    const char* value = enabled ? "auto" : "no";
    if (mpv_set_property_string(mpv_, "audio", value) < 0) {
        qWarning() << "mpv_set_property audio failed";
    }
}

void MpvWidget::cancelReconnect()
{
    reconnectPending_ = false;
    if (reconnectTimer_ != nullptr) {
        reconnectTimer_->stop();
    }
}

void MpvWidget::scheduleReconnect(const QString& reason)
{
    if (intentionalStop_ || currentUrl_.isEmpty() || reconnectPending_) {
        return;
    }

    reconnectPending_ = true;
    const int delayMs = qMin(kReconnectMaxMs, kReconnectBaseMs * (1 << qMin(reconnectAttempt_, 5)));
    ++reconnectAttempt_;

    emitStatus(QStringLiteral("Reconnecting in %1s… (%2)")
                   .arg((delayMs + 999) / 1000)
                   .arg(reason));
    reconnectTimer_->start(delayMs);
}

void MpvWidget::tryReconnect()
{
    reconnectPending_ = false;
    if (intentionalStop_ || currentUrl_.isEmpty()) {
        return;
    }

    emitStatus(QStringLiteral("Reconnecting…"));
    if (mpvRender_ == nullptr) {
        update();
        scheduleReconnect(QStringLiteral("render not ready"));
        return;
    }
    loadCurrentUrl();
}

bool MpvWidget::initRenderContext()
{
    if (mpv_ == nullptr) {
        return false;
    }
    if (mpvRender_ != nullptr) {
        return true;
    }

    mpv_opengl_init_params glInitParams;
    glInitParams.get_proc_address = &MpvWidget::getProcAddress;
    glInitParams.get_proc_address_ctx = this;
    const char* apiType = MPV_RENDER_API_TYPE_OPENGL;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(apiType)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&mpvRender_, mpv_, params) < 0) {
        qWarning() << "mpv_render_context_create failed";
        mpvRender_ = nullptr;
        emitStatus(QStringLiteral("Render context init failed"));
        return false;
    }

    mpv_render_context_set_update_callback(mpvRender_, &MpvWidget::onUpdate, this);
    return true;
}

void MpvWidget::cleanupRenderContext()
{
    if (mpvRender_ == nullptr) {
        return;
    }

    mpv_render_context_set_update_callback(mpvRender_, nullptr, nullptr);
    mpv_render_context_free(mpvRender_);
    mpvRender_ = nullptr;
}

void MpvWidget::onContextAboutToBeDestroyed()
{
    // Screen/hotplug can recreate QOpenGLWidget context; force libmpv context rebuild.
    cleanupRenderContext();
    glContext_ = nullptr;
}

void MpvWidget::onUpdate(void* ctx)
{
    auto* self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, "update", Qt::QueuedConnection);
}

void MpvWidget::onMpvWakeup(void* ctx)
{
    auto* self = static_cast<MpvWidget*>(ctx);
    if (self == nullptr) {
        return;
    }

    bool expected = false;
    if (!self->processEventsQueued_.compare_exchange_strong(expected, true)) {
        return;
    }

    QMetaObject::invokeMethod(
        self,
        [self]() {
            self->processEvents();
        },
        Qt::QueuedConnection);
}

void* MpvWidget::getProcAddress(void* ctx, const char* name)
{
    auto* self = static_cast<MpvWidget*>(ctx);
    auto* glContext = self->context();
    if (glContext == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void*>(glContext->getProcAddress(QByteArray(name)));
}

void MpvWidget::paintGL()
{
    if (mpvRender_ == nullptr) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo;
    fbo.fbo = static_cast<int>(defaultFramebufferObject());
    fbo.w = static_cast<int>(width() * dpr);
    fbo.h = static_cast<int>(height() * dpr);
    fbo.internal_format = GL_RGBA8;
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(mpvRender_, params);
}

void MpvWidget::emitStatus(const QString& status)
{
    if (status == lastStatus_) {
        return;
    }
    lastStatus_ = status;
    emit statusChanged(status);
}

void MpvWidget::processEvents()
{
    processEventsQueued_.store(false);

    if (mpv_ == nullptr) {
        return;
    }

    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0.0);
        if (event == nullptr || event->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            emitStatus(QStringLiteral("Opening stream..."));
            break;
        case MPV_EVENT_FILE_LOADED:
            reconnectAttempt_ = 0;
            cancelReconnect();
            emitStatus(QStringLiteral("Playing"));
            break;
        case MPV_EVENT_END_FILE: {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (intentionalStop_ || currentUrl_.isEmpty()) {
                break;
            }

            // Do not reconnect after an explicit stop/quit.
            if (end != nullptr
                && (end->reason == MPV_END_FILE_REASON_STOP
                    || end->reason == MPV_END_FILE_REASON_QUIT)) {
                break;
            }

            QString reason = QStringLiteral("stream ended");
            if (end != nullptr && end->error < 0) {
                reason = QString::fromUtf8(mpv_error_string(end->error));
                emitStatus(QStringLiteral("Playback error: %1").arg(reason));
            } else {
                emitStatus(QStringLiteral("Stream ended"));
            }
            scheduleReconnect(reason);
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            auto* log = static_cast<mpv_event_log_message*>(event->data);
            if (log == nullptr) {
                break;
            }

            const QString level = QString::fromUtf8(log->level ? log->level : "");
            const QString text = QString::fromUtf8(log->text ? log->text : "").trimmed();
            if (text.isEmpty()) {
                break;
            }
            if (text.contains(QStringLiteral("after creating texture: OpenGL error INVALID_ENUM"),
                              Qt::CaseInsensitive)) {
                break;
            }
            if (text.contains(QStringLiteral("bad cseq"), Qt::CaseInsensitive)) {
                break;
            }

            if (level == QStringLiteral("error") || level == QStringLiteral("fatal")) {
                emitStatus(QStringLiteral("mpv %1: %2").arg(level, text));
                qWarning() << "mpv" << level << text;
            } else if (level == QStringLiteral("warn")) {
                const QString low = text.toLower();
                if (low.contains(QStringLiteral("401"))
                    || low.contains(QStringLiteral("unauthorized"))
                    || low.contains(QStringLiteral("forbidden"))
                    || low.contains(QStringLiteral("timed out"))
                    || low.contains(QStringLiteral("not found"))
                    || low.contains(QStringLiteral("rtsp"))
                    || low.contains(QStringLiteral("tcp"))) {
                    emitStatus(QStringLiteral("mpv warn: %1").arg(text));
                }
                qWarning() << "mpv warn" << text;
            }
            break;
        }
        default:
            break;
        }
    }
}
