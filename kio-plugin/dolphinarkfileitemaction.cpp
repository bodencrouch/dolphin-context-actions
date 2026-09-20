#include <KAbstractFileItemActionPlugin>
#include <KFileItemListProperties>
#include <KPluginFactory>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QWidget>

#include <algorithm>

namespace {

const QStringList kExtractExclude = QStringLiteral(
    "3gp aac ans ape asc asm asp aspx avi awk "
    "bas bat bmp "
    "c cs cls clw cmd cpp csproj css ctl cxx "
    "def dep dlg dsp dsw "
    "eps "
    "f f77 f90 f95 fla flac frm "
    "gif "
    "h hpp hta htm html hxx "
    "ico idl inc ini inl "
    "java jpeg jpg js "
    "la lnk log "
    "mak manifest wmv mov mp3 mp4 mpe mpeg mpg m4a "
    "ofr ogg "
    "pac pas pdf php php3 php4 php5 phptml pl pm png ps py pyo "
    "ra rb rc reg rka rm rtf "
    "sed sh shn shtml sln sql srt swa "
    "tcl tex tiff tta txt "
    "vb vcproj vbs "
    "mkv wav webm wma wv "
    "xml xsd xsl xslt").split(QLatin1Char(' '), Qt::SkipEmptyParts);

const QStringList kSplitArcExts = QStringList{
    QStringLiteral("7z"), QStringLiteral("bz2"), QStringLiteral("gz"),
    QStringLiteral("rar"), QStringLiteral("zip")};

const QStringList kNumberedArcExts = QStringList{
    QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("tar"),
    QStringLiteral("wim")};

QString extensionOf(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0 || dot == name.size() - 1) {
        return {};
    }
    return name.mid(dot + 1);
}

bool needsExtract(const QString &name)
{
    const QString ext = extensionOf(name);
    if (ext.isEmpty() || ext.size() > 32) {
        return true;
    }
    return !kExtractExclude.contains(ext, Qt::CaseInsensitive);
}

QString correctFsName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QChar(QChar::Null), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("Archive") : name;
}

QString extractSubfolderName(const QString &arcName)
{
    const int dot = arcName.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return correctFsName(arcName) + QLatin1Char('~');
    }

    const QString ext = arcName.mid(dot + 1);
    QString res = arcName.left(dot).trimmed();
    const int innerDot = res.lastIndexOf(QLatin1Char('.'));
    if (innerDot > 0) {
        const QString ext2 = res.mid(innerDot + 1);
        const QString part = ext2.toLower();
        if ((ext.compare(QLatin1String("001"), Qt::CaseInsensitive) == 0
             && kSplitArcExts.contains(ext2, Qt::CaseInsensitive))
            || (ext.compare(QLatin1String("rar"), Qt::CaseInsensitive) == 0
                && (part == QLatin1String("part001") || part == QLatin1String("part01")
                    || part == QLatin1String("part1")))) {
            res = res.left(innerDot).trimmed();
        }
    }
    return correctFsName(res);
}

QString reduceString(const QString &text)
{
    constexpr int maxSize = 64;
    if (text.size() <= maxSize) {
        return text;
    }
    const int half = maxSize / 2;
    return text.left(half) + QStringLiteral(" ... ") + text.right(half);
}

QString quotedReduced(const QString &name)
{
    QString inner = reduceString(name);
    inner.replace(QLatin1Char('&'), QStringLiteral("&&"));
    return QLatin1Char('"') + inner + QLatin1Char('"');
}

bool needsNumericSuffix(const QStringList &paths, const QString &name, const QStringList &exts)
{
    QStringList prefixes;
    prefixes.reserve(exts.size());
    for (const QString &ext : exts) {
        prefixes.append(name + QLatin1Char('.') + ext);
    }
    return std::any_of(paths.cbegin(), paths.cend(), [&](const QString &path) {
        const QString base = QFileInfo(path).fileName();
        return std::any_of(prefixes.cbegin(), prefixes.cend(), [&](const QString &prefix) {
            return base.compare(prefix, Qt::CaseInsensitive) == 0;
        });
    });
}

QString createArchiveName(const QStringList &paths, bool isHash)
{
    if (paths.isEmpty()) {
        return QStringLiteral("Archive");
    }

    QString name = QStringLiteral("Archive");
    const QFileInfo first(paths.constFirst());

    if (paths.size() == 1) {
        name = first.fileName();
        if (!first.isDir() && !isHash) {
            const int dot = name.indexOf(QLatin1Char('.'));
            if (dot > 0 && name.indexOf(QLatin1Char('.'), dot + 1) < 0) {
                name = name.left(dot);
            }
        }
    } else {
        const QString parent = first.absolutePath();
        const QString parentName = QFileInfo(parent).fileName();
        if (!parentName.isEmpty()) {
            name = parentName;
        }
    }

    name = correctFsName(name);
    const QStringList exts = isHash ? QStringList{QStringLiteral("sha256")} : kNumberedArcExts;
    if (needsNumericSuffix(paths, name, exts)) {
        name += QStringLiteral("_2");
    }
    return name;
}

bool selectionWantsExtract(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return false;
    }
    return std::all_of(paths.cbegin(), paths.cend(), [](const QString &path) {
        const QFileInfo info(path);
        return !info.isDir() && needsExtract(info.fileName());
    });
}

} // namespace

class DolphinArkFileItemAction final : public KAbstractFileItemActionPlugin
{
    Q_OBJECT

public:
    explicit DolphinArkFileItemAction(QObject *parent)
        : KAbstractFileItemActionPlugin(parent)
    {
    }

    QList<QAction *> actions(const KFileItemListProperties &items, QWidget *parentWidget) override
    {
        const QStringList paths = localPaths(items.urlList());
        if (paths.isEmpty()) {
            return {};
        }

        auto *root = new QAction(QIcon::fromTheme(QStringLiteral("ark")), tr("Archive"), parentWidget);
        auto *menu = new QMenu(parentWidget);
        root->setMenu(menu);

        const QFileInfo first(paths.constFirst());
        const bool singleFile = paths.size() == 1 && !first.isDir();
        const bool canExtract = selectionWantsExtract(paths);
        if (singleFile && needsExtract(first.fileName())) {
            addCommand(menu, tr("Open archive"), QStringLiteral("document-open"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("open")} + paths);
        }

        if (canExtract) {
            const QString specFolder = (paths.size() == 1)
                ? extractSubfolderName(first.fileName()) + QLatin1Char('/')
                : QStringLiteral("*/");

            addCommand(menu, tr("Extract files..."), QStringLiteral("archive-extract"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("extract")} + paths);
            addCommand(menu, tr("Extract Here"), QStringLiteral("archive-extract"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("extract-here")} + paths);
            addCommand(menu, tr("Extract to %1").arg(quotedReduced(specFolder)),
                       QStringLiteral("archive-extract"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("extract-to")} + paths);
            addCommand(menu, tr("Test archive"), QStringLiteral("dialog-ok"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("test")} + paths);
        }

        const QString arcName = createArchiveName(paths, false);
        const QString arc7z = arcName + QStringLiteral(".7z");
        const QString arcZip = arcName + QStringLiteral(".zip");

        addCommand(menu, tr("Add to archive..."), QStringLiteral("archive-insert"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("compress")} + paths);
        addCommand(menu, tr("Compress and email..."), QStringLiteral("mail-send"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("compress-email")} + paths);

        if (arc7z.compare(first.fileName(), Qt::CaseInsensitive) != 0) {
            addCommand(menu, tr("Add to %1").arg(quotedReduced(arc7z)),
                       QStringLiteral("archive-insert"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("compress-to-7z")} + paths);
        }
        addCommand(menu, tr("Compress to %1 and email").arg(quotedReduced(arc7z)),
                   QStringLiteral("mail-send"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("compress-to-7z-email")} + paths);
        if (arcZip.compare(first.fileName(), Qt::CaseInsensitive) != 0) {
            addCommand(menu, tr("Add to %1").arg(quotedReduced(arcZip)),
                       QStringLiteral("archive-insert"),
                       QStringList{QStringLiteral("--archive"), QStringLiteral("compress-to-zip")} + paths);
        }
        addCommand(menu, tr("Compress to %1 and email").arg(quotedReduced(arcZip)),
                   QStringLiteral("mail-send"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("compress-to-zip-email")} + paths);

        auto *crc = menu->addAction(QIcon::fromTheme(QStringLiteral("document-properties")),
                                    QStringLiteral("CRC SHA"));
        auto *crcMenu = new QMenu(parentWidget);
        crc->setMenu(crcMenu);

        struct HashItem {
            const char *label;
            const char *action;
        };
        static const HashItem kHashItems[] = {
            {"CRC-32", "hash-crc32"},
            {"CRC-64", "hash-crc64"},
            {"XXH64", "hash-xxh64"},
            {"MD5", "hash-md5"},
            {"SHA-1", "hash-sha1"},
            {"SHA-256", "hash-sha256"},
            {"SHA-384", "hash-sha384"},
            {"SHA-512", "hash-sha512"},
            {"SHA3-256", "hash-sha3-256"},
            {"BLAKE2sp", "hash-blake2sp"},
            {"*", "hash-all"},
        };
        for (const HashItem &item : kHashItems) {
            addCommand(crcMenu, QString::fromLatin1(item.label), QStringLiteral("document-properties"),
                       QStringList{QStringLiteral("--archive"), QString::fromLatin1(item.action)} + paths);
        }

        crcMenu->addSeparator();
        const QString sidecar = createArchiveName(paths, true) + QStringLiteral(".sha256");
        addCommand(crcMenu, QStringLiteral("SHA-256 -> %1").arg(sidecar),
                   QStringLiteral("document-save"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("hash-generate-sha256")} + paths);
        addCommand(crcMenu, tr("Test archive : Checksum"), QStringLiteral("dialog-ok"),
                   QStringList{QStringLiteral("--archive"), QStringLiteral("hash-test")} + paths);

        return {root};
    }

private:
    static QStringList localPaths(const QList<QUrl> &urls)
    {
        QStringList paths;
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                paths.append(QDir::cleanPath(url.toLocalFile()));
            } else if (url.scheme() == QLatin1String("admin")) {
                paths.append(QDir::cleanPath(url.path()));
            }
        }
        return paths;
    }

    // Keep in sync with the Cargo.toml [[bin]] entry point.
    static QString helperName()
    {
        return QStringLiteral("dolphin-context-actions");
    }

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

    void addCommand(QMenu *menu, const QString &text, const QString &icon, const QStringList &arguments)
    {
        QAction *action = menu->addAction(QIcon::fromTheme(icon), text);
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

    void fail(const QString &message)
    {
        Q_EMIT error(message);
        const QString executable = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (executable.isEmpty()) {
            return;
        }
        QProcess::startDetached(executable,
                                {QStringLiteral("-u"), QStringLiteral("critical"),
                                 QStringLiteral("-i"), QStringLiteral("dialog-error"),
                                 QStringLiteral("-a"), QStringLiteral("Archive"),
                                 QStringLiteral("Archive"), message});
    }
};

K_PLUGIN_CLASS_WITH_JSON(DolphinArkFileItemAction, "dolphinarkfileitemaction.json")

#include "dolphinarkfileitemaction.moc"
