#include "archiveextractjob.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QtGlobal>

#include <algorithm>

#include <signal.h>
#include <unistd.h>

namespace {

const QList<QString> kSplitArcExts{QStringLiteral("7z"), QStringLiteral("bz2"), QStringLiteral("gz"),
                                   QStringLiteral("rar"), QStringLiteral("zip")};

QString correctFsName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QChar(QChar::Null), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("Archive") : name;
}

// Same rule as the Extract to "name/" menu label.
QString subfolderName(const QString &arcName)
{
    const int dot = arcName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0)
        return correctFsName(arcName) + QLatin1Char('~');
    const QString ext = arcName.mid(dot + 1);
    QString res = arcName.left(dot).trimmed();
    const int innerDot = res.lastIndexOf(QLatin1Char('.'));
    if (innerDot > 0) {
        const QString ext2 = res.mid(innerDot + 1);
        const QString part = ext2.toLower();
        if ((ext.compare(QLatin1String("001"), Qt::CaseInsensitive) == 0 && kSplitArcExts.contains(part))
            || (ext.compare(QLatin1String("rar"), Qt::CaseInsensitive) == 0
                && (part == QLatin1String("part001") || part == QLatin1String("part01")
                    || part == QLatin1String("part1"))))
            res = res.left(innerDot).trimmed();
    }
    return correctFsName(res);
}

// Every process in the extractor's tree: tar hands .bz2/.xz data to a child
// decompressor, so the reader of the archive is not always the direct child.
QList<qint64> processTree(qint64 root)
{
    QList<qint64> out{root};
    for (int i = 0; i < out.size(); ++i) {
        const QString base = QStringLiteral("/proc/%1/task").arg(out[i]);
        const QStringList tasks = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &task : tasks) {
            QFile children(base + QLatin1Char('/') + task + QStringLiteral("/children"));
            if (!children.open(QIODevice::ReadOnly))
                continue;
            const QList<QByteArray> ids = children.readAll().simplified().split(' ');
            for (const QByteArray &id : ids) {
                bool ok = false;
                const qint64 pid = id.toLongLong(&ok);
                if (ok && pid > 0 && !out.contains(pid))
                    out.append(pid);
            }
        }
    }
    return out;
}

// Largest read offset any process in the tree holds on the archive file.
qulonglong readOffset(qint64 root, const QString &archive)
{
    qulonglong best = 0;
    for (qint64 pid : processTree(root)) {
        const QString fdDir = QStringLiteral("/proc/%1/fd").arg(pid);
        const QStringList fds = QDir(fdDir).entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &fd : fds) {
            if (QFile::symLinkTarget(fdDir + QLatin1Char('/') + fd) != archive)
                continue;
            QFile info(QStringLiteral("/proc/%1/fdinfo/%2").arg(pid).arg(fd));
            if (!info.open(QIODevice::ReadOnly))
                continue;
            const QList<QByteArray> lines = info.readAll().split('\n');
            for (const QByteArray &line : lines)
                if (line.startsWith("pos:"))
                    best = std::max(best, line.mid(4).trimmed().toULongLong());
        }
    }
    return best;
}

QString lastLine(const QByteArray &text)
{
    return QString::fromLocal8Bit(text).trimmed().section(QLatin1Char('\n'), -1).trimmed();
}

} // namespace

QString ArchiveExtractJob::destinationFor(const QString &archive, bool toSubfolder)
{
    const QFileInfo info(archive);
    return toSubfolder ? info.absolutePath() + QLatin1Char('/') + subfolderName(info.fileName()) : info.absolutePath();
}

ArchiveExtractJob::ArchiveExtractJob(const QStringList &archives, bool toSubfolder, QObject *parent)
    : KJob(parent)
    , m_toSubfolder(toSubfolder)
{
    for (const QString &path : archives) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        m_archives << (canonical.isEmpty() ? path : canonical);
    }
    setCapabilities(KJob::Killable | KJob::Suspendable);
    m_progressTimer.setInterval(500);
    connect(&m_progressTimer, &QTimer::timeout, this, &ArchiveExtractJob::sampleProgress);
}

ArchiveExtractJob::~ArchiveExtractJob()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        signalGroup(SIGCONT);
        signalGroup(SIGKILL);
    }
}

void ArchiveExtractJob::start()
{
    qulonglong total = 0;
    for (const QString &path : m_archives) {
        const qulonglong size = qulonglong(std::max<qint64>(0, QFileInfo(path).size()));
        m_sizes << size;
        total += size;
    }
    setTotalAmount(KJob::Bytes, total);
    setTotalAmount(KJob::Files, qulonglong(m_archives.size()));
    setProcessedAmount(KJob::Files, 0);
    m_clock.start();
    m_progressTimer.start();
    QTimer::singleShot(0, this, &ArchiveExtractJob::nextArchive);
}

void ArchiveExtractJob::nextArchive()
{
    if (m_done)
        return;
    ++m_index;
    if (m_index >= m_archives.size()) {
        m_done = true;
        m_progressTimer.stop();
        if (!m_failures.isEmpty()) {
            setError(KJob::UserDefinedError);
            setErrorText(m_extracted == 0 ? m_failures.join(QLatin1Char('\n'))
                                          : QStringLiteral("%1 of %2 archives failed:\n%3")
                                                .arg(m_failures.size())
                                                .arg(m_archives.size())
                                                .arg(m_failures.join(QLatin1Char('\n'))));
        }
        emitResult();
        return;
    }
    const QString archive = m_archives.at(m_index);
    m_destination = destinationFor(archive, m_toSubfolder);
    m_currentPos = 0;
    Q_EMIT description(this,
                       m_archives.size() == 1
                           ? QStringLiteral("Extracting")
                           : QStringLiteral("Extracting %1 of %2").arg(m_index + 1).arg(m_archives.size()),
                       qMakePair(QStringLiteral("Source"), archive),
                       qMakePair(QStringLiteral("Destination"), m_destination));
    m_steps = planExtract(archive, m_destination, m_toSubfolder);
    if (m_steps.isEmpty()) {
        m_stderr = noExtractorMessage(archive).toLocal8Bit();
        archiveDone(false);
        return;
    }
    if (!QDir().mkpath(m_destination)) {
        m_stderr = QStringLiteral("cannot create %1").arg(m_destination).toLocal8Bit();
        archiveDone(false);
        return;
    }
    m_stepIndex = -1;
    runStep();
}

void ArchiveExtractJob::runStep()
{
    ++m_stepIndex;
    if (m_stepIndex >= m_steps.size()) {
        archiveDone(false);
        return;
    }
    const ExtractStep &step = m_steps.at(m_stepIndex);
    m_stderr.clear();
    m_sawBody = false;

    auto *proc = new QProcess(this);
    m_process = proc;
    proc->setProgram(step.exe);
    proc->setArguments(step.args);
    // Own process group so Pause / Cancel reach decompressor children too.
    proc->setChildProcessModifier([] { ::setpgid(0, 0); });
    if (step.stdoutTo.isEmpty())
        proc->setStandardOutputFile(QProcess::nullDevice());
    else
        proc->setStandardOutputFile(step.stdoutTo, QIODevice::Truncate);
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        m_stderr += proc->readAllStandardError();
        if (m_stderr.size() > 64 * 1024)
            m_stderr = m_stderr.right(16 * 1024);
    });
    connect(proc, &QProcess::finished, this, &ArchiveExtractJob::onStepFinished);
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && proc == m_process) {
            m_stderr = proc->errorString().toLocal8Bit();
            proc->deleteLater();
            QTimer::singleShot(0, this, &ArchiveExtractJob::runStep);
        }
    });
    proc->start();
    if (proc->waitForStarted(5000)) {
        m_pgid = proc->processId();
        if (m_paused)
            signalGroup(SIGSTOP);
    }
}

void ArchiveExtractJob::onStepFinished(int exitCode, QProcess::ExitStatus status)
{
    if (QProcess *proc = m_process) {
        m_stderr += proc->readAllStandardError();
        proc->deleteLater();
    }
    m_pgid = 0;
    if (m_done)
        return;
    const ExtractStep &step = m_steps.at(m_stepIndex);
    if (status == QProcess::NormalExit && step.okCodes.contains(exitCode)) {
        archiveDone(true);
        return;
    }
    if (!step.stdoutTo.isEmpty())
        QFile::remove(step.stdoutTo);
    runStep();
}

void ArchiveExtractJob::archiveDone(bool ok)
{
    const QString name = QFileInfo(m_archives.at(m_index)).fileName();
    if (ok) {
        ++m_extracted;
    } else {
        QString err = name + QStringLiteral(": extract failed");
        const QString detail = lastLine(m_stderr);
        if (!detail.isEmpty() && detail.size() < 200)
            err += QStringLiteral(" (") + detail + QLatin1Char(')');
        m_failures << err;
    }
    m_doneBytes += m_sizes.value(m_index);
    m_currentPos = 0;
    setProcessedAmount(KJob::Bytes, m_doneBytes);
    setProcessedAmount(KJob::Files, qulonglong(m_index + 1));
    nextArchive();
}

void ArchiveExtractJob::sampleProgress()
{
    if (m_pgid && !m_paused && m_index >= 0 && m_index < m_archives.size()) {
        const qulonglong size = m_sizes.value(m_index);
        const qulonglong pos = readOffset(m_pgid, m_archives.at(m_index));
        // zip readers seek to the central directory at the end first; do not report
        // that as progress until the reader is back in the body of the file.
        const qulonglong tail = std::max<qulonglong>(1024 * 1024, size / 100);
        if (size > tail && pos + tail < size)
            m_sawBody = true;
        const bool tailOnly = !m_sawBody && size > tail && pos + tail >= size;
        if (!tailOnly && pos > m_currentPos) {
            m_currentPos = std::min(pos, size ? size : pos);
            setProcessedAmount(KJob::Bytes, m_doneBytes + m_currentPos);
        }
    }
    const qulonglong processed = m_doneBytes + m_currentPos;
    const qint64 now = m_clock.elapsed();
    if (now - m_lastSpeedMs >= 1000) {
        const qulonglong delta = processed > m_lastSpeedBytes ? processed - m_lastSpeedBytes : 0;
        emitSpeed(m_paused ? 0 : delta * 1000 / qulonglong(now - m_lastSpeedMs));
        m_lastSpeedBytes = processed;
        m_lastSpeedMs = now;
    }
}

void ArchiveExtractJob::signalGroup(int sig)
{
    if (m_pgid > 0 && ::kill(pid_t(-m_pgid), sig) != 0)
        ::kill(pid_t(m_pgid), sig);
}

bool ArchiveExtractJob::doKill()
{
    m_done = true;
    m_progressTimer.stop();
    if (m_process && m_process->state() == QProcess::NotRunning) {
        m_process->deleteLater();
    } else if (m_process) {
        m_process->disconnect(this);
        signalGroup(SIGCONT);
        signalGroup(SIGTERM);
        QProcess *proc = m_process;
        const qint64 pgid = m_pgid;
        QTimer::singleShot(1500, proc, [proc, pgid] {
            if (proc->state() != QProcess::NotRunning && pgid > 0)
                ::kill(pid_t(-pgid), SIGKILL);
        });
        connect(proc, &QProcess::finished, proc, &QObject::deleteLater);
        proc->setParent(nullptr); // outlive this job long enough to reap the extractor
    }
    return true;
}

bool ArchiveExtractJob::doSuspend()
{
    m_paused = true;
    signalGroup(SIGSTOP);
    return true;
}

bool ArchiveExtractJob::doResume()
{
    m_paused = false;
    signalGroup(SIGCONT);
    return true;
}
