#include "plasma_job.h"
#include "ui.h"

#include <QCoreApplication>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusReply>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

PlasmaJob::PlasmaJob(QObject *parent)
    : QObject(parent)
{
}

PlasmaJob::~PlasmaJob()
{
    if (!m_ended)
        end({});
}

bool PlasmaJob::onOwnerThread() const
{
    return QThread::currentThread() == thread();
}

void PlasmaJob::callOnOwner(const std::function<void()> &fn)
{
    if (onOwnerThread()) {
        fn();
        return;
    }
    QMetaObject::invokeMethod(this, fn, Qt::QueuedConnection);
}

bool PlasmaJob::isCancelled() const
{
    return m_cancelled.load() || (m_batchCancel && m_batchCancel->load());
}

void PlasmaJob::signalWatched(int sig)
{
#ifdef Q_OS_UNIX
    std::unordered_set<qint64> pids;
    {
        std::lock_guard<std::mutex> lock(m_pidsMutex);
        pids = m_pids;
    }
    for (qint64 pid : pids) {
        if (pid <= 0)
            continue;
        // Prefer process-group kill (child put in its own group by runTool).
        if (::kill(static_cast<pid_t>(-pid), sig) != 0)
            ::kill(static_cast<pid_t>(pid), sig);
    }
#else
    Q_UNUSED(sig);
#endif
}

void PlasmaJob::cancel()
{
    m_cancelled.store(true);
    if (m_batchCancel)
        m_batchCancel->store(true);
#ifdef Q_OS_UNIX
    if (m_suspended.load())
        signalWatched(SIGCONT);
    signalWatched(SIGTERM);
    // Escalate: extractors often ignore soft cancel while stuck on I/O.
    QThread::msleep(150);
    signalWatched(SIGKILL);
#endif
}

void PlasmaJob::watchProcess(qint64 pid)
{
    if (pid <= 0)
        return;
    {
        std::lock_guard<std::mutex> lock(m_pidsMutex);
        m_pids.insert(pid);
    }
    if (m_suspended.load())
        applyProcessState();
}

void PlasmaJob::clearProcess(qint64 pid)
{
    std::lock_guard<std::mutex> lock(m_pidsMutex);
    if (pid > 0)
        m_pids.erase(pid);
    else
        m_pids.clear();
}

void PlasmaJob::applyProcessState()
{
#ifdef Q_OS_UNIX
    signalWatched(m_suspended.load() ? SIGSTOP : SIGCONT);
#endif
    setSuspendedUi(m_suspended.load());
}

void PlasmaJob::setSuspendedUi(bool suspended)
{
    callOnOwner([this, suspended] {
        if (m_view)
            m_view->call(QStringLiteral("setSuspended"), suspended);
    });
}

bool PlasmaJob::requestView(const QString &title, const QString &iconName, const QString &appName, int capabilities)
{
    QVariantMap hints;
    hints.insert(QStringLiteral("immediate"), true);
    hints.insert(QStringLiteral("application-display-name"), appName);
    hints.insert(QStringLiteral("application-icon-name"), iconName);
    hints.insert(QStringLiteral("title"), title);
    hints.insert(QStringLiteral("infoMessage"), title);

    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.JobViewServer"),
                                                      QStringLiteral("/JobViewServer"),
                                                      QStringLiteral("org.kde.JobViewServerV2"),
                                                      QStringLiteral("requestView"));
    msg << QStringLiteral("dolphin-context-actions") << capabilities << hints;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    QString path;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        path = reply.arguments().constFirst().value<QDBusObjectPath>().path();

    if (path.isEmpty()) {
        QDBusInterface server(QStringLiteral("org.kde.JobViewServer"),
                              QStringLiteral("/JobViewServer"),
                              QStringLiteral("org.kde.JobViewServer"),
                              QDBusConnection::sessionBus());
        if (!server.isValid())
            return false;
        QDBusReply<QDBusObjectPath> v1 =
            server.call(QStringLiteral("requestView"), appName, iconName, capabilities);
        if (!v1.isValid())
            return false;
        path = v1.value().path();
    }
    if (path.isEmpty())
        return false;

    m_viewPath = path;
    m_view = std::make_unique<QDBusInterface>(QStringLiteral("org.kde.JobViewServer"),
                                              path,
                                              QStringLiteral("org.kde.JobViewV2"),
                                              QDBusConnection::sessionBus(),
                                              this);
    if (!m_view->isValid()) {
        m_view.reset();
        return false;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QStringLiteral("org.kde.JobViewServer"), path, QStringLiteral("org.kde.JobViewV2"),
                QStringLiteral("cancelRequested"), this, SLOT(onCancelRequested()));
    bus.connect(QStringLiteral("org.kde.JobViewServer"), path, QStringLiteral("org.kde.JobViewV2"),
                QStringLiteral("suspendRequested"), this, SLOT(onSuspendRequested()));
    bus.connect(QStringLiteral("org.kde.JobViewServer"), path, QStringLiteral("org.kde.JobViewV2"),
                QStringLiteral("resumeRequested"), this, SLOT(onResumeRequested()));
    return true;
}

bool PlasmaJob::begin(const QString &title, const QString &iconName, quint64 total, const QString &unit,
                      int capabilities, const QString &appName)
{
    m_total = total;
    m_unit = unit;
    m_lastPercent.store(0);
    m_cancelled.store(false);
    m_suspended.store(false);
    m_ended = false;
    clearProcess();
    m_view.reset();
    if (ui::guiBlocked())
        return false;
    if (!requestView(title, iconName, appName, capabilities))
        return false;

    m_view->call(QStringLiteral("setInfoMessage"), title);
    if (m_total > 0) {
        m_view->call(QStringLiteral("setTotalAmount"), qulonglong(m_total), m_unit);
        m_view->call(QStringLiteral("setProcessedAmount"), qulonglong(0), m_unit);
    }
    m_view->call(QStringLiteral("setPercent"), uint(0));
    pump();
    return true;
}

void PlasmaJob::setSource(const QString &path)
{
    callOnOwner([this, path] {
        if (!m_view)
            return;
        m_view->call(QStringLiteral("setDescriptionField"),
                     uint(0),
                     QStringLiteral("Source"),
                     path);
    });
}

void PlasmaJob::setDestination(const QString &path)
{
    callOnOwner([this, path] {
        if (!m_view)
            return;
        m_view->call(QStringLiteral("setDescriptionField"),
                     uint(1),
                     QStringLiteral("Destination"),
                     path);
        const QUrl url = QUrl::fromLocalFile(path);
        m_view->call(QStringLiteral("setDestUrl"), QVariant::fromValue(url));
    });
}

void PlasmaJob::setProcessed(quint64 amount)
{
    callOnOwner([this, amount] {
        if (!m_view)
            return;
        m_view->call(QStringLiteral("setProcessedAmount"), qulonglong(amount), m_unit);
        if (m_total > 0)
            setPercent(uint((amount * 100) / m_total));
    });
}

void PlasmaJob::setPercent(uint percent)
{
    percent = qMin(percent, uint(100));
    const uint prev = m_lastPercent.exchange(percent);
    if (prev == percent)
        return;
    callOnOwner([this, percent] {
        if (!m_view)
            return;
        m_view->call(QStringLiteral("setPercent"), percent);
        if (m_total > 0 && m_unit == QLatin1String("bytes"))
            m_view->call(QStringLiteral("setProcessedAmount"),
                         qulonglong((m_total * percent) / 100),
                         m_unit);
    });
}

void PlasmaJob::setInfo(const QString &msg)
{
    callOnOwner([this, msg] {
        if (!m_view)
            return;
        m_view->call(QStringLiteral("setInfoMessage"), msg);
    });
}

void PlasmaJob::setField(uint index, const QString &label, const QString &value)
{
    callOnOwner([this, index, label, value] {
        if (!m_view)
            return;
        if (value.isEmpty())
            m_view->call(QStringLiteral("clearDescriptionField"), index);
        else
            m_view->call(QStringLiteral("setDescriptionField"), index, label, value);
    });
}

void PlasmaJob::end(const QString &errorMessage)
{
    if (!onOwnerThread()) {
        QMetaObject::invokeMethod(
            this,
            [this, errorMessage] { end(errorMessage); },
            Qt::BlockingQueuedConnection);
        return;
    }
    if (m_ended)
        return;
    m_ended = true;
    clearProcess();
    if (!m_view)
        return;
    if (errorMessage.isEmpty())
        m_view->call(QStringLiteral("setPercent"), uint(100));
    else
        m_view->call(QStringLiteral("setError"), uint(1));
    m_view->call(QStringLiteral("terminate"), errorMessage);
    m_view.reset();
}

void PlasmaJob::pump()
{
    if (!onOwnerThread())
        return;
    if (QCoreApplication::instance())
        QCoreApplication::processEvents();
}

void PlasmaJob::onCancelRequested()
{
    cancel();
}

void PlasmaJob::onSuspendRequested()
{
    m_suspended.store(true);
    applyProcessState();
}

void PlasmaJob::onResumeRequested()
{
    m_suspended.store(false);
    applyProcessState();
}
