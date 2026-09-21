#include "link_ops.h"
#include "ui.h"

#include <cerrno>
#include <functional>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <unistd.h>

static const int kMaxRenameAttempts = 50;

static QString pickedSourcesFile()
{
    const QByteArray overridePath = qgetenv("DOLPHIN_LINK_SOURCES_FILE");
    if (!overridePath.isEmpty())
        return QString::fromLocal8Bit(overridePath);
    return QDir::homePath() + QStringLiteral("/.cache/dolphin-link-extension/picked_sources.json");
}

static QStringList loadSources()
{
    QFile file(pickedSourcesFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("sources")).toVariant().toStringList();
}

static void saveSources(const QStringList &sources)
{
    QDir().mkpath(QFileInfo(pickedSourcesFile()).absolutePath());
    QJsonObject obj;
    obj.insert(QStringLiteral("sources"), QJsonArray::fromStringList(sources));
    QFile file(pickedSourcesFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(obj).toJson());
}

static void clearSources()
{
    QFile::remove(pickedSourcesFile());
}

QString candidateLeafName(const QString &sourceName, const QString &kind, int attempt, bool splitExtension)
{
    if (attempt == 0)
        return sourceName;
    QString stem = sourceName;
    QString suffix;
    const int dot = sourceName.lastIndexOf(QLatin1Char('.'));
    if (splitExtension && dot > 0) {
        stem = sourceName.left(dot);
        suffix = sourceName.mid(dot);
    }
    if (attempt == 1)
        return stem + QStringLiteral(" - ") + kind + suffix;
    return stem + QStringLiteral(" - %1 (%2)%3").arg(kind).arg(attempt).arg(suffix);
}

static QString createWithRename(const QString &parent, const QString &sourceName, const QString &kind,
                                bool splitExtension, const std::function<bool(const QString &)> &create)
{
    for (int attempt = 0; attempt <= kMaxRenameAttempts; ++attempt) {
        const QString dest = parent + QLatin1Char('/') + candidateLeafName(sourceName, kind, attempt, splitExtension);
        if (create(dest))
            return dest;
        if (errno != EEXIST)
            break;
    }
    return {};
}

bool pickLinkSource(const QStringList &paths)
{
    if (paths.isEmpty())
        return false;
    QStringList valid;
    for (const QString &p : paths) {
        const QFileInfo info(p);
        if (!info.exists() && !info.isSymLink()) {
            ui::errorDialog(QStringLiteral("Pick Link Source"), QStringLiteral("Path does not exist: %1").arg(p));
            return false;
        }
        valid << info.absoluteFilePath();
    }
    saveSources(valid);
    ui::notify(QStringLiteral("Link Source Picked"),
               QStringLiteral("Picked %1 item(s) as link source").arg(valid.size()), QStringLiteral("edit-link"));
    return true;
}

bool cancelLinkCreation()
{
    if (loadSources().isEmpty()) {
        ui::infoDialog(QStringLiteral("Cancel Link Creation"), QStringLiteral("No active pick operation to cancel."));
        return false;
    }
    clearSources();
    ui::notify(QStringLiteral("Link Creation Cancelled"), QStringLiteral("Pick operation cancelled"),
               QStringLiteral("edit-delete"));
    return true;
}

static bool writableDir(const QString &path)
{
    return access(QFile::encodeName(path).constData(), W_OK) == 0;
}

bool dropHardlink(const QString &targetDir)
{
    const QStringList sources = loadSources();
    if (sources.isEmpty()) {
        ui::errorDialog(QStringLiteral("Drop Hardlink"), QStringLiteral("No link source picked. Pick a source first."));
        return false;
    }
    const QFileInfo target(targetDir);
    if (!target.isDir() || !writableDir(target.absoluteFilePath())) {
        ui::errorDialog(QStringLiteral("Drop Hardlink"), QStringLiteral("Target is not a valid directory: %1").arg(targetDir));
        return false;
    }
    QStringList created;
    QStringList failed;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        if (info.isSymLink() || !info.isFile()) {
            failed << source + QStringLiteral(": cannot hardlink this source");
            continue;
        }
        const QString dest = createWithRename(
            target.absoluteFilePath(), info.fileName(), QStringLiteral("Hardlink"), !info.isDir(),
            [&](const QString &path) { return ::link(QFile::encodeName(source).constData(), QFile::encodeName(path).constData()) == 0; });
        if (dest.isEmpty())
            failed << source;
        else
            created << dest;
    }
    if (created.size() == 1)
        ui::notify(QStringLiteral("Hardlink Created"), QStringLiteral("Created: %1").arg(created[0]),
                   QStringLiteral("edit-link"));
    else if (created.size() > 1)
        ui::notify(QStringLiteral("Hardlinks Created"), QStringLiteral("Created %1 hardlink(s)").arg(created.size()),
                   QStringLiteral("edit-link"));
    if (!failed.isEmpty()) {
        ui::errorDialog(QStringLiteral("Hardlink Errors"), failed.join(QLatin1Char('\n')));
        saveSources(failed);
    } else {
        clearSources();
    }
    return !created.isEmpty();
}

bool dropSymlink(const QString &targetDir, bool relative)
{
    const QStringList sources = loadSources();
    if (sources.isEmpty()) {
        ui::errorDialog(QStringLiteral("Drop Symlink"), QStringLiteral("No link source picked. Pick a source first."));
        return false;
    }
    const QFileInfo target(targetDir);
    if (!target.isDir() || !writableDir(target.absoluteFilePath())) {
        ui::errorDialog(QStringLiteral("Drop Symlink"), QStringLiteral("Target is not a valid directory: %1").arg(targetDir));
        return false;
    }
    QStringList created;
    QStringList failed;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        QString linkValue = source;
        if (relative)
            linkValue = QDir(target.absoluteFilePath()).relativeFilePath(info.absoluteFilePath());
        const QString dest = createWithRename(
            target.absoluteFilePath(), info.fileName(), QStringLiteral("Symlink"), !info.isDir(),
            [&](const QString &path) {
                return ::symlink(QFile::encodeName(linkValue).constData(), QFile::encodeName(path).constData()) == 0;
            });
        if (dest.isEmpty())
            failed << source;
        else
            created << dest;
    }
    if (created.size() == 1)
        ui::notify(QStringLiteral("Symlink Created"), QStringLiteral("Created: %1").arg(created[0]),
                   QStringLiteral("edit-link"));
    else if (created.size() > 1)
        ui::notify(QStringLiteral("Symlinks Created"), QStringLiteral("Created %1 symlink(s)").arg(created.size()),
                   QStringLiteral("edit-link"));
    if (!failed.isEmpty()) {
        ui::errorDialog(QStringLiteral("Symlink Errors"), failed.join(QLatin1Char('\n')));
        saveSources(failed);
    } else {
        clearSources();
    }
    return !created.isEmpty();
}

static bool copyTreeHardlink(const QString &source, const QString &dest)
{
    QDir().mkpath(dest);
    QDir src(source);
    const auto entries = src.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        const QString out = dest + QLatin1Char('/') + entry.fileName();
        if (QFileInfo(entry.absoluteFilePath()) == QFileInfo(dest))
            continue;
        if (entry.isSymLink()) {
            if (::symlink(QFile::encodeName(entry.symLinkTarget()).constData(), QFile::encodeName(out).constData()) != 0)
                return false;
        } else if (entry.isDir()) {
            if (!copyTreeHardlink(entry.absoluteFilePath(), out))
                return false;
        } else if (::link(QFile::encodeName(entry.absoluteFilePath()).constData(), QFile::encodeName(out).constData()) != 0) {
            return false;
        }
    }
    return true;
}

bool hardlinkClone(const QString &sourceDir, const QString &targetDir)
{
    const QFileInfo source(sourceDir);
    if (!source.isDir()) {
        ui::errorDialog(QStringLiteral("Hardlink Clone"), QStringLiteral("Source is not a valid directory: %1").arg(sourceDir));
        return false;
    }
    const QFileInfo target(targetDir);
    const QString clone = createWithRename(
        target.absolutePath(), target.fileName(), QStringLiteral("Hardlink Clone"), false,
        [](const QString &path) { return QDir().mkdir(path); });
    if (clone.isEmpty() || !copyTreeHardlink(source.absoluteFilePath(), clone)) {
        ui::errorDialog(QStringLiteral("Hardlink Clone"), QStringLiteral("Clone failed"));
        QDir(clone).removeRecursively();
        return false;
    }
    ui::notify(QStringLiteral("Hardlink Clone Created"), QFileInfo(clone).fileName(), QStringLiteral("folder"));
    return true;
}

bool symlinkClone(const QString &sourceDir, const QString &targetDir, bool relative)
{
    const QFileInfo source(sourceDir);
    if (!source.isDir()) {
        ui::errorDialog(QStringLiteral("Symlink Clone"), QStringLiteral("Source is not a valid directory: %1").arg(sourceDir));
        return false;
    }
    const QFileInfo target(targetDir);
    const QString clone = createWithRename(
        target.absolutePath(), target.fileName(), QStringLiteral("Symlink Clone"), false,
        [](const QString &path) { return QDir().mkdir(path); });
    if (clone.isEmpty()) {
        ui::errorDialog(QStringLiteral("Symlink Clone"), QStringLiteral("Clone failed"));
        return false;
    }
    QDir src(source.absoluteFilePath());
    const auto entries = src.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        if (entry.absoluteFilePath() == clone)
            continue;
        const QString out = clone + QLatin1Char('/') + entry.fileName();
        if (entry.isDir() && !entry.isSymLink()) {
            QDir().mkpath(out);
        } else {
            const QString value = relative ? QDir(clone).relativeFilePath(entry.absoluteFilePath())
                                           : entry.absoluteFilePath();
            ::symlink(QFile::encodeName(value).constData(), QFile::encodeName(out).constData());
        }
    }
    ui::notify(QStringLiteral("Symlink Clone Created"), QFileInfo(clone).fileName(), QStringLiteral("folder"));
    return true;
}

bool smartCopy(const QString &source, const QString &targetDir)
{
    const QFileInfo src(source);
    const QFileInfo target(targetDir);
    if (!target.isDir()) {
        ui::errorDialog(QStringLiteral("Smart Copy"), QStringLiteral("Target is not a valid directory: %1").arg(targetDir));
        return false;
    }
    const bool split = !src.isDir();
    if (src.isDir()) {
        const QString dest = createWithRename(target.absoluteFilePath(), src.fileName(), QStringLiteral("Smart Copy"),
                                              false, [](const QString &path) { return QDir().mkdir(path); });
        if (dest.isEmpty())
            return false;
        QDir().mkpath(dest);
        const auto entries = QDir(src.absoluteFilePath()).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo &entry : entries) {
            if (entry.absoluteFilePath() == dest)
                continue;
            QFile::copy(entry.absoluteFilePath(), dest + QLatin1Char('/') + entry.fileName());
        }
        ui::notify(QStringLiteral("Smart Copy Created"), QFileInfo(dest).fileName(), QStringLiteral("folder"));
        return true;
    }
    const QString dest = createWithRename(
        target.absoluteFilePath(), src.fileName(), QStringLiteral("Smart Copy"), split,
        [](const QString &path) { return QFile(path).open(QIODevice::WriteOnly | QIODevice::NewOnly); });
    if (dest.isEmpty() || !QFile::copy(src.absoluteFilePath(), dest)) {
        ui::errorDialog(QStringLiteral("Smart Copy"), QStringLiteral("Copy failed"));
        return false;
    }
    ui::notify(QStringLiteral("Smart Copy Created"), QFileInfo(dest).fileName(), QStringLiteral("folder"));
    return true;
}

QStringList enumerateHardlinks(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    QStringList siblings;
    QDirIterator it(info.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().absoluteFilePath() == info.absoluteFilePath()
            || it.fileInfo().size() != info.size())
            continue;
        siblings << it.fileInfo().absoluteFilePath();
        if (siblings.size() > 100)
            break;
    }
    siblings.sort();
    return siblings;
}

void showHardlinkProperties(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        ui::errorDialog(QStringLiteral("Link Properties"), QStringLiteral("File does not exist: %1").arg(path));
        return;
    }
    QString msg;
    if (info.isSymLink()) {
        msg = QStringLiteral("<b>Symbolic Link</b><br><br>Target: %1").arg(info.symLinkTarget());
    } else if (info.isFile()) {
        msg = QStringLiteral("<b>Regular File</b><br><br>Path: %1").arg(info.absoluteFilePath());
    } else {
        msg = QStringLiteral("<b>Directory</b><br><br>Path: %1").arg(info.absoluteFilePath());
    }
    ui::infoDialog(QStringLiteral("Link Properties"), msg, 600, 400);
}

bool dropAs(const QString &targetDir, const QString &dropType, bool relative)
{
    const QStringList sources = loadSources();
    if (sources.isEmpty()) {
        ui::errorDialog(QStringLiteral("Drop As"), QStringLiteral("No link source picked. Pick a source first."));
        return false;
    }
    if (dropType == QLatin1String("hardlink"))
        return dropHardlink(targetDir);
    if (dropType == QLatin1String("symlink"))
        return dropSymlink(targetDir, relative);
    if (dropType == QLatin1String("hardlink-clone")) {
        if (sources.size() != 1)
            return false;
        const bool ok = hardlinkClone(sources[0], targetDir + QLatin1Char('/') + QFileInfo(sources[0]).fileName());
        if (ok)
            clearSources();
        return ok;
    }
    if (dropType == QLatin1String("symlink-clone")) {
        if (sources.size() != 1)
            return false;
        const bool ok = symlinkClone(sources[0], targetDir + QLatin1Char('/') + QFileInfo(sources[0]).fileName(), relative);
        if (ok)
            clearSources();
        return ok;
    }
    if (dropType == QLatin1String("smart-copy")) {
        if (sources.size() != 1)
            return false;
        const bool ok = smartCopy(sources[0], targetDir);
        if (ok)
            clearSources();
        return ok;
    }
    ui::errorDialog(QStringLiteral("Drop As"), QStringLiteral("Unknown drop type: %1").arg(dropType));
    return false;
}
