#pragma once

#include <QString>
#include <QStringList>

bool pickLinkSource(const QStringList &paths);
bool cancelLinkCreation();
bool dropHardlink(const QString &targetDir);
bool dropSymlink(const QString &targetDir, bool relative = true);
bool hardlinkClone(const QString &sourceDir, const QString &targetDir);
bool symlinkClone(const QString &sourceDir, const QString &targetDir, bool relative = true);
bool smartCopy(const QString &source, const QString &targetDir);
QStringList enumerateHardlinks(const QString &path);
void showHardlinkProperties(const QString &path);
bool dropAs(const QString &targetDir, const QString &dropType, bool relative = true);
QString candidateLeafName(const QString &sourceName, const QString &kind, int attempt, bool splitExtension);
