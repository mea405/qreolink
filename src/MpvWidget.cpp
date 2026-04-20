#include "MpvWidget.h"

#include <clocale>
#include <QDebug>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

MpvWidget::MpvWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    initMpv();
}

MpvWidget::~MpvWidget()
{
    if (mpvRender_ != nullptr) {
        makeCurrent();
        mpv_render_context_set_update_callback(mpvRender_, nullptr, nullptr);
        mpv_render_context_free(mpvRender_);
        mpvRender_ = nullptr;
        doneCurrent();
    }

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
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "profile", "low-latency");
    mpv_set_option_string(mpv_, "untimed", "yes");
    mpv_set_option_string(mpv_, "cache", "yes");
    mpv_set_option_string(mpv_, "rtsp-transport", "tcp");
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
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &MpvWidget::processEvents);
    timer->start();

    mpvInitialized_ = true;
    return true;
}

void MpvWidget::initializeGL()
{
    if (initMpv() && initRenderContext()) {
        loadCurrentUrl();
    }
}

void MpvWidget::play(const QString& url)
{
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
    if (mpv_ == nullptr || currentUrl_.isEmpty()) {
        return;
    }

    const QByteArray utf8 = currentUrl_.toUtf8();
    const char* cmd[] = {"loadfile", utf8.constData(), "replace", nullptr};
    if (mpv_command(mpv_, cmd) < 0) {
        qWarning() << "Failed to start stream:" << currentUrl_;
        emitStatus(QStringLiteral("Failed to start stream command"));
    }
}

void MpvWidget::stop()
{
    if (mpv_ == nullptr) {
        return;
    }

    const char* cmd[] = {"stop", nullptr};
    mpv_command(mpv_, cmd);
    emitStatus(QStringLiteral("Stopped"));
    currentUrl_.clear();
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

void MpvWidget::onUpdate(void* ctx)
{
    auto* self = static_cast<MpvWidget*>(ctx);
    QMetaObject::invokeMethod(self, "update", Qt::QueuedConnection);
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
            emitStatus(QStringLiteral("Playing"));
            break;
        case MPV_EVENT_END_FILE: {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end != nullptr && end->error != MPV_ERROR_SUCCESS) {
                emitStatus(QStringLiteral("Playback error: %1").arg(QString::fromUtf8(mpv_error_string(end->error))));
            } else {
                emitStatus(QStringLiteral("Stream ended"));
            }
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
