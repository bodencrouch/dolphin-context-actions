#include "extract_plan.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

// Shared by the CLI helper and the Dolphin plugin: which extractor command to run
// for one archive, and how to print it as a terminal command.

QString findExe(const QStringList &names)
{
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QString find7z()
{
    return findExe({QStringLiteral("7z"), QStringLiteral("7za")});
}


QString archiveKind(const QString &path)
{
    const QString n = QFileInfo(path).fileName().toLower();
    auto has = [&](const QString &s) { return n.endsWith(s); };
    if (has(QLatin1String(".tar.gz")) || has(QLatin1String(".tgz")) || has(QLatin1String(".tar.bz2"))
        || has(QLatin1String(".tbz")) || has(QLatin1String(".tbz2")) || has(QLatin1String(".tar.xz"))
        || has(QLatin1String(".txz")) || has(QLatin1String(".tar.zst")) || has(QLatin1String(".tar.zstd"))
        || has(QLatin1String(".tar")))
        return QStringLiteral("tar");
    if (has(QLatin1String(".zip")) || has(QLatin1String(".zipx")) || has(QLatin1String(".cbz"))
        || has(QLatin1String(".jar")) || has(QLatin1String(".epub")) || has(QLatin1String(".apk")))
        return QStringLiteral("zip");
    if (has(QLatin1String(".rar")) || has(QLatin1String(".cbr")))
        return QStringLiteral("rar");
    if (has(QLatin1String(".7z")))
        return QStringLiteral("7z");
    if (has(QLatin1String(".gz")))
        return QStringLiteral("gz");
    if (has(QLatin1String(".bz2")))
        return QStringLiteral("bz2");
    if (has(QLatin1String(".xz")) || has(QLatin1String(".lzma")))
        return QStringLiteral("xz");
    if (has(QLatin1String(".zst")) || has(QLatin1String(".zstd")))
        return QStringLiteral("zst");
    if (has(QLatin1String(".iso")))
        return QStringLiteral("iso");
    return QStringLiteral("unknown");
}


// 7z stream control (-bs): o=output, e=error, p=progress; 0=off, 1=stdout, 2=stderr.
// Quiet matches a terminal batch extract: no banner, no progress spam, errors on stderr only.
// Progress keeps -bsp1 (no -bd) so Plasma can parse percents.
// Capture keeps stdout for `h`/`t` result lines.

QStringList with7zIoFlags(QStringList args, SevenIo io)
{
    for (const QString &flag : {QStringLiteral("-y"), QStringLiteral("-bd"), QStringLiteral("-bb0"),
                                QStringLiteral("-bb1"), QStringLiteral("-bb2"), QStringLiteral("-bb3"),
                                QStringLiteral("-bso0"), QStringLiteral("-bso1"), QStringLiteral("-bso2"),
                                QStringLiteral("-bse0"), QStringLiteral("-bse1"), QStringLiteral("-bse2"),
                                QStringLiteral("-bsp0"), QStringLiteral("-bsp1"), QStringLiteral("-bsp2"),
                                QStringLiteral("-mmt"), QStringLiteral("-aoa"), QStringLiteral("-aos")}) {
        args.removeAll(flag);
    }
    // -mmt with no N: 7z picks the thread count (do not hardcode).
    QStringList flags{QStringLiteral("-y"), QStringLiteral("-bb0"), QStringLiteral("-mmt")};
    switch (io) {
    case SevenIo::Quiet:
        flags << QStringLiteral("-bd") << QStringLiteral("-bso0") << QStringLiteral("-bse2")
              << QStringLiteral("-bsp0") << QStringLiteral("-aoa");
        break;
    case SevenIo::Progress:
        flags << QStringLiteral("-bso0") << QStringLiteral("-bse2") << QStringLiteral("-bsp1")
              << QStringLiteral("-aoa");
        break;
    case SevenIo::Capture:
        flags << QStringLiteral("-bd") << QStringLiteral("-bsp0");
        break;
    }
    if (args.isEmpty())
        return flags;
    return QStringList{args.constFirst()} + flags + args.mid(1);
}

static bool sevenZipListsFormat(const QByteArray &needle)
{
    static const QByteArray info = []() -> QByteArray {
        const QString exe = find7z();
        if (exe.isEmpty())
            return {};
        QProcess proc;
        proc.start(exe, {QStringLiteral("i")});
        if (!proc.waitForFinished(8000))
            return {};
        return proc.readAllStandardOutput() + proc.readAllStandardError();
    }();
    return info.contains(needle);
}

bool sevenZipHandlesKind(const QString &kind)
{
    if (kind.isEmpty() || kind == QLatin1String("7z"))
        return !find7z().isEmpty();
    // This distro's 7z often ships without RAR codecs; never send .rar to it then.
    if (kind == QLatin1String("rar"))
        return sevenZipListsFormat(QByteArrayLiteral("Rar"));
    return !find7z().isEmpty();
}



QString shellQuote(const QString &s)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_@%+=:,./-]+$"));
    if (!s.isEmpty() && safe.match(s).hasMatch())
        return s;
    QString q = s;
    q.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

// The exact terminal command this step runs (program by name, as typed in a shell).
QString commandLine(const ExtractStep &step)
{
    QStringList parts{shellQuote(QFileInfo(step.exe).fileName())};
    for (const QString &arg : step.args)
        parts << shellQuote(arg);
    if (!step.stdoutTo.isEmpty())
        parts << QStringLiteral(">") << shellQuote(step.stdoutTo);
    return parts.join(QLatin1Char(' '));
}


// Format-native tool for this archive, if one is installed (first match only,
// same preference order as before: closest to a plain terminal extract).
static std::optional<ExtractStep> nativeStep(const QString &path, const QString &dest)
{
    const QString kind = archiveKind(path);
    auto step = [](const QString &exe, const QStringList &args) {
        return std::optional<ExtractStep>(ExtractStep{exe, args, {0}, {}});
    };
    if (kind == QLatin1String("zip")) {
        if (const QString unzip = findExe({QStringLiteral("unzip")}); !unzip.isEmpty())
            return step(unzip, {QStringLiteral("-o"), QStringLiteral("-qq"), path, QStringLiteral("-d"), dest});
        if (const QString bsdtar = findExe({QStringLiteral("bsdtar")}); !bsdtar.isEmpty())
            return step(bsdtar, {QStringLiteral("-xf"), path, QStringLiteral("-C"), dest});
    } else if (kind == QLatin1String("tar") || kind == QLatin1String("iso")) {
        if (const QString tar = findExe({QStringLiteral("tar"), QStringLiteral("bsdtar")}); !tar.isEmpty())
            return step(tar, {QStringLiteral("-xf"), path, QStringLiteral("-C"), dest});
    } else if (kind == QLatin1String("rar")) {
        if (const QString unrar = findExe({QStringLiteral("unrar")}); !unrar.isEmpty())
            return step(unrar, {QStringLiteral("x"), QStringLiteral("-o+"), path, dest + QLatin1Char('/')});
        if (const QString unar = findExe({QStringLiteral("unar")}); !unar.isEmpty())
            return step(unar, {QStringLiteral("-f"), QStringLiteral("-o"), dest, path});
        if (const QString bsdtar = findExe({QStringLiteral("bsdtar")}); !bsdtar.isEmpty())
            return step(bsdtar, {QStringLiteral("-xf"), path, QStringLiteral("-C"), dest});
    } else if (kind == QLatin1String("gz") || kind == QLatin1String("bz2") || kind == QLatin1String("xz")
               || kind == QLatin1String("zst")) {
        QString tool;
        if (kind == QLatin1String("gz"))
            tool = findExe({QStringLiteral("gzip"), QStringLiteral("gunzip")});
        else if (kind == QLatin1String("bz2"))
            tool = findExe({QStringLiteral("bzip2"), QStringLiteral("bunzip2")});
        else if (kind == QLatin1String("xz"))
            tool = findExe({QStringLiteral("xz"), QStringLiteral("unxz")});
        else
            tool = findExe({QStringLiteral("zstd")});
        if (tool.isEmpty())
            return std::nullopt;
        QString outName = QFileInfo(path).completeBaseName();
        if (outName.isEmpty())
            outName = QFileInfo(path).fileName();
        return ExtractStep{tool, {QStringLiteral("-dc"), path}, {0}, dest + QLatin1Char('/') + outName};
    }
    return std::nullopt;
}

// Ordered commands to try for one archive: the format-native tool, then 7z.
QList<ExtractStep> planExtract(const QString &path, const QString &dest, bool elimDup)
{
    QList<ExtractStep> steps;
    const QString kind = archiveKind(path);
    // Many Linux 7z builds cannot open RAR; unzip/tar are also closer to a plain terminal extract.
    if (auto native = nativeStep(path, dest))
        steps << *native;
    if (sevenZipHandlesKind(kind)) {
        QStringList args{QStringLiteral("x")};
        if (elimDup)
            args << QStringLiteral("-spe");
        args << QStringLiteral("-o") + dest << QStringLiteral("--") << path;
        steps << ExtractStep{find7z(), with7zIoFlags(args, SevenIo::Quiet), {0, 1}, {}};
    }
    return steps;
}

QString noExtractorMessage(const QString &path)
{
    if (archiveKind(path) == QLatin1String("7z"))
        return QStringLiteral("7z is not installed; cannot extract .7z archives.");
    return QStringLiteral("No extractor found. Install unzip, tar, unrar, or 7z.");
}

