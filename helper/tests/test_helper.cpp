#include "archive_ops.h"
#include "file_converter.h"
#include "ui.h"

#include <QDir>
#include <QFile>
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

QTEST_MAIN(HelperTests)
#include "test_helper.moc"
