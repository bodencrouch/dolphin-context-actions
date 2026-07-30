#include <KAbstractFileItemActionPlugin>
#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>
#include <KFileItemListProperties>
#include <KJob>
#include <KPluginFactory>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <memory>

class DolphinLinkFileItemAction final : public KAbstractFileItemActionPlugin
{
    Q_OBJECT

public:
    explicit DolphinLinkFileItemAction(QObject *parent)
        : KAbstractFileItemActionPlugin(parent)
    {
    }

    QList<QAction *> actions(const KFileItemListProperties &items, QWidget *parentWidget) override
    {
        const QStringList paths = localPaths(items.urlList());
        if (paths.isEmpty()) {
            return {};
        }

        const QStringList sources = pickedSources();
        if (sources.isEmpty()) {
            auto *pick = new QAction(QIcon::fromTheme(QStringLiteral("edit-link")), tr("Pick Link Source"), parentWidget);
            connect(pick, &QAction::triggered, this, [this, paths]() {
                savePickedSources(paths);
            });
            return {pick};
        }

        auto *drop = new QAction(QIcon::fromTheme(QStringLiteral("edit-link")), tr("Drop Link As"), parentWidget);
        auto *dropMenu = new QMenu(parentWidget);
        drop->setMenu(dropMenu);

        const QString targetDir = destinationDirectory(paths);
        const bool oneSource = sources.size() == 1;
        const bool oneSourceDirectory = oneSource && QFileInfo(sources.constFirst()).isDir();
        const bool onlyFiles = std::all_of(sources.cbegin(), sources.cend(), [](const QString &source) {
            return QFileInfo(source).isFile();
        });

        // Folders like / or /usr need root. The actions stay usable: the helper
        // asks polkit for the administrator password when it needs to.
        const bool writable = QFileInfo(targetDir).isWritable();
        if (!writable) {
            QAction *hint = dropMenu->addAction(QIcon::fromTheme(QStringLiteral("dialog-password")),
                                                tr("%1 needs administrator rights").arg(targetDir));
            hint->setEnabled(false);
            dropMenu->addSeparator();
        }

        // Drop Hardlink/Drop Symlink are the only operations offered elevated:
        // each creates exactly one link, which the KAuth helper can validate and
        // perform safely. Hardlink Clone/Symlink Clone/Smart Copy recurse over an
        // arbitrarily large, user-controlled tree -- not something a reviewable
        // privileged helper should do, so those stay unprivileged-only below and
        // simply fail with a permission error if the destination needs root.
        if (writable) {
            addCommand(dropMenu, tr("Drop Hardlink"), QStringLiteral("edit-link"),
                       {QStringLiteral("--drop-hardlink"), QStringLiteral("--target-dir"), targetDir}, onlyFiles);
            addCommand(dropMenu, tr("Drop Symlink"), QStringLiteral("edit-link"),
                       {QStringLiteral("--drop-symlink"), QStringLiteral("--target-dir"), targetDir});
        } else {
            addElevatedCommand(dropMenu, tr("Drop Hardlink"), QStringLiteral("edit-link"),
                                QStringLiteral("hardlink"), sources, targetDir, onlyFiles);
            addElevatedCommand(dropMenu, tr("Drop Symlink"), QStringLiteral("edit-link"),
                                QStringLiteral("symlink"), sources, targetDir, true);
        }
        dropMenu->addSeparator();
        addCommand(dropMenu, tr("Hardlink Clone"), QStringLiteral("folder"),
                   {QStringLiteral("--drop-as"), QStringLiteral("hardlink-clone"), QStringLiteral("--target-dir"), targetDir},
                   oneSourceDirectory);
        addCommand(dropMenu, tr("Symlink Clone"), QStringLiteral("folder"),
                   {QStringLiteral("--drop-as"), QStringLiteral("symlink-clone"), QStringLiteral("--target-dir"), targetDir},
                   oneSourceDirectory);
        addCommand(dropMenu, tr("Smart Copy"), QStringLiteral("edit-copy"),
                   {QStringLiteral("--drop-as"), QStringLiteral("smart-copy"), QStringLiteral("--target-dir"), targetDir},
                   oneSource);
        dropMenu->addSeparator();
        addCommand(dropMenu, tr("Enumerate Hardlinks"), QStringLiteral("view-list"),
                   QStringList{QStringLiteral("--enumerate-hardlinks")} + paths,
                   paths.size() == 1 && QFileInfo(paths.constFirst()).isFile());
        addCommand(dropMenu, tr("Link Properties"), QStringLiteral("dialog-information"),
                   QStringList{QStringLiteral("--link-properties")} + paths,
                   paths.size() == 1);

        auto *cancel = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Cancel Link Creation"), parentWidget);
        connect(cancel, &QAction::triggered, this, [this]() {
            QFile::remove(stateFile());
            notify(tr("Link Creation Cancelled"), tr("Picked sources cleared"));
        });

        return {drop, cancel};
    }

private:
    static QString stateFile()
    {
        return QDir::homePath() + QStringLiteral("/.cache/dolphin-link-extension/picked_sources.json");
    }

    static QStringList localPaths(const QList<QUrl> &urls)
    {
        QStringList paths;
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                paths.append(QDir::cleanPath(url.toLocalFile()));
            }
        }
        return paths;
    }

    static QStringList pickedSources()
    {
        QFile file(stateFile());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }

        const QJsonArray stored = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("sources")).toArray();
        QStringList sources;
        for (const QJsonValue &value : stored) {
            if (value.isString() && !value.toString().isEmpty()) {
                sources.append(value.toString());
            }
        }
        return sources;
    }

    void savePickedSources(const QStringList &paths)
    {
        QDir().mkpath(QFileInfo(stateFile()).absolutePath());

        QJsonArray sources;
        for (const QString &path : paths) {
            const QFileInfo item(path);
            if (item.exists() || item.isSymLink()) {
                sources.append(item.absoluteFilePath());
            }
        }
        if (sources.isEmpty()) {
            Q_EMIT error(tr("No local files or folders were selected."));
            return;
        }

        QSaveFile file(stateFile());
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QJsonDocument(QJsonObject{{QStringLiteral("sources"), sources}}).toJson()) < 0
            || !file.commit()) {
            Q_EMIT error(tr("Could not save the picked link sources."));
            return;
        }

        notify(tr("Link Source Picked"), tr("Picked %1 item(s)").arg(sources.size()));
    }

    // Rewrites the state file to exactly `remaining`, with none of
    // savePickedSources' pick-specific side effects (no "Link Source Picked"
    // notification, no existence filtering -- these are already-known-good
    // paths from a run in progress, not a fresh user selection).
    static void writeStateFile(const QStringList &remaining)
    {
        QJsonArray sources;
        for (const QString &path : remaining) {
            sources.append(path);
        }
        QSaveFile file(stateFile());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(QJsonObject{{QStringLiteral("sources"), sources}}).toJson());
            file.commit();
        }
    }

    static QString destinationDirectory(const QStringList &paths)
    {
        const QFileInfo first(paths.constFirst());
        return first.isDir() ? first.absoluteFilePath() : first.absolutePath();
    }

    // Keep in sync with the [project.scripts] entry point in pyproject.toml.
    static QString helperName()
    {
        return QStringLiteral("dolphin-context-actions");
    }

    // Dolphin inherits the PATH of the Plasma session, which often misses the
    // ~/.local/bin directory that "pip install --user" writes the helper to.
    static QString helperExecutable()
    {
        const QString onPath = QStandardPaths::findExecutable(helperName());
        if (!onPath.isEmpty()) {
            return onPath;
        }
        return QStandardPaths::findExecutable(helperName(),
                                              {QDir::homePath() + QStringLiteral("/.local/bin"),
                                               QStringLiteral("/usr/local/bin"),
                                               QStringLiteral("/usr/bin")});
    }

    // Same "name - Symlink (2).ext" / "name - Hardlink (2)" convention as the
    // unprivileged Python path (link_ops.py's _auto_rename_path/_auto_rename_dir),
    // so a collision looks the same to the user whether or not root was needed.
    static QString candidateLeafName(const QString &sourceName, const QString &kind, int attempt)
    {
        if (attempt == 0) {
            return sourceName;
        }
        const int dot = sourceName.lastIndexOf(QLatin1Char('.'));
        const bool hasExtension = dot > 0;
        const QString stem = hasExtension ? sourceName.left(dot) : sourceName;
        const QString suffix = hasExtension ? sourceName.mid(dot) : QString();
        if (attempt == 1) {
            return QStringLiteral("%1 - %2%3").arg(stem, kind, suffix);
        }
        return QStringLiteral("%1 - %2 (%3)%4").arg(stem, kind, QString::number(attempt), suffix);
    }

    void addElevatedCommand(QMenu *menu, const QString &text, const QString &icon, const QString &mode,
                            const QStringList &sources, const QString &targetDir, bool enabled)
    {
        QAction *action = menu->addAction(QIcon::fromTheme(icon), text);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, [this, mode, sources, targetDir]() {
            runElevated(mode, sources, targetDir, 0);
        });
    }

    // Processes one source at a time so a name collision on source N doesn't
    // abandon sources N+1..; each source gets its own bounded rename-retry loop.
    void runElevated(const QString &mode, const QStringList &sources, const QString &targetDir, int sourceIndex)
    {
        if (sourceIndex >= sources.size()) {
            // Only clear if the pick is still exactly what this run started
            // with. If the user re-picked a different source set while this
            // (polkit-prompt-gated, potentially long) run was in flight, that
            // new pick must survive -- not be silently wiped by this run
            // finishing.
            if (pickedSources() == sources) {
                QFile::remove(stateFile());
            }
            notify(tr("Link Created"), tr("Created %1 item(s) in %2 as administrator").arg(sources.size()).arg(targetDir));
            return;
        }

        const QString &source = sources.at(sourceIndex);
        const QString sourceName = QFileInfo(source).fileName();
        const QString kind = mode == QStringLiteral("hardlink") ? tr("Hardlink") : tr("Symlink");
        tryElevatedAttempt(mode, source, sourceName, kind, targetDir, sources, sourceIndex, 0);
    }

    void tryElevatedAttempt(const QString &mode, const QString &source, const QString &sourceName, const QString &kind,
                            const QString &targetDir, const QStringList &sources, int sourceIndex, int attempt)
    {
        // 50 collisions in one directory is already absurd; treat it as a stuck
        // loop rather than retrying forever.
        if (attempt > 50) {
            fail(tr("Too many name collisions in %1 for %2.").arg(targetDir, sourceName));
            return;
        }

        const QString leafName = candidateLeafName(sourceName, kind, attempt);
        const QString actionId = QStringLiteral("io.github.bodencrouch.linkhelper.create%1").arg(mode);

        KAuth::Action kauthAction(actionId);
        kauthAction.setHelperId(QStringLiteral("io.github.bodencrouch.linkhelper"));

        QVariantMap args;
        args[QStringLiteral("targetDir")] = targetDir;
        args[QStringLiteral("leafName")] = leafName;
        if (mode == QStringLiteral("symlink")) {
            // Relative to the resolved parent directory, leaf left unresolved, so
            // picking a symlink links to the symlink itself rather than its target.
            const QFileInfo sourceInfo(source);
            const QString resolvedParent = QFileInfo(sourceInfo.absolutePath()).canonicalFilePath();
            const QDir targetDirHandle(targetDir);
            const QString linkValue = targetDirHandle.relativeFilePath(resolvedParent + QLatin1Char('/') + sourceInfo.fileName());
            args[QStringLiteral("linkValue")] = linkValue;
        } else {
            args[QStringLiteral("source")] = QFileInfo(source).absoluteFilePath();
        }
        kauthAction.setArguments(args);

        KAuth::ExecuteJob *job = kauthAction.execute();
        connect(job, &KJob::result, this, [this, job, mode, source, sourceName, kind, targetDir, sources, sourceIndex, attempt]() {
            if (!job->error()) {
                runElevated(mode, sources, targetDir, sourceIndex + 1);
                return;
            }

            const QString errorText = job->errorString();
            if (errorText.contains(QStringLiteral("EEXIST"))) {
                tryElevatedAttempt(mode, source, sourceName, kind, targetDir, sources, sourceIndex, attempt + 1);
                return;
            }

            if (job->error() == KAuth::ActionReply::UserCancelledError) {
                return; // password dialog dismissed; the user already knows
            }

            // Narrow the pick to the failed source plus whatever wasn't
            // attempted yet -- sources before sourceIndex already succeeded,
            // and re-including them on retry would re-link (auto-renamed)
            // duplicates of work already done. Matches link_ops.py's Python
            // path, which only re-saves the sources that actually failed.
            if (pickedSources() == sources) {
                writeStateFile(sources.mid(sourceIndex));
            }

            fail(tr("Could not create %1 as administrator: %2").arg(kind.toLower(), errorText));
        });
    }

    void addCommand(QMenu *menu, const QString &text, const QString &icon, const QStringList &arguments, bool enabled = true)
    {
        QAction *action = menu->addAction(QIcon::fromTheme(icon), text);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, [this, arguments]() {
            const QString executable = helperExecutable();
            if (executable.isEmpty()) {
                fail(tr("Cannot find %1. Run ./install.sh again to reinstall it.").arg(helperName()));
                return;
            }
            if (!QProcess::startDetached(executable, arguments)) {
                fail(tr("Cannot start %1.").arg(executable));
            }
        });
    }

    // Dolphin swallows the error() signal in some context menus, so also raise a
    // desktop notification. Otherwise a failed action looks like nothing happened.
    void fail(const QString &message)
    {
        Q_EMIT error(message);
        send(QStringLiteral("critical"), QStringLiteral("dialog-error"), tr("Link Shell Extension"), message);
    }

    static void notify(const QString &title, const QString &message)
    {
        send(QStringLiteral("normal"), QStringLiteral("edit-link"), title, message);
    }

    static void send(const QString &urgency, const QString &icon, const QString &title, const QString &message)
    {
        const QString executable = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (executable.isEmpty()) {
            return;
        }
        QProcess::startDetached(executable,
                                {QStringLiteral("-u"), urgency, QStringLiteral("-i"), icon, QStringLiteral("-a"),
                                 QStringLiteral("Link Shell Extension"), title, message});
    }
};

K_PLUGIN_CLASS_WITH_JSON(DolphinLinkFileItemAction, "dolphinlinkfileitemaction.json")

#include "dolphinlinkfileitemaction.moc"
