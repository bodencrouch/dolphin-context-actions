#pragma once

#include <QString>

struct AppConfig {
    QString audioPreset = QStringLiteral("mp3");
    QString videoPreset = QStringLiteral("medium");
    QString imgurClientId;
};

AppConfig loadConfig();
void saveConfig(const AppConfig &cfg);
QString configDir();
