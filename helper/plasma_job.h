#pragma once

#include <QObject>
#include <QString>
#include <QtDBus/QDBusInterface>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

// Plasma copy/move-style job via org.kde.JobViewServer (no KF6).
// One JobView per logical operation. Multi-archive extracts fan out into one
// helper process per archive, so each archive owns its own view.
class PlasmaJob : public QObject
{
    Q_OBJECT
public:
    static constexpr int Killable = 1;
    static constexpr int Suspendable = 2;

    explicit PlasmaJob(QObject *parent = nullptr);
    ~PlasmaJob() override;

    bool begin(const QString &title, const QString &iconName, quint64 total = 0,
               const QString &unit = QStringLiteral("bytes"), int capabilities = Killable,
               const QString &appName = QStringLiteral("Archive"));
    void setSource(const QString &path);
    void setDestination(const QString &path);
    void setProcessed(quint64 amount);
    void setPercent(uint percent);
    void setInfo(const QString &msg);
    // Extra "label: value" row under Details (index 0 and 1 are Source/Destination).
    void setField(uint index, const QString &label, const QString &value);
    void end(const QString &errorMessage = {});
    void cancel();
    // Track a child tool PID (may be several concurrent workers on one batch job).
    void watchProcess(qint64 pid);
    void clearProcess(qint64 pid = 0);
    void setBatchCancelFlag(std::atomic<bool> *flag) { m_batchCancel = flag; }
    bool isCancelled() const;
    bool isSuspended() const { return m_suspended.load(); }
    bool isActive() const { return m_view != nullptr; }
    uint percent() const { return m_lastPercent.load(); }
    void pump();

private Q_SLOTS:
    void onCancelRequested();
    void onSuspendRequested();
    void onResumeRequested();

private:
    bool onOwnerThread() const;
    void callOnOwner(const std::function<void()> &fn);
    bool requestView(const QString &title, const QString &iconName, const QString &appName, int capabilities);
    void applyProcessState();
    void setSuspendedUi(bool suspended);
    void signalWatched(int sig);

    std::unique_ptr<QDBusInterface> m_view;
    QString m_viewPath;
    quint64 m_total = 0;
    QString m_unit;
    std::atomic<uint> m_lastPercent{0};
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_suspended{false};
    std::mutex m_pidsMutex;
    std::unordered_set<qint64> m_pids;
    std::atomic<bool> *m_batchCancel = nullptr;
    bool m_ended = false;
};
