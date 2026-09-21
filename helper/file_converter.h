#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct Conversion {
    QString id;
    QString label;
    QStringList sourceExtensions;
    QStringList sourceMimetypes;
    QString targetExtension;
    bool featured = false;
    QStringList requiresCommands;
    QStringList requiresPackages;
    QString engine;
    QString icon = QStringLiteral("document-convert");
};

QVector<Conversion> loadConversions(bool availableOnly, const QString &registryOverride = {});
bool matchesConversion(const QString &path, const Conversion &conv);
int runConvert(const QString &conversionId, const QStringList &files, bool overwrite = false);
int runPick(const QStringList &files, bool overwrite = false);
QString desktopActionId(const QString &conversionId);
QString convertCsvJson(const QString &source, const QString &target);
