#pragma once

#include <QString>
#include <QUrl>

enum class StreamType {
    Main,
    Sub
};

struct CameraConfig {
    QString name;
    QString host;
    QString username;
    QString password;
    QString mainPath;
    QString subPath;

    [[nodiscard]] QString streamUrl(StreamType type) const
    {
        const QString path = (type == StreamType::Main) ? mainPath : subPath;
        QUrl url;
        url.setScheme(QStringLiteral("rtsp"));
        url.setUserName(username);
        url.setPassword(password);
        url.setHost(host);
        url.setPath(QStringLiteral("/") + path);
        return url.toString(QUrl::FullyEncoded);
    }
};
