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
constexpr int kStreamResetMinIntervalMs = 8000;
constexpr int kDtsJumpWindowMs = 2000;
constexpr int kDtsJumpsBeforeReset = 3;
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
    ++playbackEpoch_;
    cancelReconnect();

    if (mpv_ != nullptr) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    }

    cleanupRenderContext();
    glContext_ = nullptr;

    if (mpv_ != nullptr) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    mpvInitialized_ = false;
}

void MpvWidget::destroyMpv()
{
    cancelReconnect();

    if (mpv_ != nullptr) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    }

    const bool hasGl = (context() != nullptr);
    if (hasGl) {
        makeCurrent();
    }
    cleanupRenderContext();
    if (glContext_ != nullptr) {
        disconnect(glContext_,
                   &QOpenGLContext::aboutToBeDestroyed,
                   this,
                   &MpvWidget::onContextAboutToBeDestroyed);
        glContext_ = nullptr;
    }
    if (hasGl) {
        doneCurrent();
    }

    if (mpv_ != nullptr) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    mpvInitialized_ = false;
    processEventsQueued_.store(false);
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
    // Do not use profile=low-latency / video-latency-hacks: they set fflags=+nobuffer
    // and shrink decoder delay, which splits Reolink main-stream slices into fake
    // frames (h264 "top block unavailable" / "error while decoding MB 40 0").
    mpv_set_option_string(mpv_, "untimed", "no");
    mpv_set_option_string(mpv_, "video-sync", "desync");
    // Reolink RTSP timestamps jump between independent bases; waiting on them stalls video.
    mpv_set_option_string(mpv_, "correct-pts", "no");
    mpv_set_option_string(mpv_, "interpolation", "no");
    mpv_set_option_string(mpv_, "cache", "yes");
    mpv_set_option_string(mpv_, "cache-secs", "1");
    mpv_set_option_string(mpv_, "cache-pause", "no");
    mpv_set_option_string(mpv_, "demuxer-max-bytes", "16777216");
    mpv_set_option_string(mpv_, "demuxer-max-back-bytes", "8388608");
    mpv_set_option_string(mpv_, "demuxer-lavf-analyzeduration", "1.0");
    mpv_set_option_string(mpv_, "demuxer-lavf-probesize", "1048576");
    mpv_set_option_string(mpv_, "rtsp-transport", "tcp");
    // Fail stalled TCP/RTSP sessions so END_FILE fires and reconnect can run.
    mpv_set_option_string(mpv_, "network-timeout", "10");
    // Reolink timestamps can jump between independent bases on video (stream 0)
    // and audio (stream 1). If lavf reorders/drops on DTS, H.264 slices of one
    // frame are decoded as separate pictures ("top block unavailable", MB 40 0).
    QByteArray lavfOpts =
        "stimeout=10000000,rw_timeout=10000000,"
        "fflags=-nobuffer+igndts,reorder_queue_size=0";
    if (!audioEnabled_) {
        lavfOpts += ",allowed_media_types=video";
    }
    mpv_set_option_string(mpv_, "demuxer-lavf-o", lavfOpts.constData());
    mpv_set_option_string(mpv_, "audio", audioEnabled_ ? "auto" : "no");
    mpv_set_option_string(mpv_, "hwdec", "no");
    // Frame-threaded H.264 + a mid-GOP RTSP join produces "top block unavailable" artifacts.
    mpv_set_option_string(mpv_, "vd-lavc-threads", "1");
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
        if (glContext_ != nullptr) {
            disconnect(glContext_,
                       &QOpenGLContext::aboutToBeDestroyed,
                       this,
                       &MpvWidget::onContextAboutToBeDestroyed);
        }
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
        // Resize/reparent (grid ↔ single) recreates the GL context. Restarting RTSP
        // here joins the bitstream mid-NAL and leaves a corrupt picture until the
        // next IDR — which Reolink may not send for a long time.
        // makeCurrent() inside beginPlayback/destroyMpv can reenter initializeGL;
        // never loadfile from that nested call (it would open a second RTSP session).
        if (!startingPlayback_ && !streamLoadIssued_) {
            loadCurrentUrl();
        }
    }
}

void MpvWidget::play(const QString& url)
{
    // Same target (playing or reconnecting): do not reset backoff or reload.
    if (!url.isEmpty() && url == currentUrl_ && !intentionalStop_ && mpv_ != nullptr) {
        return;
    }

    intentionalStop_ = false;
    cancelReconnect();
    reconnectAttempt_ = 0;
    currentUrl_ = url;
    emitStatus(QStringLiteral("Connecting..."));

    // Block initializeGL from loadfile on the stale instance. A hidden widget
    // (arrow switch in single view) still has an old RTSP/RTP session; reusing it
    // produces "RTP: bad cseq" and a broken picture.
    streamLoadIssued_ = true;
    const int epoch = ++playbackEpoch_;
    QMetaObject::invokeMethod(
        this,
        [this, epoch]() {
            if (epoch != playbackEpoch_ || intentionalStop_ || currentUrl_.isEmpty()) {
                return;
            }
            beginPlayback();
        },
        Qt::QueuedConnection);
}

void MpvWidget::beginPlayback()
{
    startingPlayback_ = true;
    streamLoadIssued_ = true;
    destroyMpv();

    if (!initMpv()) {
        startingPlayback_ = false;
        emitStatus(QStringLiteral("mpv init failed"));
        return;
    }

    if (context() == nullptr) {
        startingPlayback_ = false;
        streamLoadIssued_ = false;
        update();
        return;
    }

    makeCurrent();
    const bool ok = initRenderContext();
    if (ok && glContext_ == nullptr) {
        glContext_ = context();
        if (glContext_ != nullptr) {
            connect(glContext_,
                    &QOpenGLContext::aboutToBeDestroyed,
                    this,
                    &MpvWidget::onContextAboutToBeDestroyed,
                    Qt::DirectConnection);
        }
    }
    doneCurrent();

    if (!ok) {
        startingPlayback_ = false;
        streamLoadIssued_ = false;
        update();
        return;
    }

    loadCurrentUrl();
    applyAudioSetting();
    startingPlayback_ = false;
}

void MpvWidget::loadCurrentUrl()
{
    if (mpv_ == nullptr || currentUrl_.isEmpty() || intentionalStop_) {
        return;
    }

    streamLoadIssued_ = true;

    const QByteArray utf8 = currentUrl_.toUtf8();
    const char* cmd[] = {"loadfile", utf8.constData(), "replace", nullptr};
    if (mpv_command(mpv_, cmd) < 0) {
        streamLoadIssued_ = false;
        qWarning() << "Failed to start stream:" << currentUrl_;
        emitStatus(QStringLiteral("Failed to start stream command"));
        scheduleReconnect(QStringLiteral("load failed"));
        return;
    }
    applyAudioSetting();
}

void MpvWidget::stop()
{
    ++playbackEpoch_;
    intentionalStop_ = true;
    cancelReconnect();
    reconnectAttempt_ = 0;
    currentUrl_.clear();
    streamLoadIssued_ = false;
    destroyMpv();
    emitStatus(QStringLiteral("Stopped"));
}

void MpvWidget::applyAudioSetting()
{
    if (mpv_ == nullptr) {
        return;
    }
    const char* value = audioEnabled_ ? "auto" : "no";
    if (mpv_set_property_string(mpv_, "audio", value) < 0) {
        qWarning() << "mpv_set_property audio failed";
    }
}

void MpvWidget::setAudioEnabled(bool enabled)
{
    const bool changed = (audioEnabled_ != enabled);
    audioEnabled_ = enabled;
    applyAudioSetting();
    if (!changed || currentUrl_.isEmpty() || intentionalStop_) {
        return;
    }
    // RTSP SETUP with/without audio must be redone; toggling the mpv property is not enough.
    const QString url = currentUrl_;
    currentUrl_.clear();
    play(url);
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
    beginPlayback();
}

void MpvWidget::replayCurrentUrl()
{
    if (intentionalStop_ || currentUrl_.isEmpty() || startingPlayback_) {
        return;
    }
    const QString url = currentUrl_;
    currentUrl_.clear();
    play(url);
}

void MpvWidget::noteUnstableStream(const QString& text)
{
    if (intentionalStop_ || currentUrl_.isEmpty() || startingPlayback_) {
        return;
    }
    if (streamResetTimer_.isValid() && streamResetTimer_.elapsed() < kStreamResetMinIntervalMs) {
        return;
    }

    const bool dtsJump = text.contains(QStringLiteral("DTS discontinuity"), Qt::CaseInsensitive);
    const bool badCseq = text.contains(QStringLiteral("bad cseq"), Qt::CaseInsensitive);
    if (!dtsJump && !badCseq) {
        return;
    }

    if (dtsJump) {
        if (!dtsJumpWindow_.isValid() || dtsJumpWindow_.elapsed() > kDtsJumpWindowMs) {
            dtsJumpCount_ = 0;
            dtsJumpWindow_.restart();
        }
        ++dtsJumpCount_;
        if (dtsJumpCount_ < kDtsJumpsBeforeReset) {
            return;
        }
        dtsJumpCount_ = 0;
    }

    streamResetTimer_.restart();
    replayCurrentUrl();
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
            dtsJumpCount_ = 0;
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
            noteUnstableStream(text);
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
