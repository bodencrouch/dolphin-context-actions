#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/dolphin-context-actions");
}

static QString configFile()
{
    return configDir() + QStringLiteral("/config.json");
}

AppConfig loadConfig()
{
    AppConfig cfg;
    QFile file(configFile());
    if (!file.open(QIODevice::ReadOnly))
        return cfg;
    const auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.contains(QLatin1String("audio_preset")))
        cfg.audioPreset = obj.value(QLatin1String("audio_preset")).toString();
    if (obj.contains(QLatin1String("video_preset")))
        cfg.videoPreset = obj.value(QLatin1String("video_preset")).toString();
    if (obj.contains(QLatin1String("imgur_client_id")))
        cfg.imgurClientId = obj.value(QLatin1String("imgur_client_id")).toString();
    return cfg;
}

void saveConfig(const AppConfig &cfg)
{
    QDir().mkpath(configDir());
    QJsonObject obj;
    obj.insert(QStringLiteral("audio_preset"), cfg.audioPreset);
    obj.insert(QStringLiteral("video_preset"), cfg.videoPreset);
    obj.insert(QStringLiteral("imgur_client_id"), cfg.imgurClientId);
    QFile file(configFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(obj).toJson());
}
