#include "ReolinkCgiClient.h"

#include "AesCfb128.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

namespace {
constexpr int kRequestTimeoutMs = 15000;

QString headerValue(const QNetworkReply* reply, const QByteArray& name)
{
    const auto raw = reply->rawHeader(name);
    if (!raw.isEmpty()) {
        return QString::fromUtf8(raw);
    }
    // Some stacks vary casing.
    const auto list = reply->rawHeaderList();
    for (const QByteArray& key : list) {
        if (QString::fromLatin1(key).compare(QString::fromLatin1(name), Qt::CaseInsensitive) == 0) {
            return QString::fromUtf8(reply->rawHeader(key));
        }
    }
    return {};
}

}  // namespace

ReolinkCgiClient::ReolinkCgiClient(QObject* parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
    , requestTimer_(new QTimer(this))
{
    requestTimer_->setSingleShot(true);
    connect(requestTimer_, &QTimer::timeout, this, &ReolinkCgiClient::onRequestTimeout);
    connect(nam_, &QNetworkAccessManager::sslErrors, this,
            [](QNetworkReply* reply, const QList<QSslError>&) {
                if (reply != nullptr) {
                    reply->ignoreSslErrors();
                }
            });
}

ReolinkCgiClient::~ReolinkCgiClient()
{
    clearReply();
}

void ReolinkCgiClient::reboot(const CameraConfig& camera)
{
    if (state_ != State::Idle) {
        emit finished(false, QStringLiteral("Another reboot request is already in progress"));
        return;
    }
    if (camera.host.trimmed().isEmpty()) {
        emit finished(false, QStringLiteral("Camera host is empty"));
        return;
    }

    resetSession();
    camera_ = camera;
    beginChallenge(QStringLiteral("http://%1").arg(camera.host.trimmed()), State::ChallengeHttp);
}

void ReolinkCgiClient::clearReply()
{
    if (requestTimer_ != nullptr) {
        requestTimer_->stop();
    }
    if (currentReply_ == nullptr) {
        return;
    }
    QNetworkReply* reply = currentReply_;
    currentReply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void ReolinkCgiClient::watchReply(QNetworkReply* reply)
{
    clearReply();
    currentReply_ = reply;
    if (reply == nullptr) {
        return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
    requestTimer_->start(kRequestTimeoutMs);
}

void ReolinkCgiClient::onRequestTimeout()
{
    if (state_ == State::Idle && currentReply_ == nullptr) {
        return;
    }
    clearReply();
    fail(QStringLiteral("Request timed out"));
}

void ReolinkCgiClient::resetSession()
{
    realm_.clear();
    nonce_.clear();
    qop_.clear();
    nc_.clear();
    cnonce_.clear();
    aesKey_.clear();
    aesIv_.clear();
    tokenName_.clear();
    checkBasic_ = 0;
    countTotal_ = 0;
    baseUrl_.clear();
}

void ReolinkCgiClient::fail(const QString& message)
{
    state_ = State::Idle;
    emit finished(false, message);
}

void ReolinkCgiClient::succeed(const QString& message)
{
    state_ = State::Idle;
    emit finished(true, message);
}

void ReolinkCgiClient::configureSsl(QNetworkRequest* request) const
{
    if (request == nullptr || !baseUrl_.startsWith(QStringLiteral("https://"))) {
        return;
    }
    QSslConfiguration conf = request->sslConfiguration();
    conf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request->setSslConfiguration(conf);
}

void ReolinkCgiClient::beginChallenge(const QString& baseUrl, State state)
{
    state_ = state;
    baseUrl_ = baseUrl;

    const QUrl url(baseUrl_ + QStringLiteral("/cgi-bin/api.cgi?cmd=Login"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    configureSsl(&request);

    const QJsonArray body{QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("Login")},
        {QStringLiteral("action"), 0},
        {QStringLiteral("param"), QJsonObject{{QStringLiteral("Version"), 1}}},
    }};
    watchReply(nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

QString ReolinkCgiClient::md5Hex(const QString& input)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString ReolinkCgiClient::randomCnonce()
{
    static const char kHex[] = "0123456789abcdef";
    QString out;
    out.resize(48);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 48; ++i) {
        out[i] = QChar(QLatin1Char(kHex[rng->bounded(16)]));
    }
    return out;
}

bool ReolinkCgiClient::parseDigestChallenge(const QString& header)
{
    if (!header.contains(QStringLiteral("Digest"), Qt::CaseInsensitive)) {
        return false;
    }

    auto takeQuoted = [&header](const QString& key) -> QString {
        const QRegularExpression re(QStringLiteral("%1\\s*=\\s*\"([^\"]+)\"").arg(key),
                                    QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(header);
        return match.hasMatch() ? match.captured(1) : QString{};
    };
    auto takeToken = [&header](const QString& key) -> QString {
        const QRegularExpression re(QStringLiteral("%1\\s*=\\s*([^,\\s]+)").arg(key),
                                    QRegularExpression::CaseInsensitiveOption);
        const auto match = re.match(header);
        return match.hasMatch() ? match.captured(1).remove(QLatin1Char('"')) : QString{};
    };

    realm_ = takeQuoted(QStringLiteral("realm"));
    nonce_ = takeQuoted(QStringLiteral("nonce"));
    qop_ = takeQuoted(QStringLiteral("qop"));
    if (qop_.isEmpty()) {
        qop_ = takeToken(QStringLiteral("qop"));
    }
    nc_ = takeQuoted(QStringLiteral("nc"));
    if (nc_.isEmpty()) {
        nc_ = takeToken(QStringLiteral("nc"));
    }
    if (nc_.isEmpty()) {
        nc_ = QStringLiteral("00000001");
    }
    if (qop_.isEmpty()) {
        qop_ = QStringLiteral("auth");
    }
    return !realm_.isEmpty() && !nonce_.isEmpty();
}

void ReolinkCgiClient::sendDigestLogin()
{
    state_ = State::DigestLogin;
    cnonce_ = randomCnonce();

    const QString method = QStringLiteral("POST");
    const QString uri = QStringLiteral("cgi-bin/api.cgi?cmd=Login");
    const QString ha1 = md5Hex(QStringLiteral("%1:%2:%3")
                                   .arg(camera_.username, realm_, camera_.password));
    const QString ha2 = md5Hex(QStringLiteral("%1:%2").arg(method, uri));
    const QString response = md5Hex(QStringLiteral("%1:%2:%3:%4:%5:%6")
                                        .arg(ha1, nonce_, nc_, cnonce_, qop_, ha2));

    aesKey_ = md5Hex(QStringLiteral("%1-%2-%3").arg(nonce_, camera_.password, cnonce_))
                  .left(16)
                  .toUpper();
    aesIv_ = md5Hex(QStringLiteral("webapp-%1-%2-%3-%4")
                        .arg(cnonce_, camera_.password, nonce_, camera_.username))
                 .left(16)
                 .toUpper();

    const QJsonObject digest{
        {QStringLiteral("UserName"), camera_.username},
        {QStringLiteral("Realm"), realm_},
        {QStringLiteral("Method"), method},
        {QStringLiteral("Uri"), uri},
        {QStringLiteral("Nonce"), nonce_},
        {QStringLiteral("Nc"), nc_},
        {QStringLiteral("Cnonce"), cnonce_},
        {QStringLiteral("Qop"), qop_},
        {QStringLiteral("Response"), response},
    };
    const QJsonArray body{QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("Login")},
        {QStringLiteral("action"), 0},
        {QStringLiteral("param"),
         QJsonObject{{QStringLiteral("Version"), 1}, {QStringLiteral("Digest"), digest}}},
    }};

    const QUrl url(baseUrl_ + QStringLiteral("/cgi-bin/api.cgi?cmd=Login"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    configureSsl(&request);
    watchReply(nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

QByteArray ReolinkCgiClient::makeCommandBody(const QString& cmd) const
{
    const QJsonArray body{QJsonObject{
        {QStringLiteral("cmd"), cmd},
        {QStringLiteral("action"), 0},
        {QStringLiteral("param"), QJsonObject{}},
    }};
    const QString plain = QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact));
    return AesCfb128::encryptToBase64(aesKey_, aesIv_, plain).toUtf8();
}

QString ReolinkCgiClient::decryptBody(const QByteArray& raw) const
{
    const QString text = QString::fromUtf8(raw).trimmed();
    if (text.startsWith(QLatin1Char('[')) || text.startsWith(QLatin1Char('{'))) {
        return text;
    }
    return AesCfb128::decryptFromBase64(aesKey_, aesIv_, text);
}

void ReolinkCgiClient::sendReboot()
{
    state_ = State::Reboot;

    int countId = 3;
    if (countTotal_ > 0) {
        QVector<int> usable;
        usable.reserve(countTotal_);
        for (int id = 0; id < countTotal_; ++id) {
            if (id != 0 && id != 1 && id != 2) {
                usable.push_back(id);
            }
        }
        if (usable.isEmpty()) {
            for (int id = 0; id < countTotal_; ++id) {
                usable.push_back(id);
            }
        }
        countId = usable.at(QRandomGenerator::global()->bounded(usable.size()));
    }
    const int checkNum = checkBasic_ + 1;
    const QString encPlain = QStringLiteral("countId=%1&checkNum=%2").arg(countId).arg(checkNum);
    const QString encValue = AesCfb128::encryptToBase64(aesKey_, aesIv_, encPlain);

    QUrl url(baseUrl_ + QStringLiteral("/cgi-bin/api.cgi"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), tokenName_);
    query.addQueryItem(QStringLiteral("encrypt"), encValue);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    configureSsl(&request);
    watchReply(nam_->post(request, makeCommandBody(QStringLiteral("Reboot"))));
}

void ReolinkCgiClient::onReplyFinished(QNetworkReply* reply)
{
    if (currentReply_ == reply) {
        currentReply_ = nullptr;
    }
    if (requestTimer_ != nullptr) {
        requestTimer_->stop();
    }
    reply->deleteLater();

    if (state_ == State::Idle) {
        return;
    }

    const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
        const QUrl redirected = reply->url().resolved(redirect.toUrl());
        if (state_ == State::ChallengeHttp && redirected.scheme() == QStringLiteral("https")) {
            beginChallenge(QStringLiteral("https://%1").arg(camera_.host.trimmed()),
                           State::ChallengeHttps);
            return;
        }
    }

    if (reply->error() != QNetworkReply::NoError
        && headerValue(reply, "WWW-Authenticate").isEmpty()) {
        if (state_ == State::ChallengeHttp) {
            beginChallenge(QStringLiteral("https://%1").arg(camera_.host.trimmed()),
                           State::ChallengeHttps);
            return;
        }
        fail(QStringLiteral("Network error: %1").arg(reply->errorString()));
        return;
    }

    if (state_ == State::ChallengeHttp || state_ == State::ChallengeHttps) {
        const QString auth = headerValue(reply, "WWW-Authenticate");
        if (!parseDigestChallenge(auth)) {
            if (state_ == State::ChallengeHttp) {
                beginChallenge(QStringLiteral("https://%1").arg(camera_.host.trimmed()),
                               State::ChallengeHttps);
                return;
            }
            fail(QStringLiteral("Device did not return a digest login challenge"));
            return;
        }
        sendDigestLogin();
        return;
    }

    const QByteArray raw = reply->readAll();
    if (state_ == State::DigestLogin) {
        if (raw.trimmed().isEmpty()) {
            fail(QStringLiteral("Empty login response from device"));
            return;
        }
        const QString decoded = decryptBody(raw);
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(decoded.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) {
            fail(QStringLiteral("Failed to parse login response (%1 bytes, decrypt %2 chars)")
                     .arg(raw.size())
                     .arg(decoded.size()));
            return;
        }
        const QJsonObject item = doc.array().at(0).toObject();
        if (item.value(QStringLiteral("code")).toInt(-1) != 0) {
            const QString detail = item.value(QStringLiteral("error")).toObject()
                                       .value(QStringLiteral("detail")).toString();
            fail(detail.isEmpty() ? QStringLiteral("Login failed") : detail);
            return;
        }
        const QJsonObject token = item.value(QStringLiteral("value")).toObject()
                                      .value(QStringLiteral("Token")).toObject();
        tokenName_ = token.value(QStringLiteral("name")).toString();
        checkBasic_ = token.value(QStringLiteral("checkBasic")).toInt();
        countTotal_ = token.value(QStringLiteral("countTotal")).toInt();
        if (tokenName_.isEmpty()) {
            fail(QStringLiteral("Login response did not include a token"));
            return;
        }
        sendReboot();
        return;
    }

    if (state_ == State::Reboot) {
        // Device may close the connection immediately after accepting reboot.
        if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
            succeed(QStringLiteral("Reboot command sent"));
            return;
        }
        const QString decoded = decryptBody(raw);
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(decoded.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty()) {
            // Many firmwares drop the TCP session mid-response; treat as success.
            succeed(QStringLiteral("Reboot command sent"));
            return;
        }
        const QJsonObject item = doc.array().at(0).toObject();
        if (item.value(QStringLiteral("code")).toInt(-1) != 0) {
            const QString detail = item.value(QStringLiteral("error")).toObject()
                                       .value(QStringLiteral("detail")).toString();
            fail(detail.isEmpty() ? QStringLiteral("Reboot failed") : detail);
            return;
        }
        succeed(QStringLiteral("Reboot started"));
        return;
    }

    fail(QStringLiteral("Unexpected client state"));
}
