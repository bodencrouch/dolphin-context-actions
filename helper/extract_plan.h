#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// 7z stream control (-bs): Quiet matches a terminal batch extract, Progress keeps
// percent output for parsing, Capture keeps stdout for `h`/`t` result lines.
enum class SevenIo { Quiet, Progress, Capture };

// One concrete extractor invocation. `stdoutTo` set means the tool writes the
// payload to stdout and the caller redirects it into that file (gzip -dc src > dest).
struct ExtractStep {
    QString exe;
    QStringList args;
    QList<int> okCodes{0};
    QString stdoutTo;
};

QString findExe(const QStringList &names);
QString find7z();
QString archiveKind(const QString &path);
QStringList with7zIoFlags(QStringList args, SevenIo io);
bool sevenZipHandlesKind(const QString &kind);

QString shellQuote(const QString &s);
// The exact terminal command a step runs (program by name, as typed in a shell).
QString commandLine(const ExtractStep &step);
// Ordered commands to try for one archive: the format-native tool, then 7z.
QList<ExtractStep> planExtract(const QString &path, const QString &dest, bool elimDup);
QString noExtractorMessage(const QString &path);
