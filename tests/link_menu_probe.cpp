#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QMenu>
#include <QMimeDatabase>
#include <QTextStream>
#include <QUrl>

#include <sys/stat.h>

static void printActions(const QList<QAction *> &actions, int depth = 0)
{
    QTextStream output(stdout);
    for (QAction *action : actions) {
        if (action->isSeparator()) {
            continue;
        }
        output << QString(depth * 2, QLatin1Char(' ')) << action->text() << '\n';
        if (action->menu()) {
            printActions(action->menu()->actions(), depth + 1);
        }
    }
}

static QAction *findAction(const QList<QAction *> &actions, const QString &text)
{
    for (QAction *action : actions) {
        if (action->text() == text) {
            return action;
        }
        if (action->menu()) {
            if (QAction *match = findAction(action->menu()->actions(), text)) {
                return match;
            }
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStringList args;
    for (int i = 1; i < argc; ++i) {
        args << QString::fromLocal8Bit(argv[i]);
    }

    // --admin builds the item's URL under the admin:// scheme instead of
    // file://, mirroring what Dolphin's "Open as Administrator" (kio-admin)
    // hands the plugin -- lets this probe exercise that path without a real
    // polkit prompt.
    const bool asAdmin = args.removeAll(QStringLiteral("--admin")) > 0;
    if (args.size() < 1 || args.size() > 2) {
        return 2;
    }

    const QFileInfo file(args.at(0));
    const QString mimeType = QMimeDatabase().mimeTypeForFile(file).name();
    const mode_t mode = file.isDir() ? S_IFDIR : S_IFREG;
    QUrl url = QUrl::fromLocalFile(file.absoluteFilePath());
    if (asAdmin) {
        url.setScheme(QStringLiteral("admin"));
    }
    KFileItemList selected{KFileItem(url, mimeType, mode)};

    KFileItemActions fileItemActions;
    fileItemActions.setItemListProperties(KFileItemListProperties(selected));

    QMenu menu;
    fileItemActions.addActionsTo(&menu, KFileItemActions::MenuActionSource::Plugins);
    printActions(menu.actions());

    if (args.size() == 2) {
        QAction *action = findAction(menu.actions(), args.at(1));
        if (!action) {
            return 3;
        }
        action->trigger();
    }
    return 0;
}
