#include "file_converter_menus.h"
#include "file_converter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <cstdio>

int generateMenus(const QString &outputDir, const QString &converterBin, const QString &registry)
{
    const auto conversions = loadConversions(true, registry);
    QDir().mkpath(outputDir);
    const QDir dir(outputDir);
    const auto old = dir.entryList({QStringLiteral("dolphin-context-actions-convert-*.desktop")}, QDir::Files);
    for (const QString &name : old)
        QFile::remove(dir.filePath(name));

    QMap<QStringList, QVector<Conversion>> groups;
    for (const Conversion &conv : conversions) {
        QStringList key = conv.sourceExtensions;
        key.sort();
        groups[key].append(conv);
    }

    int written = 0;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QString slug;
        for (const QString &ext : it.key()) {
            if (!slug.isEmpty())
                slug += QLatin1Char('-');
            slug += QString(ext).remove(QLatin1Char('.'));
        }
        QSet<QString> mimes;
        for (const Conversion &conv : it.value()) {
            for (const QString &mime : conv.sourceMimetypes)
                mimes.insert(mime);
        }
        QStringList mimeList = QStringList(mimes.begin(), mimes.end());
        mimeList.sort();
        if (mimeList.isEmpty())
            mimeList << QStringLiteral("application/octet-stream");

        QVector<Conversion> featured;
        for (const Conversion &conv : it.value()) {
            if (conv.featured)
                featured.append(conv);
        }
        QStringList actionIds;
        for (const Conversion &conv : featured)
            actionIds << desktopActionId(conv.id);
        actionIds << QStringLiteral("moreConversions");

        QString text = QStringLiteral("[Desktop Entry]\nType=Service\nMimeType=%1;\nActions=%2;\n"
                                      "X-KDE-Submenu=Convert\nIcon=document-convert\n\n")
                           .arg(mimeList.join(QLatin1Char(';')), actionIds.join(QLatin1Char(';')));
        for (int i = 0; i < featured.size(); ++i) {
            text += QStringLiteral("[Desktop Action %1]\nName=%2\nIcon=%3\nExec=\"%4\" --file-convert %5 %F\n\n")
                        .arg(actionIds[i], featured[i].label, featured[i].icon, converterBin, featured[i].id);
        }
        text += QStringLiteral("[Desktop Action moreConversions]\nName=More…\nIcon=view-list-details\n"
                               "Exec=\"%1\" --pick-file-conversion %F\n\n")
                    .arg(converterBin);

        const QString path = dir.filePath(QStringLiteral("dolphin-context-actions-convert-%1.desktop").arg(slug));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            continue;
        file.write(text.toUtf8());
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                            | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                            | QFileDevice::ExeOther);
        printf("%s\n", qPrintable(path));
        ++written;
    }
    if (written == 0) {
        fprintf(stderr, "WARN: no service menus generated (no available conversions)\n");
        return 1;
    }
    return 0;
}
