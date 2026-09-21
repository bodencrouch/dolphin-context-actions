#include "converters.h"
#include "ui.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

static QString ffmpegBin()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

static QString uniqueOutput(QString dest)
{
    if (!QFileInfo::exists(dest))
        return dest;
    const QFileInfo info(dest);
    for (int n = 2;; ++n) {
        const QString candidate = info.path() + QLatin1Char('/') + info.completeBaseName()
            + QStringLiteral(" (%1).").arg(n) + info.suffix();
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

static void runFfmpeg(const QStringList &files, const QStringList &extra, const QString &ext,
                      const QString &title)
{
    const QString bin = ffmpegBin();
    if (bin.isEmpty()) {
        ui::errorDialog(title, QStringLiteral("<b>ffmpeg</b> not found."));
        return;
    }
    int done = 0;
    QStringList errors;
    auto handle = ui::pbarOpen(title, QStringLiteral("Starting…"));
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo info(files[i]);
        if (!info.exists()) {
            errors << QStringLiteral("File not found: %1").arg(files[i]);
            continue;
        }
        QString out = info.path() + QLatin1Char('/') + info.completeBaseName() + ext;
        if (out == info.absoluteFilePath())
            out = info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral("_converted") + ext;
        out = uniqueOutput(out);
        ui::pbarSet(handle, int(i * 100.0 / files.size()), QStringLiteral("Converting: %1").arg(info.fileName()));
        QStringList args{QStringLiteral("-y"), QStringLiteral("-i"), files[i]};
        args << extra << QStringLiteral("--") << out;
        QProcess proc;
        proc.start(bin, args);
        proc.waitForFinished(-1);
        if (proc.exitCode() != 0)
            errors << info.fileName();
        else
            ++done;
    }
    ui::pbarClose(handle);
    if (!errors.isEmpty())
        ui::errorDialog(title + QStringLiteral(" - Errors"), errors.join(QLatin1Char('\n')));
    else if (done > 0)
        ui::notify(title + QStringLiteral(" - Done"), QStringLiteral("✔ %1 file(s)").arg(done));
}

void convertAudio(const QStringList &files, const QString &format)
{
    QStringList extra{QStringLiteral("-c:a")};
    QString ext = QLatin1Char('.') + format;
    if (format == QLatin1String("mp3"))
        extra << QStringLiteral("libmp3lame") << QStringLiteral("-aq") << QStringLiteral("0");
    else if (format == QLatin1String("ogg"))
        extra << QStringLiteral("libvorbis") << QStringLiteral("-aq") << QStringLiteral("6");
    else if (format == QLatin1String("flac"))
        extra << QStringLiteral("flac");
    else if (format == QLatin1String("wav"))
        extra << QStringLiteral("pcm_s16le");
    else if (format == QLatin1String("m4a"))
        extra << QStringLiteral("aac") << QStringLiteral("-b:a") << QStringLiteral("192k");
    else if (format == QLatin1String("opus"))
        extra << QStringLiteral("libopus") << QStringLiteral("-b:a") << QStringLiteral("128k");
    else if (format == QLatin1String("alac")) {
        extra << QStringLiteral("alac");
        ext = QStringLiteral(".m4a");
    } else {
        ui::errorDialog(QStringLiteral("Audio Converter"), QStringLiteral("Unknown format: %1").arg(format));
        return;
    }
    runFfmpeg(files, extra, ext, QStringLiteral("Audio Converter"));
}

void videoToGif(const QStringList &files, const QString &preset)
{
    int fps = 15;
    int width = 800;
    if (preset == QLatin1String("small")) {
        fps = 10;
        width = 480;
    } else if (preset == QLatin1String("large")) {
        fps = 24;
        width = 1280;
    } else if (preset == QLatin1String("source"))
        width = -1;
    QString vf = QStringLiteral("fps=%1").arg(fps);
    if (width != -1)
        vf += QStringLiteral(",scale=%1:-1:flags=lanczos").arg(width);
    runFfmpeg(files,
              {QStringLiteral("-vf"), vf + QStringLiteral(",split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse")},
              QStringLiteral(".gif"), QStringLiteral("Video → GIF"));
}

void videoTo(const QStringList &files, const QString &ext)
{
    QStringList extra;
    if (ext == QLatin1String(".webm"))
        extra << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9") << QStringLiteral("-c:a")
              << QStringLiteral("libopus") << QStringLiteral("-crf") << QStringLiteral("30")
              << QStringLiteral("-b:v") << QStringLiteral("0");
    else
        extra << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-c:a")
              << QStringLiteral("aac") << QStringLiteral("-preset") << QStringLiteral("medium")
              << QStringLiteral("-crf") << QStringLiteral("23");
    runFfmpeg(files, extra, ext, QStringLiteral("Video"));
}

void extractAudio(const QStringList &files)
{
    runFfmpeg(files,
              {QStringLiteral("-vn"), QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), QStringLiteral("-aq"),
               QStringLiteral("2")},
              QStringLiteral(".mp3"), QStringLiteral("Extract Audio"));
}

QString audioPresetName(const QString &format)
{
    if (format == QLatin1String("mp3"))
        return QStringLiteral("MP3 (V0)");
    if (format == QLatin1String("ogg"))
        return QStringLiteral("OGG (Q6)");
    if (format == QLatin1String("flac"))
        return QStringLiteral("FLAC");
    if (format == QLatin1String("wav"))
        return QStringLiteral("WAV");
    if (format == QLatin1String("m4a"))
        return QStringLiteral("M4A (AAC, 192k)");
    if (format == QLatin1String("opus"))
        return QStringLiteral("Opus (128k)");
    if (format == QLatin1String("alac"))
        return QStringLiteral("ALAC (M4A)");
    return format;
}

QStringList audioFormats()
{
    return {QStringLiteral("alac"), QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("mp3"),
            QStringLiteral("ogg"),  QStringLiteral("opus"), QStringLiteral("wav")};
}

QString gifPresetName(const QString &preset)
{
    if (preset == QLatin1String("small"))
        return QStringLiteral("Small (10 fps, 480px)");
    if (preset == QLatin1String("large"))
        return QStringLiteral("Large (24 fps, 1280px)");
    if (preset == QLatin1String("source"))
        return QStringLiteral("Source size (15 fps)");
    return QStringLiteral("Medium (15 fps, 800px)");
}
