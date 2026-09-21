#include "ui.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <cstdio>

namespace ui {

bool guiBlocked()
{
    const auto env = QProcessEnvironment::systemEnvironment();
    return env.contains(QStringLiteral("DOLPHIN_CONTEXT_ACTIONS_HEADLESS"))
        || env.contains(QStringLiteral("PYTEST_CURRENT_TEST"));
}

int kdialog(const QStringList &args, QString *stdoutText)
{
    if (guiBlocked()) {
        fprintf(stderr, "kdialog skipped (headless): %s\n", qPrintable(args.join(QLatin1Char(' '))));
        if (stdoutText)
            *stdoutText = {};
        return 1;
    }
    QProcess proc;
    proc.start(QStringLiteral("kdialog"), args);
    if (!proc.waitForFinished(-1)) {
        fprintf(stderr, "kdialog unavailable\n");
        return 1;
    }
    if (stdoutText)
        *stdoutText = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return proc.exitCode();
}

void notify(const QString &title, const QString &msg, const QString &icon)
{
    if (guiBlocked())
        return;
    QProcess::startDetached(QStringLiteral("notify-send"),
                            {QStringLiteral("-i"), icon, QStringLiteral("-a"), QStringLiteral("Context Actions"), title, msg});
}

void errorDialog(const QString &title, const QString &msg)
{
    kdialog({QStringLiteral("--title"), title, QStringLiteral("--error"), msg});
}

void infoDialog(const QString &title, const QString &msg, int width, int height)
{
    QStringList args{QStringLiteral("--title"), title, QStringLiteral("--msgbox"), msg};
    if (width > 0 && height > 0)
        args << QStringLiteral("--geometry") << QStringLiteral("%1x%2").arg(width).arg(height);
    kdialog(args);
}

bool confirmDialog(const QString &title, const QString &msg)
{
    return kdialog({QStringLiteral("--title"), title, QStringLiteral("--yesno"), msg}) == 0;
}

QString menuDialog(const QString &title, const QString &prompt, const QList<QPair<QString, QString>> &items)
{
    QStringList args{QStringLiteral("--title"), title, QStringLiteral("--menu"), prompt};
    for (const auto &item : items) {
        if (item.second.isEmpty())
            continue;
        args << item.first << item.second;
    }
    QString out;
    if (kdialog(args, &out) != 0)
        return {};
    return out;
}

QString inputDialog(const QString &title, const QString &prompt, const QString &text)
{
    QString out;
    if (kdialog({QStringLiteral("--title"), title, QStringLiteral("--inputbox"), prompt, text}, &out) != 0)
        return {};
    return out;
}

std::pair<QString, QString> pbarOpen(const QString &title, const QString &label)
{
    QString out;
    kdialog({QStringLiteral("--title"), title, QStringLiteral("--progressbar"), label, QStringLiteral("100")}, &out);
    const auto parts = QStringView{out}.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() >= 2)
        return {parts[0].toString(), parts[1].toString()};
    if (parts.size() == 1)
        return {parts[0].toString(), QStringLiteral("/")};
    return {};
}

static QString qdbusBin()
{
    for (const char *name : {"qdbus-qt5", "qdbus", "qdbus6"}) {
        if (!QStandardPaths::findExecutable(QLatin1String(name)).isEmpty())
            return QLatin1String(name);
    }
    return {};
}

bool pbarSet(const std::pair<QString, QString> &handle, int value, const QString &label)
{
    if (handle.first.isEmpty())
        return true;
    const QString exe = qdbusBin();
    if (exe.isEmpty())
        return true;
    QProcess proc;
    proc.start(exe, {handle.first, handle.second, QStringLiteral("Set"), {}, QStringLiteral("value"), QString::number(value)});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return proc.error() == QProcess::Timedout;
    if (!label.isEmpty()) {
        QProcess lab;
        lab.start(exe, {handle.first, handle.second, QStringLiteral("setLabelText"), label});
        lab.waitForFinished(5000);
    }
    return true;
}

void pbarClose(const std::pair<QString, QString> &handle)
{
    if (handle.first.isEmpty())
        return;
    const QString exe = qdbusBin();
    if (exe.isEmpty())
        return;
    QProcess::execute(exe, {handle.first, handle.second, QStringLiteral("close")});
}

} // namespace ui
