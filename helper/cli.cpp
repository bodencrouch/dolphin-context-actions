#include "cli.h"
#include "archive_ops.h"
#include "config.h"
#include "converters.h"
#include "file_converter.h"
#include "file_converter_menus.h"
#include "link_ops.h"
#include "ui.h"
#include "uploaders.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <cstdio>

static bool ffmpegCheck()
{
    for (const char *tool : {"ffmpeg", "ffprobe"}) {
        if (QStandardPaths::findExecutable(QLatin1String(tool)).isEmpty()) {
            ui::errorDialog(QStringLiteral("Context Actions - Missing Dependency"),
                            QStringLiteral("<b>%1</b> not found.\n\nInstall ffmpeg.").arg(QLatin1String(tool)));
            return false;
        }
    }
    return true;
}

static void smartMenu(const QStringList &files)
{
    if (files.isEmpty()) {
        ui::errorDialog(QStringLiteral("Context Actions"), QStringLiteral("No files selected."));
        return;
    }
    if (!ffmpegCheck())
        return;
    QProcess proc;
    proc.start(QStringLiteral("file"), {QStringLiteral("--mime-type"), QStringLiteral("--brief"), files[0]});
    proc.waitForFinished(5000);
    const QString mime = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QString ext = QFileInfo(files[0]).suffix().toLower();
    const bool isGif = mime == QLatin1String("image/gif") || ext == QLatin1String("gif");
    const bool isVideo = mime.startsWith(QLatin1String("video/"))
        || QStringList{QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"), QStringLiteral("avi"),
                       QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("flv")}
               .contains(ext);
    const bool isAudio = mime.startsWith(QLatin1String("audio/"))
        || QStringList{QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("flac"), QStringLiteral("wav"),
                       QStringLiteral("m4a"), QStringLiteral("opus"), QStringLiteral("wma"), QStringLiteral("aac")}
               .contains(ext);
    const bool isImage = mime.startsWith(QLatin1String("image/")) && !isGif;

    QList<QPair<QString, QString>> choices;
    if (isImage)
        choices.append({QStringLiteral("upload_imgur"), QStringLiteral("Upload to Imgur")});
    else if (isGif || isVideo) {
        if (isVideo)
            choices.append({QStringLiteral("to_gif"), QStringLiteral("Convert to GIF…")});
        choices.append({QStringLiteral("to_mp4"), QStringLiteral("Convert to MP4")});
        choices.append({QStringLiteral("to_webm"), QStringLiteral("Convert to WebM")});
        choices.append({QStringLiteral("to_mkv"), QStringLiteral("Convert to MKV")});
        choices.append({QStringLiteral("extract_audio"), QStringLiteral("Extract Audio")});
        choices.append({QStringLiteral("upload_imgur"), QStringLiteral("Upload to Imgur")});
    } else if (isAudio) {
        for (const QString &fmt : audioFormats()) {
            if (fmt == ext)
                continue;
            choices.append({QStringLiteral("audio_") + fmt, QStringLiteral("Convert to %1").arg(audioPresetName(fmt))});
        }
    } else {
        ui::errorDialog(QStringLiteral("Context Actions"), QStringLiteral("Unsupported file type: %1").arg(mime.isEmpty() ? ext : mime));
        return;
    }
    const QString choice = ui::menuDialog(QStringLiteral("Context Actions"),
                                          QStringLiteral("Actions for: %1").arg(QFileInfo(files[0]).fileName()), choices);
    if (choice == QLatin1String("to_gif")) {
        const QString preset = ui::menuDialog(QStringLiteral("Convert to GIF"), QStringLiteral("Select quality preset:"),
                                              {{QStringLiteral("small"), gifPresetName(QStringLiteral("small"))},
                                               {QStringLiteral("medium"), gifPresetName(QStringLiteral("medium"))},
                                               {QStringLiteral("large"), gifPresetName(QStringLiteral("large"))},
                                               {QStringLiteral("source"), gifPresetName(QStringLiteral("source"))}});
        if (!preset.isEmpty())
            videoToGif(files, preset);
    } else if (choice == QLatin1String("to_mp4"))
        videoTo(files, QStringLiteral(".mp4"));
    else if (choice == QLatin1String("to_webm"))
        videoTo(files, QStringLiteral(".webm"));
    else if (choice == QLatin1String("to_mkv"))
        videoTo(files, QStringLiteral(".mkv"));
    else if (choice == QLatin1String("extract_audio"))
        extractAudio(files);
    else if (choice == QLatin1String("upload_imgur"))
        imgurUpload(files);
    else if (choice.startsWith(QLatin1String("audio_")))
        convertAudio(files, choice.mid(6));
}

int runCli(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dolphin-context-actions"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Dolphin Context Actions"));
    parser.addHelpOption();
    const QCommandLineOption smartMenuOpt(QStringLiteral("smart-menu"), QStringLiteral("Show smart context menu"));
    const QCommandLineOption batchOpt(QStringLiteral("batch"), QStringLiteral("Batch convert mode"), QStringLiteral("MODE"));
    const QCommandLineOption formatOpt(QStringLiteral("format"), QStringLiteral("Target format"), QStringLiteral("FMT"));
    const QCommandLineOption configureOpt(QStringLiteral("configure"), QStringLiteral("Open configuration"));
    const QCommandLineOption fileConvertOpt(QStringLiteral("file-convert"), QStringLiteral("Run a conversion"), QStringLiteral("ID"));
    const QCommandLineOption pickConvOpt(QStringLiteral("pick-file-conversion"), QStringLiteral("Choose a conversion"));
    const QCommandLineOption listConvOpt(QStringLiteral("list-file-conversions"), QStringLiteral("List conversions"));
    const QCommandLineOption pickLinkOpt(QStringLiteral("pick-link-source"), QStringLiteral("Pick link source"));
    const QCommandLineOption cancelLinkOpt(QStringLiteral("cancel-link"), QStringLiteral("Cancel link creation"));
    const QCommandLineOption dropAsOpt(QStringLiteral("drop-as"), QStringLiteral("Drop as type"), QStringLiteral("TYPE"));
    const QCommandLineOption dropHardOpt(QStringLiteral("drop-hardlink"), QStringLiteral("Drop hardlink"));
    const QCommandLineOption dropSymOpt(QStringLiteral("drop-symlink"), QStringLiteral("Drop symlink"));
    const QCommandLineOption hardCloneOpt(QStringLiteral("hardlink-clone"), QStringLiteral("Hardlink clone"));
    const QCommandLineOption symCloneOpt(QStringLiteral("symlink-clone"), QStringLiteral("Symlink clone"));
    const QCommandLineOption smartCopyOpt(QStringLiteral("smart-copy"), QStringLiteral("Smart copy"));
    const QCommandLineOption linkPropsOpt(QStringLiteral("link-properties"), QStringLiteral("Link properties"));
    const QCommandLineOption enumOpt(QStringLiteral("enumerate-hardlinks"), QStringLiteral("Enumerate hardlinks"));
    const QCommandLineOption targetDirOpt(QStringLiteral("target-dir"), QStringLiteral("Target directory"), QStringLiteral("DIR"));
    const QCommandLineOption archiveOpt(QStringLiteral("archive"), QStringLiteral("Archive action"), QStringLiteral("ACTION"));
    const QCommandLineOption genMenusOpt(QStringLiteral("generate-menus"), QStringLiteral("Generate Convert menus"));
    const QCommandLineOption outputDirOpt(QStringLiteral("output-dir"), QStringLiteral("Menu output directory"), QStringLiteral("DIR"));
    const QCommandLineOption converterBinOpt(QStringLiteral("converter-bin"), QStringLiteral("Helper path"), QStringLiteral("BIN"));
    const QCommandLineOption registryOpt(QStringLiteral("registry"), QStringLiteral("Registry path"), QStringLiteral("PATH"));
    parser.addOptions({smartMenuOpt, batchOpt, formatOpt, configureOpt, fileConvertOpt, pickConvOpt, listConvOpt,
                       pickLinkOpt, cancelLinkOpt, dropAsOpt, dropHardOpt, dropSymOpt, hardCloneOpt, symCloneOpt,
                       smartCopyOpt, linkPropsOpt, enumOpt, targetDirOpt, archiveOpt, genMenusOpt, outputDirOpt,
                       converterBinOpt, registryOpt});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Selected files"), QStringLiteral("[files...]"));
    parser.process(app);
    const QStringList files = parser.positionalArguments();

    if (parser.isSet(genMenusOpt)) {
        const QString out = parser.value(outputDirOpt).isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kio/servicemenus")
            : parser.value(outputDirOpt);
        const QString bin = parser.value(converterBinOpt).isEmpty()
            ? QDir::homePath() + QStringLiteral("/.local/bin/dolphin-context-actions")
            : parser.value(converterBinOpt);
        return generateMenus(out, bin, parser.value(registryOpt));
    }
    if (parser.isSet(listConvOpt)) {
        for (const Conversion &conv : loadConversions(false))
            printf("%s\t%s\n", qPrintable(conv.id), qPrintable(conv.label));
        return 0;
    }
    if (parser.isSet(fileConvertOpt))
        return runConvert(parser.value(fileConvertOpt), files);
    if (parser.isSet(pickConvOpt))
        return runPick(files);
    if (parser.isSet(configureOpt)) {
        AppConfig cfg = loadConfig();
        const QString sel = ui::menuDialog(QStringLiteral("Context Actions - Configure"),
                                           QStringLiteral("Choose a setting to change:"),
                                           {{QStringLiteral("audio"), QStringLiteral("Default audio format")},
                                            {QStringLiteral("video"), QStringLiteral("Default GIF preset")},
                                            {QStringLiteral("imgur"), QStringLiteral("Imgur Client ID")}});
        if (sel == QLatin1String("audio")) {
            QList<QPair<QString, QString>> items;
            for (const QString &fmt : audioFormats())
                items.append({fmt, audioPresetName(fmt)});
            const QString chosen = ui::menuDialog(QStringLiteral("Audio Format"), QStringLiteral("Default audio format:"), items);
            if (!chosen.isEmpty()) {
                cfg.audioPreset = chosen;
                saveConfig(cfg);
            }
        } else if (sel == QLatin1String("video")) {
            const QString chosen = ui::menuDialog(QStringLiteral("GIF Preset"), QStringLiteral("Default GIF preset:"),
                                                  {{QStringLiteral("small"), gifPresetName(QStringLiteral("small"))},
                                                   {QStringLiteral("medium"), gifPresetName(QStringLiteral("medium"))},
                                                   {QStringLiteral("large"), gifPresetName(QStringLiteral("large"))},
                                                   {QStringLiteral("source"), gifPresetName(QStringLiteral("source"))}});
            if (!chosen.isEmpty()) {
                cfg.videoPreset = chosen;
                saveConfig(cfg);
            }
        } else if (sel == QLatin1String("imgur")) {
            const QString val = ui::inputDialog(QStringLiteral("Imgur Client ID"), QStringLiteral("Client ID:"), cfg.imgurClientId);
            if (!val.isNull()) {
                cfg.imgurClientId = val;
                saveConfig(cfg);
            }
        }
        return 0;
    }
    if (parser.isSet(batchOpt)) {
        if (!ffmpegCheck())
            return 1;
        const QString mode = parser.value(batchOpt);
        const AppConfig cfg = loadConfig();
        if (mode == QLatin1String("audio"))
            convertAudio(files, parser.value(formatOpt).isEmpty() ? cfg.audioPreset : parser.value(formatOpt));
        else if (mode == QLatin1String("video-to-gif"))
            videoToGif(files, parser.value(formatOpt).isEmpty() ? cfg.videoPreset : parser.value(formatOpt));
        else if (mode == QLatin1String("video-to-mp4"))
            videoTo(files, QStringLiteral(".mp4"));
        else if (mode == QLatin1String("video-to-webm"))
            videoTo(files, QStringLiteral(".webm"));
        else if (mode == QLatin1String("extract-audio"))
            extractAudio(files);
        else if (mode == QLatin1String("imgur-upload"))
            imgurUpload(files);
        return 0;
    }
    if (parser.isSet(pickLinkOpt)) {
        pickLinkSource(files);
        return 0;
    }
    if (parser.isSet(cancelLinkOpt)) {
        cancelLinkCreation();
        return 0;
    }
    if (parser.isSet(dropAsOpt) && parser.isSet(targetDirOpt)) {
        dropAs(parser.value(targetDirOpt), parser.value(dropAsOpt));
        return 0;
    }
    if (parser.isSet(dropHardOpt) && parser.isSet(targetDirOpt)) {
        dropHardlink(parser.value(targetDirOpt));
        return 0;
    }
    if (parser.isSet(dropSymOpt) && parser.isSet(targetDirOpt)) {
        dropSymlink(parser.value(targetDirOpt));
        return 0;
    }
    if (parser.isSet(hardCloneOpt) && !files.isEmpty()) {
        const QString dest = ui::inputDialog(QStringLiteral("Hardlink Clone"), QStringLiteral("Destination folder:"),
                                             QFileInfo(files[0]).absolutePath());
        if (!dest.isEmpty())
            hardlinkClone(files[0], dest + QLatin1Char('/') + QFileInfo(files[0]).fileName());
        return 0;
    }
    if (parser.isSet(symCloneOpt) && !files.isEmpty()) {
        const QString dest = ui::inputDialog(QStringLiteral("Symlink Clone"), QStringLiteral("Destination folder:"),
                                             QFileInfo(files[0]).absolutePath());
        if (!dest.isEmpty())
            symlinkClone(files[0], dest + QLatin1Char('/') + QFileInfo(files[0]).fileName());
        return 0;
    }
    if (parser.isSet(smartCopyOpt) && !files.isEmpty()) {
        const QString dest = ui::inputDialog(QStringLiteral("Smart Copy"), QStringLiteral("Destination folder:"),
                                             QFileInfo(files[0]).absolutePath());
        if (!dest.isEmpty())
            smartCopy(files[0], dest);
        return 0;
    }
    if (parser.isSet(linkPropsOpt) && !files.isEmpty()) {
        showHardlinkProperties(files[0]);
        return 0;
    }
    if (parser.isSet(enumOpt) && !files.isEmpty()) {
        const QStringList siblings = enumerateHardlinks(files[0]);
        ui::infoDialog(QStringLiteral("Hardlink Siblings"),
                       siblings.isEmpty() ? QStringLiteral("No other hardlinks found for this file.")
                                          : siblings.join(QLatin1Char('\n')),
                       600, 400);
        return 0;
    }
    if (parser.isSet(archiveOpt))
        return runArchiveAction(parser.value(archiveOpt), files);
    smartMenu(files);
    return 0;
}
