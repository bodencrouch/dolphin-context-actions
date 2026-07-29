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
    if (argc < 2 || argc > 3) {
        return 2;
    }

    const QFileInfo file(QString::fromLocal8Bit(argv[1]));
    const QString mimeType = QMimeDatabase().mimeTypeForFile(file).name();
    const mode_t mode = file.isDir() ? S_IFDIR : S_IFREG;
    KFileItemList selected{KFileItem(QUrl::fromLocalFile(file.absoluteFilePath()), mimeType, mode)};

    KFileItemActions fileItemActions;
    fileItemActions.setItemListProperties(KFileItemListProperties(selected));

    QMenu menu;
    fileItemActions.addActionsTo(&menu, KFileItemActions::MenuActionSource::Plugins);
    printActions(menu.actions());

    if (argc == 3) {
        QAction *action = findAction(menu.actions(), QString::fromLocal8Bit(argv[2]));
        if (!action) {
            return 3;
        }
        action->trigger();
    }
    return 0;
}
