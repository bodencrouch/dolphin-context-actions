#include <KAbstractFileItemActionPlugin>
#include <KFileItemListProperties>
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

        addCommand(dropMenu, tr("Drop Hardlink"), QStringLiteral("edit-link"),
                   {QStringLiteral("--drop-hardlink"), QStringLiteral("--target-dir"), targetDir}, onlyFiles);
        addCommand(dropMenu, tr("Drop Symlink"), QStringLiteral("edit-link"),
                   {QStringLiteral("--drop-symlink"), QStringLiteral("--target-dir"), targetDir});
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

    static QString destinationDirectory(const QStringList &paths)
    {
        const QFileInfo first(paths.constFirst());
        return first.isDir() ? first.absoluteFilePath() : first.absolutePath();
    }

    void addCommand(QMenu *menu, const QString &text, const QString &icon, const QStringList &arguments, bool enabled = true)
    {
        QAction *action = menu->addAction(QIcon::fromTheme(icon), text);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, [this, arguments]() {
            const QString executable = QStandardPaths::findExecutable(QStringLiteral("dolphin-context-actions"));
            if (executable.isEmpty() || !QProcess::startDetached(executable, arguments)) {
                Q_EMIT error(tr("Could not start dolphin-context-actions."));
            }
        });
    }

    static void notify(const QString &title, const QString &message)
    {
        const QString executable = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (!executable.isEmpty()) {
            QProcess::startDetached(executable,
                                    {QStringLiteral("-i"), QStringLiteral("edit-link"), QStringLiteral("-a"),
                                     QStringLiteral("Link Shell Extension"), title, message});
        }
    }
};

K_PLUGIN_CLASS_WITH_JSON(DolphinLinkFileItemAction, "dolphinlinkfileitemaction.json")

#include "dolphinlinkfileitemaction.moc"
