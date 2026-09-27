#include "archive_ops.h"
#include "extract_plan.h"
#include "plasma_job.h"
#include "ui.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <optional>

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

static QString destinationDir(const QStringList &paths)
{
    return QFileInfo(paths[0]).absolutePath();
}

static int parse7zPercent(const QByteArray &chunk)
{
    static const QRegularExpression re(QStringLiteral("(\\d{1,3})%"));
    int last = -1;
    auto it = re.globalMatch(QString::fromLocal8Bit(chunk));
    while (it.hasNext()) {
        const int n = it.next().captured(1).toInt();
        if (n >= 0 && n <= 100)
            last = n;
    }
    return last;
}

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

static void adoptProcessGroup(qint64 pid)
{
#ifdef Q_OS_UNIX
    if (pid > 0)
        ::setpgid(static_cast<pid_t>(pid), static_cast<pid_t>(pid));
#else
    Q_UNUSED(pid);
#endif
}

static int runTool(const QString &exe, const QStringList &args, const QString &cwd, QByteArray *out,
                   PlasmaJob *job, const QList<int> &okCodes = {0}, bool parseProgress = false)
{
    if (exe.isEmpty())
        return 1;
    QProcess proc;
    if (!cwd.isEmpty())
        proc.setWorkingDirectory(cwd);
    // Separate channels: progress (stdout) vs errors (stderr) do not fight each other.
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(exe, args);
    if (!proc.waitForStarted(15000))
        return 1;
    const qint64 pid = proc.processId();
    adoptProcessGroup(pid);
    if (job)
        job->watchProcess(pid);
    QByteArray buf;
    // With no progress stream, behave like a terminal wait — poll infrequently.
    const int pollMs = parseProgress ? 250 : 1000;
    while (!proc.waitForFinished(pollMs)) {
        if (parseProgress) {
            const QByteArray chunk = proc.readAllStandardOutput();
            buf += chunk;
            if (job && !job->isCancelled()) {
                const int pct = parse7zPercent(chunk.isEmpty() ? buf : chunk);
                if (pct >= 0)
                    job->setPercent(uint(pct));
            }
        } else {
            buf += proc.readAllStandardOutput();
            buf += proc.readAllStandardError();
        }
        if (job) {
            if (job->isCancelled()) {
#ifdef Q_OS_UNIX
                if (pid > 0) {
                    ::kill(static_cast<pid_t>(pid), SIGCONT);
                    ::kill(static_cast<pid_t>(-pid), SIGTERM);
                    if (!proc.waitForFinished(500))
                        ::kill(static_cast<pid_t>(-pid), SIGKILL);
                }
#endif
                proc.kill();
                proc.waitForFinished(2000);
                job->clearProcess(pid);
                return 1;
            }
            job->pump();
        } else if (QCoreApplication::instance()) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        }
    }
    if (job)
        job->clearProcess(pid);
    buf += proc.readAllStandardOutput();
    buf += proc.readAllStandardError();
    if (out)
        *out = buf;
    const int code = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : 1;
    return okCodes.contains(code) ? 0 : 1;
}

static int run7z(const QStringList &args, const QString &cwd, QByteArray *out, PlasmaJob *job = nullptr,
                 SevenIo io = SevenIo::Quiet)
{
    const QString exe = find7z();
    if (exe.isEmpty())
        return 1;
    const bool parseProgress = (io == SevenIo::Progress) && job;
    return runTool(exe, with7zIoFlags(args, io), cwd, out, job, {0, 1}, parseProgress);
}

static void startDetached(const QString &exe, const QStringList &args)
{
    if (!QProcess::startDetached(exe, args)) {
        ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Cannot start %1").arg(exe));
    }
}

static int decompressFile(const ExtractStep &step, QByteArray *out, PlasmaJob *job)
{
    if (step.exe.isEmpty())
        return 1;
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(step.exe, step.args);
    if (!proc.waitForStarted(15000))
        return 1;
    const qint64 pid = proc.processId();
    adoptProcessGroup(pid);
    if (job)
        job->watchProcess(pid);
    QFile dest(step.stdoutTo);
    if (!dest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (job)
            job->clearProcess(pid);
        return 1;
    }
    QByteArray err;
    while (!proc.waitForFinished(200)) {
        dest.write(proc.readAllStandardOutput());
        err += proc.readAllStandardError();
        if (job) {
            if (job->isCancelled()) {
#ifdef Q_OS_UNIX
                if (pid > 0) {
                    ::kill(static_cast<pid_t>(-pid), SIGTERM);
                    if (!proc.waitForFinished(500))
                        ::kill(static_cast<pid_t>(-pid), SIGKILL);
                }
#endif
                proc.kill();
                proc.waitForFinished(2000);
                job->clearProcess(pid);
                dest.remove();
                return 1;
            }
            job->pump();
        }
    }
    if (job)
        job->clearProcess(pid);
    dest.write(proc.readAllStandardOutput());
    err += proc.readAllStandardError();
    dest.close();
    if (out)
        *out = err;
    const int code = proc.exitStatus() == QProcess::NormalExit ? proc.exitCode() : 1;
    return code == 0 ? 0 : 1;
}

static int runStep(const ExtractStep &step, QByteArray *out, PlasmaJob *job)
{
    if (!step.stdoutTo.isEmpty())
        return decompressFile(step, out, job);
    return runTool(step.exe, step.args, {}, out, job, step.okCodes);
}

static int extractOne(const QString &path, const QString &dest, bool elimDup, PlasmaJob *job, QByteArray *out)
{
    QDir().mkpath(dest);
    const QList<ExtractStep> steps = planExtract(path, dest, elimDup);
    if (steps.isEmpty()) {
        if (out)
            *out = noExtractorMessage(path).toUtf8();
        return 1;
    }
    for (const ExtractStep &step : steps) {
        if (job) {
            if (job->isCancelled())
                return 1;
            job->setInfo(commandLine(step));
        }
        if (runStep(step, out, job) == 0)
            return 0;
    }
    return 1;
}

static QString extractDestination(const QString &path, bool toSubfolder)
{
    const QString parent = QFileInfo(path).absolutePath();
    return toSubfolder ? parent + QLatin1Char('/') + extractSubfolderName(QFileInfo(path).fileName()) : parent;
}

static QString failureText(const QString &name, const QByteArray &out)
{
    // Keep failures on the job / notification — never a modal dump of tool stdout.
    QString err = name + QLatin1String(": extract failed");
    const QString detail = QString::fromLocal8Bit(out).trimmed();
    const QString last = detail.section(QLatin1Char('\n'), -1).trimmed();
    if (!last.isEmpty() && last.size() < 160)
        err += QLatin1String(" (") + last + QLatin1Char(')');
    return err;
}

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#endif

// Per-user cap on extractors running at once across all helper processes, so a
// 200-archive selection does not start 200 disk-bound extractors together. Each
// archive stays its own job; waiting jobs show a "Waiting" state until a slot frees.
// DOLPHIN_CONTEXT_ACTIONS_EXTRACT_JOBS=N changes the cap (0 = no cap).
class ExtractSlot
{
public:
    ~ExtractSlot()
    {
#ifdef Q_OS_UNIX
        if (m_fd >= 0)
            ::close(m_fd);
#endif
    }

    // Returns false only when cancelled while waiting.
    bool acquire(PlasmaJob *job)
    {
#ifdef Q_OS_UNIX
        bool ok = false;
        int cap = qEnvironmentVariableIntValue("DOLPHIN_CONTEXT_ACTIONS_EXTRACT_JOBS", &ok);
        if (!ok)
            cap = 2;
        if (cap <= 0)
            return true;
        QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (base.isEmpty())
            base = QDir::tempPath();
        const QString dir = base + QStringLiteral("/dolphin-context-actions");
        QDir().mkpath(dir);
        bool announced = false;
        while (true) {
            for (int i = 0; i < cap; ++i) {
                const QByteArray file = QFile::encodeName(dir + QStringLiteral("/extract-slot-%1.lock").arg(i));
                const int fd = ::open(file.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
                if (fd < 0)
                    return true; // cannot gate; run rather than stall forever
                if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
                    m_fd = fd;
                    if (announced && job)
                        job->setField(2, QStringLiteral("State"), {});
                    return true;
                }
                ::close(fd);
            }
            if (job) {
                if (!announced) {
                    job->setField(2, QStringLiteral("State"), QStringLiteral("Waiting for a free extract slot"));
                    announced = true;
                }
                if (job->isCancelled())
                    return false;
                job->pump();
            }
            QThread::msleep(250);
        }
#else
        Q_UNUSED(job);
        return true;
#endif
    }

private:
    int m_fd = -1;
};

// One archive, one Plasma job, titled with the exact command it runs.
static int extractSingle(const QString &path, bool toSubfolder)
{
    const QString dest = extractDestination(path, toSubfolder);
    const QString name = QFileInfo(path).fileName();
    const QList<ExtractStep> steps = planExtract(path, dest, toSubfolder);

    QObject guard;
    PlasmaJob job(&guard);
    const QString title = steps.isEmpty() ? QStringLiteral("Extracting %1").arg(name) : commandLine(steps.first());
    const bool tracked = job.begin(title, QStringLiteral("archive-extract"), 0, QStringLiteral("bytes"),
                                   PlasmaJob::Killable | PlasmaJob::Suspendable);
    if (tracked) {
        job.setSource(path);
        job.setDestination(dest);
    }

    QByteArray out;
    int rc = 1;
    ExtractSlot slot;
    // Only jobs shown in Plasma queue for a slot; headless/CLI runs never wait on them.
    if (!steps.isEmpty() && (!tracked || slot.acquire(&job)))
        rc = extractOne(path, dest, toSubfolder, tracked ? &job : nullptr, &out);
    else if (steps.isEmpty())
        out = noExtractorMessage(path).toUtf8();

    const bool cancelled = job.isCancelled();
    const QString err = rc == 0 ? QString() : (cancelled ? QStringLiteral("cancelled") : failureText(name, out));
    if (tracked) {
        job.end(err);
    } else if (!err.isEmpty() && !cancelled) {
        ui::notify(QStringLiteral("Archive"), err, QStringLiteral("dialog-warning"));
    } else if (rc == 0 && !ui::guiBlocked()) {
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Extracted %1").arg(name), QStringLiteral("ark"));
    }
    return rc;
}

// Headless / test mode: extract in-process one after another and report once.
static int extractSequential(const QStringList &paths, bool toSubfolder)
{
    int ok = 0;
    QStringList errors;
    for (const QString &path : paths) {
        QByteArray out;
        const QString dest = extractDestination(path, toSubfolder);
        if (extractOne(path, dest, toSubfolder, nullptr, &out) == 0)
            ++ok;
        else
            errors << failureText(QFileInfo(path).fileName(), out);
    }
    if (!errors.isEmpty()) {
        const QString err = errors.join(QLatin1Char('\n'));
        ui::notify(QStringLiteral("Archive"), err.size() > 400 ? err.left(400) + QLatin1String("…") : err,
                   QStringLiteral("dialog-warning"));
    }
    return ok == 0 ? 1 : 0;
}

static int extractEach(const QStringList &paths, bool toSubfolder, const QString &action)
{
    if (paths.isEmpty())
        return 1;
    if (paths.size() == 1)
        return extractSingle(paths.first(), toSubfolder);
    if (ui::guiBlocked())
        return extractSequential(paths, toSubfolder);

    // One independent helper process per archive: each owns its own Plasma job
    // (own progress, Pause, Cancel), exactly like running the command in a terminal.
    const QString self = QCoreApplication::applicationFilePath();
    QStringList failed;
    for (const QString &path : paths) {
        if (self.isEmpty()
            || !QProcess::startDetached(self, {QStringLiteral("--archive"), action, path}))
            failed << QFileInfo(path).fileName();
    }
    if (!failed.isEmpty()) {
        ui::notify(QStringLiteral("Archive"),
                   QStringLiteral("Could not start extraction for: %1").arg(failed.join(QStringLiteral(", "))),
                   QStringLiteral("dialog-warning"));
    }
    return failed.size() == paths.size() ? 1 : 0;
}

static QString compressTo(const QStringList &paths, const QString &suffix, const QString &arcType, PlasmaJob *job)
{
    QString useSuffix = suffix;
    QString useType = arcType;
    const QString seven = find7z();
    if (useType == QLatin1String("7z") && seven.isEmpty()) {
        useType = QStringLiteral("zip");
        useSuffix = QStringLiteral(".zip");
    }

    const QString dest = destinationDir(paths);
    const QString archive = dest + QLatin1Char('/') + createArchiveName(paths) + useSuffix;
    QStringList names;
    const QDir destDir(dest);
    for (const QString &path : paths)
        names << destDir.relativeFilePath(QFileInfo(path).absoluteFilePath());
    if (job) {
        job->setSource(paths.size() == 1 ? paths.first() : dest);
        job->setDestination(archive);
        job->setInfo(QStringLiteral("Compressing"));
    }
    QByteArray out;
    if (!seven.isEmpty()) {
        QStringList args{QStringLiteral("a"), QStringLiteral("-t") + useType, archive};
        args << names;
        if (run7z(args, dest, &out, job, SevenIo::Progress) != 0) {
            ui::notify(QStringLiteral("Archive"),
                       QStringLiteral("Failed to create %1").arg(QFileInfo(archive).fileName()),
                       QStringLiteral("dialog-warning"));
            return {};
        }
        return archive;
    }

    if (useType == QLatin1String("zip")) {
        const QString zip = findExe({QStringLiteral("zip")});
        if (!zip.isEmpty()) {
            QStringList args{QStringLiteral("-r"), QStringLiteral("-q"), archive};
            args << names;
            if (runTool(zip, args, dest, &out, job) != 0) {
                ui::notify(QStringLiteral("Archive"),
                           QStringLiteral("Failed to create %1").arg(QFileInfo(archive).fileName()),
                           QStringLiteral("dialog-warning"));
                return {};
            }
            return archive;
        }
        const QString bsdtar = findExe({QStringLiteral("bsdtar")});
        if (!bsdtar.isEmpty()) {
            QStringList args{QStringLiteral("-a"), QStringLiteral("-cf"), archive};
            args << names;
            if (runTool(bsdtar, args, dest, &out, job) != 0) {
                ui::notify(QStringLiteral("Archive"),
                           QStringLiteral("Failed to create %1").arg(QFileInfo(archive).fileName()),
                           QStringLiteral("dialog-warning"));
                return {};
            }
            return archive;
        }
    }

    const QString tar = findExe({QStringLiteral("tar")});
    if (!tar.isEmpty()) {
        const QString tgz = dest + QLatin1Char('/') + createArchiveName(paths) + QStringLiteral(".tar.gz");
        if (job)
            job->setDestination(tgz);
        QStringList args{QStringLiteral("-czf"), tgz};
        args << names;
        if (runTool(tar, args, dest, &out, job) != 0) {
            ui::notify(QStringLiteral("Archive"),
                       QStringLiteral("Failed to create %1").arg(QFileInfo(tgz).fileName()),
                       QStringLiteral("dialog-warning"));
            return {};
        }
        return tgz;
    }
    ui::errorDialog(QStringLiteral("Archive"),
                    QStringLiteral("No compressor found. Install zip, tar, or 7z."));
    return {};
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

static quint32 crc32Update(quint32 crc, const char *data, qsizetype len)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    for (qsizetype i = 0; i < len; ++i)
        crc = table[(crc ^ quint8(data[i])) & 0xffu] ^ (crc >> 8);
    return crc;
}

static QString crc32Hex(QIODevice *dev)
{
    quint32 crc = 0xffffffffu;
    char buf[65536];
    while (true) {
        const qint64 n = dev->read(buf, sizeof(buf));
        if (n <= 0)
            break;
        crc = crc32Update(crc, buf, n);
    }
    crc ^= 0xffffffffu;
    return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0'));
}

static std::optional<QCryptographicHash::Algorithm> qtHashAlgo(const QString &key)
{
    if (key == QLatin1String("md5"))
        return QCryptographicHash::Md5;
    if (key == QLatin1String("sha1"))
        return QCryptographicHash::Sha1;
    if (key == QLatin1String("sha256"))
        return QCryptographicHash::Sha256;
    if (key == QLatin1String("sha384"))
        return QCryptographicHash::Sha384;
    if (key == QLatin1String("sha512"))
        return QCryptographicHash::Sha512;
    if (key == QLatin1String("sha3-256"))
        return QCryptographicHash::Sha3_256;
    return std::nullopt;
}

static QByteArray hashFileNative(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QFileInfo(path).fileName().toUtf8() + ": unreadable\n";
    QString digest;
    if (key == QLatin1String("crc32")) {
        digest = crc32Hex(&f);
    } else {
        const auto algo = qtHashAlgo(key);
        if (!algo)
            return {};
        QCryptographicHash h(*algo);
        h.addData(&f);
        digest = QString::fromLatin1(h.result().toHex());
    }
    return (digest + QLatin1String("  ") + QFileInfo(path).fileName() + QLatin1Char('\n')).toUtf8();
}

static QByteArray hashPathNative(const QString &path, const QString &key)
{
    if (!QFileInfo(path).isDir())
        return hashFileNative(path, key);
    QByteArray all;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        all += hashFileNative(it.filePath(), key);
    }
    return all;
}

static int testNative(const QString &path, QByteArray *out, PlasmaJob *job)
{
    const QString kind = archiveKind(path);
    if (kind == QLatin1String("zip")) {
        const QString unzip = findExe({QStringLiteral("unzip")});
        if (!unzip.isEmpty())
            return runTool(unzip, {QStringLiteral("-t"), QStringLiteral("-qq"), path}, {}, out, job);
    }
    if (kind == QLatin1String("tar") || kind == QLatin1String("iso")) {
        const QString tar = findExe({QStringLiteral("tar"), QStringLiteral("bsdtar")});
        if (!tar.isEmpty())
            return runTool(tar, {QStringLiteral("-tf"), path}, {}, out, job);
    }
    if (kind == QLatin1String("rar")) {
        const QString unrar = findExe({QStringLiteral("unrar")});
        if (!unrar.isEmpty())
            return runTool(unrar, {QStringLiteral("t"), QStringLiteral("-idq"), path}, {}, out, job);
    }
    if (kind == QLatin1String("gz") || kind == QLatin1String("bz2") || kind == QLatin1String("xz")
        || kind == QLatin1String("zst")) {
        if (out)
            *out = QByteArray("compressed file present");
        return QFileInfo::exists(path) ? 0 : 1;
    }
    if (out)
        *out = QByteArray("No tester found for this archive (install unzip/tar/unrar or 7z).");
    return 1;
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

    auto withCompressJob = [&](const QString &suffix, const QString &arcType) {
        PlasmaJob job;
        job.begin(QStringLiteral("Compressing archive"), QStringLiteral("archive-insert"), 1);
        const QString archive = compressTo(paths, suffix, arcType, &job);
        job.setProcessed(1);
        job.end(archive.isEmpty() ? QStringLiteral("Compression failed") : QString());
        return archive;
    };

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
        PlasmaJob job;
        job.begin(QStringLiteral("Computing checksum"), QStringLiteral("document-encrypt"), quint64(paths.size()));
        const QString dest = destinationDir(paths);
        const QDir destDir(dest);
        const bool have7z = !find7z().isEmpty();
        const bool nativeOk = key == QLatin1String("crc32") || qtHashAlgo(key).has_value()
            || key == QLatin1String("all");
        if (!have7z && !nativeOk) {
            job.end(QStringLiteral("Needs 7z"));
            ui::infoDialog(QStringLiteral("CRC SHA — %1").arg(method),
                           QStringLiteral("%1 needs 7z, which is not installed.").arg(method), 520, 200);
            return 1;
        }
        QByteArray all;
        QStringList errors;
        for (int i = 0; i < paths.size(); ++i) {
            if (job.isCancelled()) {
                errors << QStringLiteral("cancelled");
                break;
            }
            const QString &path = paths.at(i);
            job.setSource(path);
            job.setInfo(QStringLiteral("Hashing %1").arg(QFileInfo(path).fileName()));
            QByteArray out;
            int rc = 1;
            if (have7z) {
                QStringList args{QStringLiteral("h"), QStringLiteral("-scrc") + method};
                if (QFileInfo(path).isDir())
                    args << QStringLiteral("-r");
                args << QStringLiteral("--") << destDir.relativeFilePath(path);
                rc = run7z(args, dest, &out, &job, SevenIo::Capture);
            } else if (key == QLatin1String("all")) {
                for (const QString &part : {QStringLiteral("crc32"), QStringLiteral("md5"), QStringLiteral("sha1"),
                                            QStringLiteral("sha256"), QStringLiteral("sha512")}) {
                    out += (part.toUpper() + QLatin1Char('\n')).toUtf8();
                    out += hashPathNative(path, part);
                    out += '\n';
                }
                rc = 0;
            } else {
                out = hashPathNative(path, key);
                rc = out.isEmpty() ? 1 : 0;
            }
            if (rc != 0)
                errors << QFileInfo(path).fileName() + QLatin1String(": hash failed");
            all += out;
            if (i + 1 < paths.size())
                all += '\n';
            job.setProcessed(quint64(i + 1));
        }
        job.end(errors.join(QLatin1Char('\n')));
        ui::infoDialog(QStringLiteral("CRC SHA — %1").arg(method), QString::fromUtf8(all), 640, 400);
        return errors.size() == paths.size() ? 1 : 0;
    }

    if (action == QLatin1String("extract-here"))
        return extractEach(paths, false, QStringLiteral("extract-here"));
    if (action == QLatin1String("extract-to"))
        return extractEach(paths, true, QStringLiteral("extract-to"));
    if (action == QLatin1String("compress-to-7z")) {
        const QString archive = withCompressJob(QStringLiteral(".7z"), QStringLiteral("7z"));
        if (archive.isEmpty())
            return 1;
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Created %1").arg(QFileInfo(archive).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("compress-to-zip")) {
        const QString archive = withCompressJob(QStringLiteral(".zip"), QStringLiteral("zip"));
        if (archive.isEmpty())
            return 1;
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Created %1").arg(QFileInfo(archive).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("compress-to-7z-email")) {
        const QString archive = withCompressJob(QStringLiteral(".7z"), QStringLiteral("7z"));
        return archive.isEmpty() ? 1 : emailAttachment(archive);
    }
    if (action == QLatin1String("compress-to-zip-email")) {
        const QString archive = withCompressJob(QStringLiteral(".zip"), QStringLiteral("zip"));
        return archive.isEmpty() ? 1 : emailAttachment(archive);
    }

    const QString ark = findExe({QStringLiteral("ark")});
    if (action == QLatin1String("open")) {
        if (paths.size() != 1 || QFileInfo(paths[0]).isDir()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Open archive needs exactly one file."));
            return 1;
        }
        const QString opener = ark.isEmpty() ? findExe({QStringLiteral("xdg-open")}) : ark;
        if (opener.isEmpty()) {
            ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("No app found to open the archive."));
            return 1;
        }
        PlasmaJob job;
        job.begin(QStringLiteral("Opening archive"), QStringLiteral("ark"), 1);
        job.setSource(paths.first());
        startDetached(opener, paths);
        job.setProcessed(1);
        job.end({});
        return 0;
    }
    if (action == QLatin1String("extract")) {
        if (!ark.isEmpty()) {
            QString dest = destinationDir(paths);
            if (paths.size() == 1)
                dest += QLatin1Char('/') + extractSubfolderName(QFileInfo(paths[0]).fileName());
            PlasmaJob job;
            job.begin(QStringLiteral("Extracting archive"), QStringLiteral("archive-extract"), quint64(paths.size()));
            job.setSource(paths.first());
            job.setDestination(dest);
            QStringList args{QStringLiteral("--batch"), QStringLiteral("--dialog"), QStringLiteral("--destination"), dest};
            args << paths;
            startDetached(ark, args);
            job.setProcessed(quint64(paths.size()));
            job.end({});
            return 0;
        }
        return extractEach(paths, true, QStringLiteral("extract-to"));
    }
    if (action == QLatin1String("compress")) {
        if (!ark.isEmpty()) {
            PlasmaJob job;
            job.begin(QStringLiteral("Add to archive"), QStringLiteral("archive-insert"), 1);
            job.setSource(paths.first());
            QStringList args{QStringLiteral("--add"), QStringLiteral("--changetofirstpath")};
            args << paths;
            startDetached(ark, args);
            job.setProcessed(1);
            job.end({});
            return 0;
        }
        const QString archive = withCompressJob(QStringLiteral(".zip"), QStringLiteral("zip"));
        if (archive.isEmpty())
            return 1;
        ui::notify(QStringLiteral("Archive"), QStringLiteral("Created %1").arg(QFileInfo(archive).fileName()),
                   QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("test")) {
        PlasmaJob job;
        job.begin(QStringLiteral("Testing archive"), QStringLiteral("ark"), quint64(paths.size()));
        const bool have7z = !find7z().isEmpty();
        QByteArray all;
        QStringList errors;
        for (int i = 0; i < paths.size(); ++i) {
            if (job.isCancelled()) {
                errors << QStringLiteral("cancelled");
                break;
            }
            const QString &path = paths.at(i);
            job.setSource(path);
            job.setInfo(QStringLiteral("Testing %1").arg(QFileInfo(path).fileName()));
            QByteArray out;
            const int rc = have7z
                               ? run7z({QStringLiteral("t"), QStringLiteral("--"), path},
                                       QFileInfo(path).absolutePath(),
                                       &out,
                                       &job,
                                       SevenIo::Capture)
                               : testNative(path, &out, &job);
            all += "=== " + QFileInfo(path).fileName().toUtf8() + " ===\n" + out;
            if (rc != 0)
                errors << QFileInfo(path).fileName() + QLatin1String(": test failed");
            job.setProcessed(quint64(i + 1));
        }
        job.end(errors.join(QLatin1Char('\n')));
        ui::infoDialog(QStringLiteral("Test archive"), QString::fromUtf8(all), 640, 400);
        return errors.size() == paths.size() ? 1 : 0;
    }
    if (action == QLatin1String("hash-generate-sha256")) {
        PlasmaJob job;
        job.begin(QStringLiteral("Writing SHA-256 files"), QStringLiteral("document-encrypt"), 1);
        const QString dest = destinationDir(paths);
        const QString sidecar = dest + QLatin1Char('/') + createArchiveName(paths, true) + QStringLiteral(".sha256");
        job.setSource(paths.first());
        job.setDestination(sidecar);
        QByteArray out;
        if (!find7z().isEmpty()) {
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
            if (run7z(args, dest, &out, &job, SevenIo::Capture) != 0) {
                job.end(QStringLiteral("SHA-256 failed"));
                ui::errorDialog(QStringLiteral("SHA-256 -> file.sha256"), QString::fromUtf8(out));
                return 1;
            }
        } else {
            QStringList files;
            for (const QString &path : paths) {
                if (QFileInfo(path).isDir()) {
                    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
                    while (it.hasNext())
                        files << it.next();
                } else {
                    files << path;
                }
            }
            for (const QString &path : files) {
                QFile in(path);
                if (!in.open(QIODevice::ReadOnly))
                    continue;
                const qint64 size = in.size();
                QCryptographicHash h(QCryptographicHash::Sha256);
                h.addData(&in);
                out += (QString::fromLatin1(h.result().toHex()) + QLatin1String("  ") + QString::number(size)
                        + QLatin1String("  ") + QFileInfo(path).fileName() + QLatin1Char('\n'))
                           .toUtf8();
            }
            if (out.isEmpty()) {
                job.end(QStringLiteral("SHA-256 failed"));
                return 1;
            }
        }
        QFile file(sidecar);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            job.end(QStringLiteral("Cannot write sidecar"));
            return 1;
        }
        file.write(out);
        job.setProcessed(1);
        const bool tracked = job.isActive();
        job.end({});
        if (!tracked)
            ui::notify(QStringLiteral("Archive"), QStringLiteral("Wrote %1").arg(QFileInfo(sidecar).fileName()),
                       QStringLiteral("ark"));
        return 0;
    }
    if (action == QLatin1String("compress-email")) {
        const QString archive = withCompressJob(QStringLiteral(".7z"), QStringLiteral("7z"));
        return archive.isEmpty() ? 1 : emailAttachment(archive);
    }
    if (action == QLatin1String("hash-test")) {
        PlasmaJob job;
        job.begin(QStringLiteral("Verifying checksums"), QStringLiteral("document-encrypt"), quint64(paths.size()));
        QString report;
        QStringList errors;
        int ok = 0;
        for (int i = 0; i < paths.size(); ++i) {
            if (job.isCancelled()) {
                errors << QStringLiteral("cancelled");
                break;
            }
            QString path = paths.at(i);
            QString target = path;
            QString sidecar = path;
            if (path.endsWith(QLatin1String(".sha256"), Qt::CaseInsensitive))
                target = path.chopped(7);
            else
                sidecar = path + QLatin1String(".sha256");
            job.setSource(target);
            job.setDestination(sidecar);
            job.setInfo(QStringLiteral("Checking %1").arg(QFileInfo(target).fileName()));
            QFile sf(sidecar);
            if (!sf.open(QIODevice::ReadOnly)) {
                report += QFileInfo(target).fileName() + QLatin1String(": missing .sha256\n");
                errors << QFileInfo(target).fileName() + QLatin1String(": missing .sha256");
                job.setProcessed(quint64(i + 1));
                continue;
            }
            const QString expected = QString::fromUtf8(sf.readAll()).trimmed().split(QRegularExpression(QStringLiteral("\\s+"))).value(0);
            QFile in(target);
            if (!in.open(QIODevice::ReadOnly)) {
                report += QFileInfo(target).fileName() + QLatin1String(": unreadable\n");
                errors << QFileInfo(target).fileName() + QLatin1String(": unreadable");
                job.setProcessed(quint64(i + 1));
                continue;
            }
            QCryptographicHash hasher(QCryptographicHash::Sha256);
            hasher.addData(&in);
            const QString got = QString::fromLatin1(hasher.result().toHex());
            const bool match = got.compare(expected, Qt::CaseInsensitive) == 0;
            report += QFileInfo(target).fileName() + (match ? QLatin1String(": OK\n") : QLatin1String(": MISMATCH\n"));
            if (match)
                ++ok;
            else
                errors << QFileInfo(target).fileName() + QLatin1String(": mismatch");
            job.setProcessed(quint64(i + 1));
        }
        job.end(errors.join(QLatin1Char('\n')));
        ui::infoDialog(QStringLiteral("Test checksum"), report, 520, 280);
        return ok == 0 ? 1 : 0;
    }

    ui::errorDialog(QStringLiteral("Archive"), QStringLiteral("Unknown Archive action: %1").arg(action));
    return 1;
}
