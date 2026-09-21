#include "uploaders.h"
#include "config.h"
#include "ui.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QEventLoop>

void imgurUpload(const QStringList &files)
{
    AppConfig cfg = loadConfig();
    if (cfg.imgurClientId.isEmpty()) {
        cfg.imgurClientId = ui::inputDialog(QStringLiteral("Imgur Upload"),
                                            QStringLiteral("Enter your Imgur Client ID:"));
        if (cfg.imgurClientId.isEmpty())
            return;
        if (ui::confirmDialog(QStringLiteral("Imgur Upload"), QStringLiteral("Save this Client ID for future uploads?")))
            saveConfig(cfg);
    }
    QNetworkAccessManager nam;
    int ok = 0;
    QStringList errors;
    auto handle = ui::pbarOpen(QStringLiteral("Upload to Imgur"), QStringLiteral("Starting…"));
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo info(files[i]);
        if (!info.exists()) {
            errors << QStringLiteral("File not found: %1").arg(files[i]);
            continue;
        }
        ui::pbarSet(handle, int(i * 100.0 / files.size()), QStringLiteral("Uploading: %1").arg(info.fileName()));
        QFile file(files[i]);
        if (!file.open(QIODevice::ReadOnly)) {
            errors << info.fileName();
            continue;
        }
        QNetworkRequest req(QUrl(QStringLiteral("https://api.imgur.com/3/image")));
        req.setRawHeader("Authorization", QByteArray("Client-ID ") + cfg.imgurClientId.toUtf8());
        QNetworkReply *reply = nam.post(req, file.readAll());
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const bool success = reply->error() == QNetworkReply::NoError
            && doc.object().value(QLatin1String("success")).toBool();
        reply->deleteLater();
        if (success) {
            ++ok;
            const QString url = doc.object().value(QLatin1String("data")).toObject().value(QLatin1String("link")).toString();
            QProcess::startDetached(QStringLiteral("wl-copy"), {});
            Q_UNUSED(url);
        } else {
            errors << info.fileName();
        }
    }
    ui::pbarClose(handle);
    if (ok > 0)
        ui::notify(QStringLiteral("Imgur Upload - Done"), QStringLiteral("✔ %1 file(s) uploaded").arg(ok),
                   QStringLiteral("internet-web-browser"));
    if (!errors.isEmpty())
        ui::errorDialog(QStringLiteral("Imgur Upload - Errors"), errors.join(QLatin1Char('\n')));
}
