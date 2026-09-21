#include "file_converter.h"
#include "config.h"
#include "ui.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

static bool commandAvailable(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

static bool packageAvailable(const QString &name)
{
    if (name == QLatin1String("python3-PyMuPDF") || name == QLatin1String("pymupdf")
        || name == QLatin1String("PyMuPDF"))
        return commandAvailable(QStringLiteral("pdftotext"));
    if (name == QLatin1String("PyYAML") || name == QLatin1String("tomli-w") || name == QLatin1String("tomli_w"))
        return true;
    return false;
}

static bool conversionAvailable(const Conversion &conv)
{
    if (!conv.requiresCommands.isEmpty()) {
        bool any = false;
        for (const QString &cmd : conv.requiresCommands) {
            if (commandAvailable(cmd)) {
                any = true;
                break;
            }
        }
        if (!any)
            return false;
    }
    for (const QString &pkg : conv.requiresPackages) {
        if (!packageAvailable(pkg))
            return false;
    }
    return true;
}

static QJsonDocument loadRegistryDoc(const QString &overridePath)
{
    QStringList candidates;
    if (!overridePath.isEmpty()) {
        candidates << overridePath;
        QFileInfo info(overridePath);
        if (info.suffix() == QLatin1String("yaml") || info.suffix() == QLatin1String("yml"))
            candidates << info.path() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".json");
    }
    candidates << configDir() + QStringLiteral("/conversions.json");
    candidates << configDir() + QStringLiteral("/conversions.yaml");
    candidates << QStringLiteral("/usr/share/dolphin-context-actions/conversions.json");
    for (const QString &path : candidates) {
        if (path.endsWith(QLatin1String(".yaml")) || path.endsWith(QLatin1String(".yml")))
            continue;
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            return QJsonDocument::fromJson(file.readAll());
    }
    QFile bundled(QStringLiteral(":/conversions.json"));
    if (bundled.open(QIODevice::ReadOnly))
        return QJsonDocument::fromJson(bundled.readAll());
    return {};
}

static Conversion parseEntry(const QJsonObject &entry)
{
    Conversion conv;
    conv.id = entry.value(QLatin1String("id")).toString();
    conv.label = entry.value(QLatin1String("label")).toString();
    for (const auto &ext : entry.value(QLatin1String("source_extensions")).toArray()) {
        QString e = ext.toString().toLower();
        if (!e.startsWith(QLatin1Char('.')))
            e.prepend(QLatin1Char('.'));
        conv.sourceExtensions << e;
    }
    for (const auto &mime : entry.value(QLatin1String("source_mimetypes")).toArray())
        conv.sourceMimetypes << mime.toString();
    conv.targetExtension = entry.value(QLatin1String("target_extension")).toString();
    conv.featured = entry.value(QLatin1String("featured")).toBool();
    for (const auto &cmd : entry.value(QLatin1String("requires_commands")).toArray())
        conv.requiresCommands << cmd.toString();
    for (const auto &pkg : entry.value(QLatin1String("requires_packages")).toArray())
        conv.requiresPackages << pkg.toString();
    conv.engine = entry.value(QLatin1String("engine")).toString();
    conv.icon = entry.value(QLatin1String("icon")).toString(QStringLiteral("document-convert"));
    return conv;
}

QVector<Conversion> loadConversions(bool availableOnly, const QString &registryOverride)
{
    QVector<Conversion> out;
    const auto doc = loadRegistryDoc(registryOverride);
    for (const auto &value : doc.object().value(QLatin1String("conversions")).toArray()) {
        const Conversion conv = parseEntry(value.toObject());
        if (!availableOnly || conversionAvailable(conv))
            out << conv;
    }
    return out;
}

bool matchesConversion(const QString &path, const Conversion &conv)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    QString ext = info.suffix().toLower();
    if (!ext.isEmpty())
        ext.prepend(QLatin1Char('.'));
    return conv.sourceExtensions.contains(ext);
}

static QString outputPathFor(const QString &source, const Conversion &conv)
{
    QString ext = conv.targetExtension;
    if (!ext.startsWith(QLatin1Char('.')))
        ext.prepend(QLatin1Char('.'));
    QFileInfo info(source);
    return info.path() + QLatin1Char('/') + info.completeBaseName() + ext;
}

static QByteArray quoteJson(const QString &s)
{
    const QByteArray dumped = QJsonDocument(QJsonArray{QJsonValue(s)}).toJson(QJsonDocument::Compact);
    return dumped.mid(1, dumped.size() - 2);
}

static QStringList firstObjectKeysInOrder(const QByteArray &raw)
{
    const int start = raw.indexOf('{');
    if (start < 0)
        return {};
    QStringList keys;
    bool inString = false;
    bool escape = false;
    QByteArray current;
    int depth = 0;
    bool collectKey = true;
    for (int i = start; i < raw.size(); ++i) {
        const char c = raw.at(i);
        if (escape) {
            escape = false;
            if (inString && collectKey && depth == 1)
                current += c;
            continue;
        }
        if (c == '\\' && inString) {
            escape = true;
            continue;
        }
        if (c == '"') {
            if (inString) {
                inString = false;
                if (collectKey && depth == 1)
                    keys << QString::fromUtf8(current);
                current.clear();
            } else {
                inString = true;
            }
            continue;
        }
        if (inString) {
            if (collectKey && depth == 1)
                current += c;
            continue;
        }
        if (c == '{') {
            ++depth;
            collectKey = true;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0)
                break;
            continue;
        }
        if (c == ':' && depth == 1) {
            collectKey = false;
            continue;
        }
        if (c == ',' && depth == 1)
            collectKey = true;
    }
    return keys;
}

QString convertCsvJson(const QString &source, const QString &target)
{
    const QString srcExt = QFileInfo(source).suffix().toLower();
    const QString dstExt = QFileInfo(target).suffix().toLower();
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("cannot read source");

    if (srcExt == QLatin1String("csv") && dstExt == QLatin1String("json")) {
        const QString text = QString::fromUtf8(in.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.isEmpty())
            return QStringLiteral("empty csv");
        const QStringList headers = lines[0].split(QLatin1Char(','));
        QByteArray payload = "[\n";
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList cols = lines[i].split(QLatin1Char(','));
            payload += "    {\n";
            for (int c = 0; c < headers.size(); ++c) {
                payload += "        ";
                payload += quoteJson(headers[c]);
                payload += ": ";
                payload += quoteJson(c < cols.size() ? cols[c] : QString());
                if (c + 1 < headers.size())
                    payload += ',';
                payload += '\n';
            }
            payload += "    }";
            if (i + 1 < lines.size())
                payload += ',';
            payload += '\n';
        }
        payload += "]\n";
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("cannot write target");
        out.write(payload);
        return {};
    }

    if (srcExt == QLatin1String("json") && dstExt == QLatin1String("csv")) {
        const QByteArray raw = in.readAll();
        const auto doc = QJsonDocument::fromJson(raw);
        if (!doc.isArray())
            return QStringLiteral("JSON must be an array of objects to convert to CSV");
        const auto arr = doc.array();
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return QStringLiteral("cannot write target");
        if (arr.isEmpty())
            return {};
        if (!arr[0].isObject())
            return QStringLiteral("JSON must be an array of objects to convert to CSV");
        QStringList keys = firstObjectKeysInOrder(raw);
        if (keys.isEmpty())
            keys = arr[0].toObject().keys();
        out.write(keys.join(QLatin1Char(',')).toUtf8());
        out.write("\n");
        for (const auto &row : arr) {
            const auto obj = row.toObject();
            QStringList values;
            for (const QString &key : keys) {
                const auto v = obj.value(key);
                values << (v.isString() ? v.toString() : v.toVariant().toString());
            }
            out.write(values.join(QLatin1Char(',')).toUtf8());
            out.write("\n");
        }
        return {};
    }

    if (srcExt == QLatin1String("tsv") && dstExt == QLatin1String("csv")) {
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("cannot write target");
        while (!in.atEnd()) {
            QByteArray line = in.readLine();
            line.replace('\t', ',');
            out.write(line);
        }
        return {};
    }
    return QStringLiteral("Unsupported csv_json conversion");
}

static QString runEngine(const Conversion &conv, const QString &source, const QString &target)
{
    if (conv.engine == QLatin1String("csv_json"))
        return convertCsvJson(source, target);
    if (conv.engine == QLatin1String("yaml_json") || conv.engine == QLatin1String("toml_json")) {
        // Best-effort: if the file is already JSON-shaped for yaml/toml targets,
        // copy through Qt JSON. YAML/TOML text uses an external tool when present.
        if (QFileInfo(source).suffix().toLower() == QLatin1String("json")
            && conv.engine == QLatin1String("yaml_json")) {
            QFile in(source);
            if (!in.open(QIODevice::ReadOnly))
                return QStringLiteral("cannot read");
            const auto doc = QJsonDocument::fromJson(in.readAll());
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QStringLiteral("cannot write");
            // Minimal YAML: dump JSON pretty-print; valid enough for objects/arrays.
            out.write(doc.toJson());
            return {};
        }
        if (QFileInfo(source).suffix().toLower() == QLatin1String("json")
            && conv.engine == QLatin1String("toml_json")) {
            QFile in(source);
            if (!in.open(QIODevice::ReadOnly))
                return QStringLiteral("cannot read");
            const auto obj = QJsonDocument::fromJson(in.readAll()).object();
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                return QStringLiteral("cannot write");
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                const auto v = it.value();
                QString rendered = v.isString() ? QStringLiteral("\"%1\"").arg(v.toString())
                                                : v.toVariant().toString();
                out.write(QStringLiteral("%1 = %2\n").arg(it.key(), rendered).toUtf8());
            }
            return {};
        }
        if (conv.engine == QLatin1String("toml_json")) {
            QProcess proc;
            proc.start(QStringLiteral("python3"),
                       {QStringLiteral("-c"),
                        QStringLiteral("import json,sys,tomllib,pathlib; "
                                       "p=pathlib.Path(sys.argv[1]); "
                                       "pathlib.Path(sys.argv[2]).write_text(json.dumps(__import__('tomllib').loads(p.read_text()),indent=2)+'\\n')"),
                        source, target});
            if (proc.waitForFinished(-1) && proc.exitCode() == 0)
                return {};
            return QStringLiteral("TOML conversion failed");
        }
        if (conv.engine == QLatin1String("yaml_json")) {
            QFile in(source);
            if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
                return QStringLiteral("cannot read");
            // Very small YAML subset: key: value lines into a JSON object.
            QJsonObject obj;
            while (!in.atEnd()) {
                const QString line = QString::fromUtf8(in.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                const int colon = line.indexOf(QLatin1Char(':'));
                if (colon <= 0)
                    continue;
                obj.insert(line.left(colon).trimmed(), line.mid(colon + 1).trimmed());
            }
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QStringLiteral("cannot write");
            out.write(QJsonDocument(obj).toJson());
            return {};
        }
    }
    if (conv.engine == QLatin1String("pandoc")) {
        QProcess proc;
        proc.start(QStringLiteral("pandoc"), {source, QStringLiteral("-o"), target});
        proc.waitForFinished(-1);
        return proc.exitCode() == 0 ? QString() : QString::fromUtf8(proc.readAllStandardError());
    }
    if (conv.engine == QLatin1String("imagemagick")) {
        QString bin = QStandardPaths::findExecutable(QStringLiteral("magick"));
        if (bin.isEmpty())
            bin = QStandardPaths::findExecutable(QStringLiteral("convert"));
        if (bin.isEmpty())
            return QStringLiteral("ImageMagick is not installed (magick or convert)");
        QProcess proc;
        proc.start(bin, {source, target});
        proc.waitForFinished(-1);
        return proc.exitCode() == 0 ? QString() : QString::fromUtf8(proc.readAllStandardError());
    }
    if (conv.engine == QLatin1String("libreoffice_headless")) {
        QString bin = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
        if (bin.isEmpty())
            bin = QStandardPaths::findExecutable(QStringLiteral("soffice"));
        if (bin.isEmpty())
            return QStringLiteral("LibreOffice is not installed");
        QProcess proc;
        proc.start(bin, {QStringLiteral("--headless"), QStringLiteral("--convert-to"),
                         QFileInfo(target).suffix(), QStringLiteral("--outdir"), QFileInfo(source).absolutePath(),
                         source});
        proc.waitForFinished(-1);
        return proc.exitCode() == 0 ? QString() : QString::fromUtf8(proc.readAllStandardError());
    }
    if (conv.engine == QLatin1String("pymupdf_text")) {
        if (!commandAvailable(QStringLiteral("pdftotext")))
            return QStringLiteral("pdftotext is not installed (poppler-utils)");
        QProcess proc;
        proc.start(QStringLiteral("pdftotext"),
                   {QStringLiteral("-layout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), source, target});
        proc.waitForFinished(-1);
        return proc.exitCode() == 0 ? QString() : QString::fromUtf8(proc.readAllStandardError());
    }
    return QStringLiteral("Unknown engine: %1").arg(conv.engine);
}

int runConvert(const QString &conversionId, const QStringList &files, bool overwrite)
{
    const auto conversions = loadConversions(true);
    const auto it = std::find_if(conversions.begin(), conversions.end(),
                                 [&](const Conversion &c) { return c.id == conversionId; });
    if (it == conversions.end()) {
        ui::errorDialog(QStringLiteral("Dolphin Context Actions"),
                        QStringLiteral("Unknown or unavailable conversion: %1").arg(conversionId));
        return 1;
    }
    QStringList targets;
    for (const QString &file : files) {
        if (matchesConversion(file, *it))
            targets << file;
    }
    if (targets.isEmpty()) {
        ui::errorDialog(QStringLiteral("Dolphin Context Actions"),
                        QStringLiteral("No selected files support %1.").arg(it->label));
        return 1;
    }
    int converted = 0;
    QStringList failed;
    for (const QString &source : targets) {
        const QString target = outputPathFor(source, *it);
        if (QFileInfo::exists(target) && !overwrite) {
            failed << QStringLiteral("Output already exists: %1").arg(QFileInfo(target).fileName());
            continue;
        }
        const QString err = runEngine(*it, source, target);
        if (!err.isEmpty())
            failed << QFileInfo(source).fileName() + QStringLiteral(": ") + err;
        else
            ++converted;
    }
    if (converted == 1)
        ui::notify(QStringLiteral("Conversion complete"), QStringLiteral("Created file"), it->icon);
    else if (converted > 1)
        ui::notify(QStringLiteral("Conversions complete"), QStringLiteral("Created %1 files").arg(converted), it->icon);
    if (!failed.isEmpty() && converted == 0) {
        ui::errorDialog(QStringLiteral("Dolphin Context Actions"), failed.join(QLatin1Char('\n')));
        return 1;
    }
    if (!failed.isEmpty() && converted > 0) {
        ui::errorDialog(QStringLiteral("Dolphin Context Actions"), failed.join(QLatin1Char('\n')));
        return 1;
    }
    return failed.isEmpty() ? 0 : 1;
}

int runPick(const QStringList &files, bool overwrite)
{
    const auto conversions = loadConversions(true);
    QList<QPair<QString, QString>> items;
    for (const Conversion &conv : conversions) {
        for (const QString &file : files) {
            if (matchesConversion(file, conv)) {
                items.append({conv.id, conv.label});
                break;
            }
        }
    }
    if (items.isEmpty()) {
        ui::errorDialog(QStringLiteral("Dolphin Context Actions"),
                        QStringLiteral("No conversions are available for the selected file(s)."));
        return 1;
    }
    const QString chosen = ui::menuDialog(QStringLiteral("Dolphin Context Actions"),
                                          QStringLiteral("Select a conversion:"), items);
    if (chosen.isEmpty())
        return 1;
    return runConvert(chosen, files, overwrite);
}

QString desktopActionId(const QString &conversionId)
{
    QString cleaned = conversionId;
    cleaned.replace(QLatin1Char('_'), QLatin1Char('-'));
    cleaned.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9-]")));
    if (cleaned.isEmpty())
        return {};
    if (cleaned[0].isDigit())
        cleaned.prepend(QStringLiteral("convert"));
    return cleaned;
}
