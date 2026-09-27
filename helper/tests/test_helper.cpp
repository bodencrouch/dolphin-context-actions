#include "archive_ops.h"
#include "file_converter.h"
#include "ui.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class HelperTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void needsExtract_matchesExcludeList();
    void extractSubfolder_stripsExtension();
    void createArchiveName_singleFile();
    void quotedReduced_shortens();
    void csvJson_roundTrip();
    void unknownConversion_headless();
    void extractHere_continuesAfterBadArchive();
    void extractTo_createsSubfolderAndMerges();
    void extractTo_twoArchivesEachGetOutput();
    void hashTest_verifiesSidecarAndContinues();
    void archiveOps_workWithout7z();
};

void HelperTests::needsExtract_matchesExcludeList()
{
    QVERIFY(!needsExtract(QStringLiteral("notes.txt")));
    QVERIFY(!needsExtract(QStringLiteral("clip.mp4")));
    QVERIFY(!needsExtract(QStringLiteral("Photo.JPG")));
    QVERIFY(needsExtract(QStringLiteral("payload.zip")));
    QVERIFY(needsExtract(QStringLiteral("README")));
}

void HelperTests::extractSubfolder_stripsExtension()
{
    QCOMPARE(extractSubfolderName(QStringLiteral("photos.zip")), QStringLiteral("photos"));
    QCOMPARE(extractSubfolderName(QStringLiteral("photos.tar.gz")), QStringLiteral("photos.tar"));
    QCOMPARE(extractSubfolderName(QStringLiteral("noext")), QStringLiteral("noext~"));
    QCOMPARE(extractSubfolderName(QStringLiteral("archive.7z.001")), QStringLiteral("archive"));
}

void HelperTests::createArchiveName_singleFile()
{
    QTemporaryDir dir;
    const QString txt = dir.filePath(QStringLiteral("letter.txt"));
    QFile f(txt);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hi");
    f.close();
    QCOMPARE(createArchiveName({txt}), QStringLiteral("letter"));
}

void HelperTests::quotedReduced_shortens()
{
    QCOMPARE(quotedReduced(QStringLiteral("A & B")), QStringLiteral("\"A && B\""));
    const QString reduced = quotedReduced(QString(80, QLatin1Char('a')));
    QVERIFY(reduced.startsWith(QLatin1Char('"')));
    QVERIFY(reduced.contains(QLatin1String(" ... ")));
    QCOMPARE(reduced.size(), 2 + 32 + 5 + 32);
}

void HelperTests::csvJson_roundTrip()
{
    QTemporaryDir dir;
    const QString csv = dir.filePath(QStringLiteral("people.csv"));
    const QString json = dir.filePath(QStringLiteral("people.json"));
    QFile in(csv);
    QVERIFY(in.open(QIODevice::WriteOnly));
    in.write("name,age\nAda,36\nBob,40\n");
    in.close();
    QCOMPARE(convertCsvJson(csv, json), QString());
    QVERIFY(QFileInfo::exists(json));
    QFile::remove(csv);
    QCOMPARE(convertCsvJson(json, csv), QString());
    QFile out(csv);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(out.readAll()).contains(QLatin1String("Ada,36")));
}

void HelperTests::unknownConversion_headless()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
    QCOMPARE(runConvert(QStringLiteral("missing-conversion"), {}), 1);
}

static QString makeZip(const QString &dir, const QString &zipName, const QString &innerName, const QByteArray &payload)
{
    const QString inner = dir + QLatin1Char('/') + innerName;
    QFile f(inner);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(payload);
    f.close();
    QProcess proc;
    proc.setWorkingDirectory(dir);
    if (!QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty())
        proc.start(QStringLiteral("zip"), {QStringLiteral("-q"), zipName, innerName});
    else
        proc.start(QStringLiteral("7z"), {QStringLiteral("a"), QStringLiteral("-tzip"), zipName, innerName});
    if (!proc.waitForFinished(15000) || proc.exitCode() > 1)
        return {};
    QFile::remove(inner);
    return dir + QLatin1Char('/') + zipName;
}

static QString pathHiding7z(const QString &binDir)
{
    const QStringList tools{QStringLiteral("zip"), QStringLiteral("unzip"), QStringLiteral("tar"),
                            QStringLiteral("gzip"), QStringLiteral("gunzip"), QStringLiteral("bzip2"),
                            QStringLiteral("xz"), QStringLiteral("unrar"), QStringLiteral("unar"),
                            QStringLiteral("bsdtar")};
    for (const QString &name : tools) {
        const QString src = QStandardPaths::findExecutable(name);
        if (!src.isEmpty())
            QFile::link(src, binDir + QLatin1Char('/') + name);
    }
    return binDir;
}

void HelperTests::extractHere_continuesAfterBadArchive()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
        QSKIP("zip or 7z is required");

    QTemporaryDir dir;
    const QString good = makeZip(dir.path(), QStringLiteral("good.zip"), QStringLiteral("hello.txt"), "ok\n");
    QVERIFY(!good.isEmpty());
    const QString bad = dir.filePath(QStringLiteral("bad.zip"));
    QFile bf(bad);
    QVERIFY(bf.open(QIODevice::WriteOnly));
    bf.write("this is not a zip file");
    bf.close();

    QCOMPARE(runArchiveAction(QStringLiteral("extract-here"), {bad, good}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("hello.txt"))));

    QCOMPARE(runArchiveAction(QStringLiteral("test"), {bad, good}), 0);
}

void HelperTests::extractTo_createsSubfolderAndMerges()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
        QSKIP("zip or 7z is required");

    QTemporaryDir dir;
    const QString zip = makeZip(dir.path(), QStringLiteral("photos.zip"), QStringLiteral("a.txt"), "one\n");
    QVERIFY(!zip.isEmpty());
    QCOMPARE(runArchiveAction(QStringLiteral("extract-to"), {zip}), 0);
    const QString dest = dir.filePath(QStringLiteral("photos"));
    QVERIFY(QFileInfo(dest).isDir());
    QVERIFY(QFileInfo::exists(dest + QLatin1String("/a.txt")));

    const QString zip2 = makeZip(dir.path(), QStringLiteral("photos2.zip"), QStringLiteral("b.txt"), "two\n");
    QVERIFY(!zip2.isEmpty());
    QCOMPARE(runArchiveAction(QStringLiteral("extract-to"), {zip2}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("photos2/b.txt"))));

    QCOMPARE(runArchiveAction(QStringLiteral("extract-to"), {zip}), 0);
    QVERIFY(QFileInfo::exists(dest + QLatin1String("/a.txt")));
}

void HelperTests::extractTo_twoArchivesEachGetOutput()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
        QSKIP("zip or 7z is required");

    QTemporaryDir dir;
    const QString zipA = makeZip(dir.path(), QStringLiteral("one.zip"), QStringLiteral("a.txt"), "A\n");
    const QString zipB = makeZip(dir.path(), QStringLiteral("two.zip"), QStringLiteral("b.txt"), "B\n");
    QVERIFY(!zipA.isEmpty());
    QVERIFY(!zipB.isEmpty());
    QCOMPARE(runArchiveAction(QStringLiteral("extract-to"), {zipA, zipB}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("one/a.txt"))));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("two/b.txt"))));
}

void HelperTests::hashTest_verifiesSidecarAndContinues()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");

    QTemporaryDir dir;
    const QString good = dir.filePath(QStringLiteral("ok.txt"));
    const QString bad = dir.filePath(QStringLiteral("nope.txt"));
    QFile gf(good);
    QVERIFY(gf.open(QIODevice::WriteOnly));
    gf.write("ok\n");
    gf.close();
    QFile bf(bad);
    QVERIFY(bf.open(QIODevice::WriteOnly));
    bf.write("bad\n");
    bf.close();
    QCOMPARE(runArchiveAction(QStringLiteral("hash-generate-sha256"), {good}), 0);
    QFile sidecar(good + QLatin1String(".sha256"));
    QVERIFY(sidecar.exists());
    QFile::copy(good + QLatin1String(".sha256"), bad + QLatin1String(".sha256"));
    QCOMPARE(runArchiveAction(QStringLiteral("hash-test"), {good, bad}), 0);
}

void HelperTests::archiveOps_workWithout7z()
{
    qputenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty()
        || QStandardPaths::findExecutable(QStringLiteral("tar")).isEmpty())
        QSKIP("zip, unzip, and tar are required");

    const QByteArray oldPath = qgetenv("PATH");
    QTemporaryDir bin;
    QVERIFY(bin.isValid());
    qputenv("PATH", pathHiding7z(bin.path()).toUtf8());
    const auto restorePath = qScopeGuard([&] { qputenv("PATH", oldPath); });
    QVERIFY(QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty());
    QVERIFY(QStandardPaths::findExecutable(QStringLiteral("7za")).isEmpty());
    QVERIFY(!QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty());

    QTemporaryDir dir;
    const QString zip = makeZip(dir.path(), QStringLiteral("good.zip"), QStringLiteral("hello.txt"), "ok\n");
    QVERIFY(!zip.isEmpty());
    QCOMPARE(runArchiveAction(QStringLiteral("extract-here"), {zip}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("hello.txt"))));

    QCOMPARE(runArchiveAction(QStringLiteral("test"), {zip}), 0);

    const QString txt = dir.filePath(QStringLiteral("letter.txt"));
    QFile tf(txt);
    QVERIFY(tf.open(QIODevice::WriteOnly));
    tf.write("hi\n");
    tf.close();
    QCOMPARE(runArchiveAction(QStringLiteral("compress-to-zip"), {txt}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("letter.zip"))));

    QProcess tar;
    tar.setWorkingDirectory(dir.path());
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-czf"), QStringLiteral("bundle.tar.gz"), QStringLiteral("letter.txt")});
    QVERIFY(tar.waitForFinished(15000));
    QCOMPARE(tar.exitCode(), 0);
    QCOMPARE(runArchiveAction(QStringLiteral("extract-to"), {dir.filePath(QStringLiteral("bundle.tar.gz"))}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("bundle.tar/letter.txt"))));

    QCOMPARE(runArchiveAction(QStringLiteral("hash-generate-sha256"), {txt}), 0);
    QVERIFY(QFileInfo::exists(txt + QLatin1String(".sha256")));
    QCOMPARE(runArchiveAction(QStringLiteral("hash-sha256"), {txt}), 0);
    QCOMPARE(runArchiveAction(QStringLiteral("hash-crc32"), {txt}), 0);

    QCOMPARE(runArchiveAction(QStringLiteral("compress-to-7z"), {txt}), 0);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("letter.zip")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("letter.tar.gz")))
            || QFileInfo::exists(dir.filePath(QStringLiteral("letter_2.zip"))));
}

QTEST_MAIN(HelperTests)
#include "test_helper.moc"
