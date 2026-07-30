// Root-owned KAuth helper: creates exactly one symlink or hardlink per call.
//
// Runs as root, D-Bus-activated by polkit -- never a user-writable path
// pointed at by pkexec. Every input is re-validated here regardless of what
// the unprivileged caller already checked, because caller-side validation is
// not a security boundary once this process is root.
//
// Destination resolution walks the target directory component-by-component
// with O_NOFOLLOW at each step (the CERT POS35-C / FIO45-C pattern), so a
// symlink swapped into any part of the path -- not just the final directory --
// cannot redirect the write. The actual link creation uses symlinkat()/
// linkat(), which are atomic: there is no separate existence check to race,
// EEXIST from the syscall itself is authoritative.

#include <KAuth/ActionReply>
#include <KAuth/HelperSupport>

#include <QVariantMap>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace KAuth;

namespace
{

// Splits an absolute path into components, rejecting anything that isn't a
// plain path: relative paths, empty segments beyond the leading slash, and
// "." / ".." (a canonicalized path shouldn't contain these, but the caller's
// canonicalization is not trusted here).
bool splitAbsolutePath(const QString &path, QStringList &components)
{
    if (!path.startsWith(QLatin1Char('/'))) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            return false;
        }
    }
    components = parts;
    return true;
}

// A single path segment used as a filename: must not be a path itself.
bool isPlainLeafName(const QString &name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && name != QLatin1String(".") && name != QLatin1String("..");
}

// Opens targetDir following no symlinks anywhere in its path, by walking one
// component at a time from "/". Returns an O_PATH fd suitable as the dirfd
// argument to *at() calls, or -1 on failure (errno is set).
int openDirectoryNoFollow(const QString &targetDir)
{
    QStringList components;
    if (!splitAbsolutePath(targetDir, components)) {
        errno = EINVAL;
        return -1;
    }

    int fd = open("/", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }

    for (const QString &component : components) {
        const QByteArray name = component.toLocal8Bit();
        const int next = openat(fd, name.constData(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        if (next < 0) {
            return -1;
        }
        fd = next;
    }

    return fd;
}

ActionReply fail(const QString &message)
{
    ActionReply reply = ActionReply::HelperErrorReply();
    reply.setErrorDescription(message);
    return reply;
}

ActionReply failErrno(const QString &context, int savedErrno)
{
    return fail(QStringLiteral("%1: %2").arg(context, QString::fromLocal8Bit(strerror(savedErrno))));
}

} // namespace

class LinkHelper : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    ActionReply createsymlink(const QVariantMap &args)
    {
        const QString targetDir = args.value(QStringLiteral("targetDir")).toString();
        const QString leafName = args.value(QStringLiteral("leafName")).toString();
        const QString linkValue = args.value(QStringLiteral("linkValue")).toString();

        if (!isPlainLeafName(leafName)) {
            return fail(QStringLiteral("Invalid link name."));
        }
        if (linkValue.isEmpty()) {
            return fail(QStringLiteral("Invalid link target."));
        }

        const int dirfd = openDirectoryNoFollow(targetDir);
        if (dirfd < 0) {
            return failErrno(QStringLiteral("Cannot open destination directory"), errno);
        }

        const QByteArray leaf = leafName.toLocal8Bit();
        const QByteArray value = linkValue.toLocal8Bit();
        const int rc = symlinkat(value.constData(), dirfd, leaf.constData());
        const int savedErrno = errno;
        close(dirfd);

        if (rc != 0) {
            if (savedErrno == EEXIST) {
                return fail(QStringLiteral("EEXIST"));
            }
            return failErrno(QStringLiteral("Could not create symlink"), savedErrno);
        }

        ActionReply reply = ActionReply::SuccessReply();
        return reply;
    }

    ActionReply createhardlink(const QVariantMap &args)
    {
        const QString targetDir = args.value(QStringLiteral("targetDir")).toString();
        const QString leafName = args.value(QStringLiteral("leafName")).toString();
        const QString source = args.value(QStringLiteral("source")).toString();

        if (!isPlainLeafName(leafName)) {
            return fail(QStringLiteral("Invalid link name."));
        }
        if (!source.startsWith(QLatin1Char('/'))) {
            return fail(QStringLiteral("Source must be an absolute path."));
        }

        // Hardlinks only make sense for regular files. Checking here (not just
        // in the unprivileged caller) matters because this process is root:
        // lstat, not stat, so a symlink source is rejected rather than
        // silently followed to something the caller didn't intend to link.
        struct stat st;
        const QByteArray sourceBytes = source.toLocal8Bit();
        if (lstat(sourceBytes.constData(), &st) != 0) {
            return failErrno(QStringLiteral("Cannot inspect source"), errno);
        }
        if (!S_ISREG(st.st_mode)) {
            return fail(QStringLiteral("Only regular files can be hardlinked."));
        }

        const int dirfd = openDirectoryNoFollow(targetDir);
        if (dirfd < 0) {
            return failErrno(QStringLiteral("Cannot open destination directory"), errno);
        }

        const QByteArray leaf = leafName.toLocal8Bit();
        // AT_FDCWD + an absolute oldpath ignores the fd; no AT_SYMLINK_FOLLOW,
        // matching link(2)'s default of not following a symlink oldpath (moot
        // here since we already rejected non-regular sources above).
        const int rc = linkat(AT_FDCWD, sourceBytes.constData(), dirfd, leaf.constData(), 0);
        const int savedErrno = errno;
        close(dirfd);

        if (rc != 0) {
            if (savedErrno == EEXIST) {
                return fail(QStringLiteral("EEXIST"));
            }
            return failErrno(QStringLiteral("Could not create hardlink"), savedErrno);
        }

        ActionReply reply = ActionReply::SuccessReply();
        return reply;
    }
};

KAUTH_HELPER_MAIN("io.github.bodencrouch.linkhelper", LinkHelper)

#include "linkhelper.moc"
