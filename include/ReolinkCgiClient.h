#pragma once

#include "CameraConfig.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Reolink web CGI client: digest login + AES-CFB encrypted commands.
class ReolinkCgiClient : public QObject {
    Q_OBJECT

public:
    explicit ReolinkCgiClient(QObject* parent = nullptr);

    // Starts async reboot. Emits finished() when done (or on error).
    void reboot(const CameraConfig& camera);

    [[nodiscard]] bool isBusy() const { return state_ != State::Idle; }

signals:
    void finished(bool ok, const QString& message);

private:
    enum class State {
        Idle,
        ChallengeHttp,
        ChallengeHttps,
        DigestLogin,
        Reboot
    };

    void resetSession();
    void fail(const QString& message);
    void succeed(const QString& message);
    void beginChallenge(const QString& baseUrl, State state);
    void sendDigestLogin();
    void sendReboot();
    void configureSsl(class QNetworkRequest* request) const;
    void onReplyFinished(QNetworkReply* reply);

    [[nodiscard]] static QString md5Hex(const QString& input);
    [[nodiscard]] static QString randomCnonce();
    [[nodiscard]] QByteArray makeCommandBody(const QString& cmd) const;
    [[nodiscard]] QString decryptBody(const QByteArray& raw) const;
    [[nodiscard]] bool parseDigestChallenge(const QString& header);

    QNetworkAccessManager* nam_ = nullptr;
    State state_ = State::Idle;

    CameraConfig camera_;
    QString baseUrl_;
    QString realm_;
    QString nonce_;
    QString qop_;
    QString nc_;
    QString cnonce_;
    QString aesKey_;
    QString aesIv_;
    QString tokenName_;
    int checkBasic_ = 0;
    int countTotal_ = 0;
};
