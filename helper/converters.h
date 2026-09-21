#pragma once

#include <QString>
#include <QStringList>

void convertAudio(const QStringList &files, const QString &format);
void videoToGif(const QStringList &files, const QString &preset);
void videoTo(const QStringList &files, const QString &ext);
void extractAudio(const QStringList &files);
QString audioPresetName(const QString &format);
QStringList audioFormats();
QString gifPresetName(const QString &preset);
