#pragma once

#include <QString>
#include <QStringList>

inline const QStringList kArkActions{
    QStringLiteral("open"),
    QStringLiteral("extract"),
    QStringLiteral("extract-here"),
    QStringLiteral("extract-to"),
    QStringLiteral("test"),
    QStringLiteral("compress"),
    QStringLiteral("compress-email"),
    QStringLiteral("compress-to-7z"),
    QStringLiteral("compress-to-7z-email"),
    QStringLiteral("compress-to-zip"),
    QStringLiteral("compress-to-zip-email"),
    QStringLiteral("hash-crc32"),
    QStringLiteral("hash-crc64"),
    QStringLiteral("hash-xxh64"),
    QStringLiteral("hash-md5"),
    QStringLiteral("hash-sha1"),
    QStringLiteral("hash-sha256"),
    QStringLiteral("hash-sha384"),
    QStringLiteral("hash-sha512"),
    QStringLiteral("hash-sha3-256"),
    QStringLiteral("hash-blake2sp"),
    QStringLiteral("hash-all"),
    QStringLiteral("hash-generate-sha256"),
    QStringLiteral("hash-test"),
};

bool needsExtract(const QString &name);
QString extractSubfolderName(const QString &arcName);
QString createArchiveName(const QStringList &paths, bool isHash = false);
QString quotedReduced(const QString &name);
bool selectionWantsExtract(const QStringList &paths);
int runArchiveAction(const QString &action, const QStringList &files);
