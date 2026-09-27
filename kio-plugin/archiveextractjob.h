#pragma once

#include "extract_plan.h"

#include <KJob>

#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>

// One "Extract Here" / "Extract to" click as a single native KJob inside Dolphin,
// covering every selected archive — the same model as copying several files.
// Registered with the KIO job tracker it sits in Dolphin's notification group
// with combined byte progress, "N of M" files, speed, Pause and Cancel. Each
// click is its own job, so separate selections are paused/cancelled independently.
class ArchiveExtractJob : public KJob
{
    Q_OBJECT
public:
    ArchiveExtractJob(const QStringList &archives, bool toSubfolder, QObject *parent = nullptr);
    ~ArchiveExtractJob() override;

    void start() override;

    static QString destinationFor(const QString &archive, bool toSubfolder);

protected:
    bool doKill() override;
    bool doSuspend() override;
    bool doResume() override;

private:
    void nextArchive();
    void runStep();
    void onStepFinished(int exitCode, QProcess::ExitStatus status);
    void archiveDone(bool ok);
    void sampleProgress();
    void signalGroup(int sig);

    QStringList m_archives;
    QList<qulonglong> m_sizes;
    bool m_toSubfolder = false;
    int m_index = -1;
    qulonglong m_doneBytes = 0; // bytes of archives already finished
    qulonglong m_currentPos = 0;
    QString m_destination;
    QList<ExtractStep> m_steps;
    int m_stepIndex = -1;
    QPointer<QProcess> m_process;
    qint64 m_pgid = 0;
    QTimer m_progressTimer;
    QElapsedTimer m_clock;
    qulonglong m_lastSpeedBytes = 0;
    qint64 m_lastSpeedMs = 0;
    bool m_sawBody = false;
    bool m_done = false;
    bool m_paused = false;
    QByteArray m_stderr;
    QStringList m_failures;
    int m_extracted = 0;
};
