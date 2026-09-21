#pragma once

#include <QString>
#include <QStringList>
#include <utility>

namespace ui {

bool guiBlocked();
int kdialog(const QStringList &args, QString *stdoutText = nullptr);
void notify(const QString &title, const QString &msg, const QString &icon = QStringLiteral("document-convert"));
void errorDialog(const QString &title, const QString &msg);
void infoDialog(const QString &title, const QString &msg, int width = 0, int height = 0);
bool confirmDialog(const QString &title, const QString &msg);
QString menuDialog(const QString &title, const QString &prompt, const QList<QPair<QString, QString>> &items);
QString inputDialog(const QString &title, const QString &prompt, const QString &text = {});
std::pair<QString, QString> pbarOpen(const QString &title, const QString &label);
bool pbarSet(const std::pair<QString, QString> &handle, int value, const QString &label = {});
void pbarClose(const std::pair<QString, QString> &handle);

} // namespace ui
