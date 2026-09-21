#include "archive_ops.h"
#include "ui.h"

#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>

static const QSet<QString> kExtractExclude = {
    QStringLiteral("3gp"), QStringLiteral("aac"), QStringLiteral("ans"), QStringLiteral("ape"),
    QStringLiteral("asc"), QStringLiteral("asm"), QStringLiteral("asp"), QStringLiteral("aspx"),
    QStringLiteral("avi"), QStringLiteral("awk"), QStringLiteral("bas"), QStringLiteral("bat"),
    QStringLiteral("bmp"), QStringLiteral("c"), QStringLiteral("cs"), QStringLiteral("cls"),
    QStringLiteral("clw"), QStringLiteral("cmd"), QStringLiteral("cpp"), QStringLiteral("csproj"),
    QStringLiteral("css"), QStringLiteral("ctl"), QStringLiteral("cxx"), QStringLiteral("def"),
    QStringLiteral("dep"), QStringLiteral("dlg"), QStringLiteral("dsp"), QStringLiteral("dsw"),
    QStringLiteral("eps"), QStringLiteral("f"), QStringLiteral("f77"), QStringLiteral("f90"),
    QStringLiteral("f95"), QStringLiteral("fla"), QStringLiteral("flac"), QStringLiteral("frm"),
    QStringLiteral("gif"), QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("hta"),
    QStringLiteral("htm"), QStringLiteral("html"), QStringLiteral("hxx"), QStringLiteral("ico"),
    QStringLiteral("idl"), QStringLiteral("inc"), QStringLiteral("ini"), QStringLiteral("inl"),
    QStringLiteral("java"), QStringLiteral("jpeg"), QStringLiteral("jpg"), QStringLiteral("js"),
    QStringLiteral("la"), QStringLiteral("lnk"), QStringLiteral("log"), QStringLiteral("mak"),
    QStringLiteral("manifest"), QStringLiteral("wmv"), QStringLiteral("mov"), QStringLiteral("mp3"),
    QStringLiteral("mp4"), QStringLiteral("mpe"), QStringLiteral("mpeg"), QStringLiteral("mpg"),
    QStringLiteral("m4a"), QStringLiteral("ofr"), QStringLiteral("ogg"), QStringLiteral("pac"),
    QStringLiteral("pas"), QStringLiteral("pdf"), QStringLiteral("php"), QStringLiteral("php3"),
    QStringLiteral("php4"), QStringLiteral("php5"), QStringLiteral("phptml"), QStringLiteral("pl"),
    QStringLiteral("pm"), QStringLiteral("png"), QStringLiteral("ps"), QStringLiteral("py"),
    QStringLiteral("pyo"), QStringLiteral("ra"), QStringLiteral("rb"), QStringLiteral("rc"),
    QStringLiteral("reg"), QStringLiteral("rka"), QStringLiteral("rm"), QStringLiteral("rtf"),
    QStringLiteral("sed"), QStringLiteral("sh"), QStringLiteral("shn"), QStringLiteral("shtml"),
    QStringLiteral("sln"), QStringLiteral("sql"), QStringLiteral("srt"), QStringLiteral("swa"),
    QStringLiteral("tcl"), QStringLiteral("tex"), QStringLiteral("tiff"), QStringLiteral("tta"),
    QStringLiteral("txt"), QStringLiteral("vb"), QStringLiteral("vcproj"), QStringLiteral("vbs"),
    QStringLiteral("mkv"), QStringLiteral("wav"), QStringLiteral("webm"), QStringLiteral("wma"),
    QStringLiteral("wv"), QStringLiteral("xml"), QStringLiteral("xsd"), QStringLiteral("xsl"),
    QStringLiteral("xslt")};

static QString extension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == name.size() - 1)
        return {};
    return name.mid(dot + 1);
}

static QString correctFsName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QChar(0), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("Archive") : name;
}

bool needsExtract(const QString &name)
{
    const QString ext = extension(name);
    if (ext.isEmpty() || ext.size() > 32)
        return true;
    return !kExtractExclude.contains(ext.toLower());
}

QString extractSubfolderName(const QString &arcName)
{
    const int dot = arcName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return correctFsName(arcName) + QLatin1Char('~');
    const QString ext = arcName.mid(dot + 1);
    QString res = QStringView{arcName}.left(dot).toString().trimmed();
    const int inner = res.lastIndexOf(QLatin1Char('.'));
    if (inner > 0) {
        const QString ext2 = res.mid(inner + 1);
        const QString part = ext2.toLower();
        const bool strip = (ext.compare(QLatin1String("001"), Qt::CaseInsensitive) == 0
                            && QStringList{QStringLiteral("7z"), QStringLiteral("bz2"), QStringLiteral("gz"),
                                           QStringLiteral("rar"), QStringLiteral("zip")}
                                   .contains(ext2.toLower()))
            || (ext.compare(QLatin1String("rar"), Qt::CaseInsensitive) == 0
                && (part == QLatin1String("part001") || part == QLatin1String("part01")
                    || part == QLatin1String("part1")));
        if (strip)
            res = QStringView{res}.left(inner).toString().trimmed();
    }
    return correctFsName(res);
}

static bool needsNumericSuffix(const QStringList &paths, const QString &name, const QStringList &exts)
{
    QSet<QString> prefixes;
    for (const QString &ext : exts)
        prefixes.insert(QStringLiteral("%1.%2").arg(name, ext).toLower());
    for (const QString &path : paths) {
        if (prefixes.contains(QFileInfo(path).fileName().toLower()))
            return true;
    }
    return false;
}

QString createArchiveName(const QStringList &paths, bool isHash)
{
    if (paths.isEmpty())
        return QStringLiteral("Archive");
    QString name = QStringLiteral("Archive");
    QString fiName;
    bool fiIsDir = false;
    if (paths.size() == 1) {
        const QFileInfo info(paths[0]);
        fiName = info.fileName();
        fiIsDir = info.isDir();
    } else {
        const QFileInfo parent(QFileInfo(paths[0]).absolutePath());
        if (!parent.fileName().isEmpty())
            name = parent.fileName();
    }
    if (!fiName.isEmpty()) {
        name = fiName;
        if (!fiIsDir && !isHash) {
            const int dot = name.indexOf(QLatin1Char('.'));
            if (dot > 0 && !name.mid(dot + 1).contains(QLatin1Char('.')))
                name = name.left(dot);
        }
    }
    name = correctFsName(name);
    const QStringList numbered = isHash ? QStringList{QStringLiteral("sha256")}
                                        : QStringList{QStringLiteral("7z"), QStringLiteral("zip"),
                                                      QStringLiteral("tar"), QStringLiteral("wim")};
    if (needsNumericSuffix(paths, name, numbered))
        name += QStringLiteral("_2");
    return name;
}

QString quotedReduced(const QString &name)
{
    QString text = name;
    if (text.size() > 64)
        text = text.left(32) + QStringLiteral(" ... ") + text.right(32);
    text.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

bool selectionWantsExtract(const QStringList &paths)
{
    if (paths.isEmpty())
        return false;
    for (const QString &path : paths) {
        if (QFileInfo(path).isDir())
            return false;
    }
    for (const QString &path : paths) {
        if (!needsExtract(QFileInfo(path).fileName()))
            return false;
    }
    return true;
}

static QString findExe(const QStringList &names)
{
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

static QString require7z()
{
    const QString exe = findExe({QStringLiteral("7z"), QStringLiteral("7za")});
    if (exe.isEmpty()) {
        ui::errorDialog(QStringLiteral("Archive"),
                        QStringLiteral("<b>7z</b> not found.\n\nInstall 7zip."));
    }
    return exe;
}

static QString destinationDir(const QStringList &paths)
{
    return QFileInfo(paths[0]).absolutePath();
}

static int run7z(const QStringList &args, const QString &cwd, QByteArray *out)
{
    const QString exe = require7z();
    if (exe.isEmpty())
        return 1;
    QProcess proc;
    if (!cwd.isEmpty())
        proc.setWorkingDirectory(cwd);
    proc.start(exe, args);
    proc.waitForFinished(-1);
    if (out)
        *out = proc.readAllStandardOutput() + proc.readAllStandardError();
    const int code = proc.exitCode();
    return (code == 0 || code == 1) ? 0 : 1;
}

static void startDetached(const QString &exe, const QStringList &args)
{
    if (!QProcess::startDetached(exe, args)) {
        ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Cannot start %1").arg(exe));
    }
}

static int extractWith7z(const QStringList &paths, const QString &dest, bool elimDup, const QString &title)
{
    QDir().mkpath(dest);
    QStringList args{QStringLiteral("x"), QStringLiteral("-y")};
    if (elimDup)
        args << QStringLiteral("-spe");
    args << QStringLiteral("-o") + dest << QStringLiteral("--") << paths;
    QByteArray out;
    if (run7z(args, {}, &out) != 0) {
        ui::errorDialog(title, QString::fromUtf8(out));
        return 1;
    }
    ui::notify(QStringLiteral("Archive"), QStringLiteral("Extracted to %1").arg(dest), QStringLiteral("ark"));
    return 0;
}

static QString compressTo(const QStringList &paths, const QString &suffix, const QString &arcType)
{
    const QString dest = destinationDir(paths);
    const QString archive = dest + QLatin1Char('/') + createArchiveName(paths) + suffix;
    QStringList names;
    const QDir destDir(dest);
    for (const QString &path : paths)
        names << destDir.relativeFilePath(QFileInfo(path).absoluteFilePath());
    QStringList args{QStringLiteral("a"), QStringLiteral("-t") + arcType, QStringLiteral("--"), archive};
    args << names;
    QByteArray out;
    if (run7z(args, dest, &out) != 0) {
        ui::errorDialog(QStringLiteral("Add to %1").arg(QFileInfo(archive).fileName()), QString::fromUtf8(out));
        return {};
    }
    return archive;
}

static int emailAttachment(const QString &archive)
{
    const QString mailer = QStandardPaths::findExecutable(QStringLiteral("xdg-email"));
    if (mailer.isEmpty()) {
        ui::errorDialog(QStringLiteral("Archive"),
                        QStringLiteral("No mailer found (xdg-email). The archive was created:\n<tt>%1</tt>").arg(archive));
        return 1;
    }
    if (QProcess::execute(mailer, {QStringLiteral("--attach"), archive}) != 0) {
        ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("xdg-email failed"));
        return 1;
    }
    return 0;
}

int runArchiveAction(const QString &action, const QStringList &files)
{
    QStringList paths;
    for (const QString &raw : files) {
        QString path = raw;
        if (path.startsWith(QLatin1String("~/")))
            path = QDir::homePath() + path.mid(1);
        const QFileInfo info(path);
        if (info.exists() || info.isSymLink())
            paths << info.absoluteFilePath();
    }
    if (paths.isEmpty()) {
        ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("No local files or folders were selected."));
        return 1;
    }

    if (action.startsWith(QLatin1String("hash-")) && action != QLatin1String("hash-generate-sha256")
        && action != QLatin1String("hash-test")) {
        const QString key = action.mid(5);
        static const QHash<QString, QString> methods{
            {QStringLiteral("crc32"), QStringLiteral("CRC32")},
            {QStringLiteral("crc64"), QStringLiteral("CRC64")},
            {QStringLiteral("xxh64"), QStringLiteral("XXH64")},
            {QStringLiteral("md5"), QStringLiteral("MD5")},
            {QStringLiteral("sha1"), QStringLiteral("SHA1")},
            {QStringLiteral("sha256"), QStringLiteral("SHA256")},
            {QStringLiteral("sha384"), QStringLiteral("SHA384")},
            {QStringLiteral("sha512"), QStringLiteral("SHA512")},
            {QStringLiteral("sha3-256"), QStringLiteral("SHA3-256")},
            {QStringLiteral("blake2sp"), QStringLiteral("BLAKE2sp")},
            {QStringLiteral("all"), QStringLiteral("*")},
        };
        const QString method = methods.value(key);
        if (method.isEmpty()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Unknown Archive action: %1").arg(action));
            return 1;
        }
        const QString dest = destinationDir(paths);
        QStringList args{QStringLiteral("h"), QStringLiteral("-scrc") + method};
        for (const QString &path : paths) {
            if (QFileInfo(path).isDir()) {
                args << QStringLiteral("-r");
                break;
            }
        }
        args << QStringLiteral("--");
        const QDir destDir(dest);
        for (const QString &path : paths)
            args << destDir.relativeFilePath(path);
        QByteArray out;
        if (run7z(args, dest, &out) != 0) {
            ui::errorDialog(QStringLiteral("CRC SHA"), QString::fromUtf8(out));
            return 1;
        }
        ui::infoDialog(QStringLiteral("CRC SHA — %1").arg(method), QString::fromUtf8(out), 640, 400);
        return 0;
    }

    if (action == QLatin1String("extract-here"))
        return extractWith7z(paths, destinationDir(paths), false, QStringLiteral("Extract Here"));
    if (action == QLatin1String("extract-to")) {
        const QString destRoot = destinationDir(paths);
        if (paths.size() == 1) {
            return extractWith7z(paths, destRoot + QLatin1Char('/') + extractSubfolderName(QFileInfo(paths[0]).fileName()),
                                 true, QStringLiteral("Extract to"));
        }
        int rc = 0;
        for (const QString &path : paths) {
            if (extractWith7z({path}, destRoot + QLatin1Char('/') + extractSubfolderName(QFileInfo(path).fileName()),
                              true, QStringLiteral("Extract to"))
                != 0)
                rc = 1;
        }
        if (rc == 0)
            ui::notify(QStringLiteral("Archive"), QStringLiteral("Extracted %1 archive(s)").arg(paths.size()),
                       QStringLiteral("ark"));
        return rc;
    }
    if (action == QLatin1String("compress-to-7z")) {
        const QString archive = compressTo(paths, QStringLiteral(".7z"), QStringLiteral("7z"));
        if (archive.isEmpty())
            return 1;
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Created %1").arg(QFileInfo(archive).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("compress-to-zip")) {
        const QString archive = compressTo(paths, QStringLiteral(".zip"), QStringLiteral("zip"));
        if (archive.isEmpty())
            return 1;
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Created %1").arg(QFileInfo(archive).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("compress-to-7z-email")) {
        const QString archive = compressTo(paths, QStringLiteral(".7z"), QStringLiteral("7z"));
        return archive.isEmpty() ? 1 : emailAttachment(archive);
    }
    if (action == QLatin1String("compress-to-zip-email")) {
        const QString archive = compressTo(paths, QStringLiteral(".zip"), QStringLiteral("zip"));
        return archive.isEmpty() ? 1 : emailAttachment(archive);
    }

    const QString ark = findExe({QStringLiteral("ark")});
    if (action == QLatin1String("open")) {
        if (paths.size() != 1 || QFileInfo(paths[0]).isDir()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Open archive needs exactly one file."));
            return 1;
        }
        if (ark.isEmpty()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("<b>ark</b> not found."));
            return 1;
        }
        startDetached(ark, paths);
        return 0;
    }
    if (action == QLatin1String("extract")) {
        if (ark.isEmpty()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("<b>ark</b> not found."));
            return 1;
        }
        QString dest = destinationDir(paths);
        if (paths.size() == 1)
            dest += QLatin1Char('/') + extractSubfolderName(QFileInfo(paths[0]).fileName());
        QStringList args{QStringLiteral("--batch"), QStringLiteral("--dialog"), QStringLiteral("--destination"), dest};
        args << paths;
        startDetached(ark, args);
        return 0;
    }
    if (action == QLatin1String("compress")) {
        if (ark.isEmpty()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("<b>ark</b> not found."));
            return 1;
        }
        QStringList args{QStringLiteral("--add"), QStringLiteral("--changetofirstpath")};
        args << paths;
        startDetached(ark, args);
        return 0;
    }
    if (action == QLatin1String("test")) {
        QStringList args{QStringLiteral("t"), QStringLiteral("--")};
        args << paths;
        QByteArray out;
        if (run7z(args, {}, &out) != 0) {
            ui::errorDialog(QStringLiteral("Test archive"), QString::fromUtf8(out));
            return 1;
        }
        ui::infoDialog(QStringLiteral("Test archive"), QString::fromUtf8(out), 640, 400);
        return 0;
    }
    if (action == QLatin1String("hash-generate-sha256")) {
        const QString dest = destinationDir(paths);
        const QString sidecar = dest + QLatin1Char('/') + createArchiveName(paths, true) + QStringLiteral(".sha256");
        QStringList args{QStringLiteral("h"), QStringLiteral("-scrcSHA256"), QStringLiteral("-ba")};
        for (const QString &path : paths) {
            if (QFileInfo(path).isDir()) {
                args << QStringLiteral("-r");
                break;
            }
        }
        args << QStringLiteral("--");
        const QDir destDir(dest);
        for (const QString &path : paths)
            args << destDir.relativeFilePath(path);
        QByteArray out;
        if (run7z(args, dest, &out) != 0) {
            ui::errorDialog(QStringLiteral("SHA-256 -> file.sha256"), QString::fromUtf8(out));
            return 1;
        }
        QFile file(sidecar);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 1;
        file.write(out);
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Wrote %1").arg(QFileInfo(sidecar).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("hash-test") || action == QLatin1String("compress-email")) {
        if (action == QLatin1String("compress-email")) {
            const QString archive = compressTo(paths, QStringLiteral(".7z"), QStringLiteral("7z"));
            return archive.isEmpty() ? 1 : emailAttachment(archive);
        }
        return runArchiveAction(QStringLiteral("test"), paths);
    }

    ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Unknown Archive action: %1").arg(action));
    return 1;
}
