#include "OpenWithController.h"

#include "app/Logging.h"
#include "ViewerController.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>

#include <cstddef>
#include <memory>
#include <utility>

namespace
{
const char* const kFieldName = "name";
const char* const kFieldExtensions = "extensions";
const char* const kFieldCommandLine = "commandLine";

// Outside quickAccess/accounts/... on purpose: this list is a property of the
// machine, not of the signed-in account. Persisted user data, so the shape is
// fixed from here on (CLAUDE.md, "Pre-1.0").
const char* const kSettingsKey = "openWith/programs";

// What the command line puts the URL in place of. %N (the file name) is
// deliberately absent: a name on MEGA can be chosen by whoever shared the folder,
// so it would be an injection surface (STUDY_OPEN_WITH.md 3-3-2).
const QString kUrlPlaceholder = QStringLiteral("%U");

// Where the first token of command ends, or -1 when it has none. Mirrors
// QProcess::splitCommand() -- QChar::isSpace() separators, and three quotes in a
// row as a literal quote -- so the program swapped out is the one launch() runs.
qsizetype firstTokenEnd(const QString& command)
{
    int quoteCount = 0;
    bool inQuote = false;
    bool tokenStarted = false;
    for (qsizetype i = 0; i < command.size(); ++i)
    {
        const QChar c = command.at(i);
        if (c == u'"')
        {
            if (++quoteCount == 3)
            {
                quoteCount = 0;
                tokenStarted = true;
            }
            continue;
        }
        if (quoteCount == 1)
            inQuote = !inQuote;
        quoteCount = 0;
        if (!inQuote && c.isSpace())
        {
            if (tokenStarted)
                return i;
        }
        else
        {
            tokenStarted = true;
        }
    }
    return tokenStarted ? command.size() : -1;
}

// unique_ptr because QSettings is neither copyable nor movable.
std::unique_ptr<QSettings> openSettings(const QString& iniFilePath)
{
    if (iniFilePath.isEmpty())
        return std::make_unique<QSettings>();
    return std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat);
}
} // namespace

OpenWithController::OpenWithController(ViewerController* viewer,
                                       QString iniFilePath,
                                       QObject* parent)
    : QObject(parent), mViewer(viewer), mIniFilePath(std::move(iniFilePath))
{
    load();
}

int OpenWithController::count() const
{
    return static_cast<int>(mEntries.size());
}

QVariantList OpenWithController::entries() const
{
    QVariantList result;
    for (const OpenWithEntry& entry : mEntries)
    {
        QVariantMap map;
        map.insert(QString::fromLatin1(kFieldName), QString::fromStdString(entry.name));
        map.insert(QString::fromLatin1(kFieldExtensions), QString::fromStdString(entry.extensions));
        map.insert(QString::fromLatin1(kFieldCommandLine),
                   QString::fromStdString(entry.commandLine));
        result.append(map);
    }
    return result;
}

void OpenWithController::setEntries(const QVariantList& entries)
{
    std::vector<OpenWithEntry> parsed;
    parsed.reserve(static_cast<std::size_t>(entries.size()));
    for (const QVariant& value : entries)
    {
        const QVariantMap map = value.toMap();
        OpenWithEntry entry;
        entry.name = map.value(QString::fromLatin1(kFieldName)).toString().toStdString();
        entry.extensions =
            map.value(QString::fromLatin1(kFieldExtensions)).toString().toStdString();
        entry.commandLine =
            map.value(QString::fromLatin1(kFieldCommandLine)).toString().toStdString();
        // An entry with no command is unstartable, and one with no name would be a
        // blank menu row -- the settings screen's half-typed new row is exactly that,
        // so it is dropped here rather than saved and shown.
        if (entry.name.empty() || entry.commandLine.empty())
            continue;
        parsed.push_back(std::move(entry));
    }

    if (parsed == mEntries)
        return;
    mEntries = std::move(parsed);
    save();
    emit entriesChanged();
}

QString OpenWithController::nameAt(int index) const
{
    if (index < 0 || index >= count())
        return {};
    return QString::fromStdString(mEntries[static_cast<std::size_t>(index)].name);
}

bool OpenWithController::matchesAt(int index, const QString& fileName) const
{
    if (index < 0 || index >= count())
        return false;
    return openWithExtensionMatches(mEntries[static_cast<std::size_t>(index)].extensions,
                                    fileName.toStdString());
}

void OpenWithController::launch(int index, quint64 handle)
{
    const QString name = nameAt(index);
    if (index < 0 || index >= count())
    {
        qCWarning(lcApp) << "no Open with program at index" << index;
        emit programLaunched(false, name);
        return;
    }

    const QString commandLine =
        QString::fromStdString(mEntries[static_cast<std::size_t>(index)].commandLine);
    // splitCommand() honours double quotes and does not treat a backslash as an
    // escape, so a Windows path can be pasted in as written and no parser of our
    // own is needed (STUDY_OPEN_WITH.md 3-3-2).
    QStringList arguments = QProcess::splitCommand(commandLine);
    if (arguments.isEmpty())
    {
        qCWarning(lcApp) << "Open with program" << name << "has no runnable command";
        emit programLaunched(false, name);
        return;
    }
    const QString program = arguments.takeFirst();

    if (mViewer == nullptr)
    {
        emit programLaunched(false, name);
        return;
    }
    const QString url = mViewer->sourceUrl(handle);
    if (url.isEmpty())
    {
        // Neither this warning nor the ones above carry the URL or the arguments
        // built from it: it is a capability, and a log line is the one place it
        // would otherwise be written down.
        qCWarning(lcApp) << "no streaming URL for node" << handle << "-- nothing to hand over";
        emit programLaunched(false, name);
        return;
    }

    // Substituted after splitting, never before: a URL carrying a space would
    // otherwise break into two arguments, and quoting %U would then have to account
    // for the URL's own encoding (STUDY_OPEN_WITH.md 3-3-2).
    bool substituted = false;
    for (QString& argument : arguments)
    {
        if (!argument.contains(kUrlPlaceholder))
            continue;
        argument.replace(kUrlPlaceholder, url);
        substituted = true;
    }
    if (!substituted)
        arguments.append(url);

    const bool ok = QProcess::startDetached(program, arguments);
    if (!ok)
        qCWarning(lcApp) << "failed to start Open with program" << name;
    emit programLaunched(ok, name);
}

bool OpenWithController::commandRunnable(const QString& commandLine) const
{
    const QStringList arguments = QProcess::splitCommand(commandLine);
    if (arguments.isEmpty())
        return false;

    // launch() leaves the search to CreateProcess, which only ever appends ".exe";
    // findExecutable would also accept a "code.cmd" that CreateProcess never finds.
    const QString& program = arguments.first();
    const QString withSuffix = QFileInfo(program).suffix().isEmpty()
                                   ? program + QStringLiteral(".exe")
                                   : program;
    if (program.contains(u'/') || program.contains(u'\\'))
        return QFileInfo(program).isFile() || QFileInfo(withSuffix).isFile();
    return !QStandardPaths::findExecutable(withSuffix).isEmpty();
}

QString OpenWithController::commandWithProgram(const QString& commandLine, const QUrl& program) const
{
    const QString quoted =
        QLatin1Char('"') + QDir::toNativeSeparators(program.toLocalFile()) + QLatin1Char('"');
    const qsizetype end = firstTokenEnd(commandLine);
    if (end < 0)
        return quoted + QStringLiteral(" ") + kUrlPlaceholder;
    return quoted + commandLine.mid(end);
}

void OpenWithController::load()
{
    const std::unique_ptr<QSettings> settings = openSettings(mIniFilePath);
    const QString stored = settings->value(QString::fromLatin1(kSettingsKey)).toString();
    if (stored.isEmpty())
        return;

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        qCWarning(lcApp) << "stored Open with programs are not a valid JSON array:"
                         << parseError.errorString();
        return;
    }

    for (const QJsonValue& value : document.array())
    {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        OpenWithEntry entry;
        entry.name = object.value(QString::fromLatin1(kFieldName)).toString().toStdString();
        entry.extensions =
            object.value(QString::fromLatin1(kFieldExtensions)).toString().toStdString();
        entry.commandLine =
            object.value(QString::fromLatin1(kFieldCommandLine)).toString().toStdString();
        // Same test setEntries applies, so a hand-edited settings file cannot put a
        // blank or unstartable row in the menu.
        if (entry.name.empty() || entry.commandLine.empty())
            continue;
        mEntries.push_back(std::move(entry));
    }
}

void OpenWithController::save() const
{
    QJsonArray array;
    for (const OpenWithEntry& entry : mEntries)
    {
        QJsonObject object;
        object.insert(QString::fromLatin1(kFieldName), QString::fromStdString(entry.name));
        object.insert(QString::fromLatin1(kFieldExtensions),
                      QString::fromStdString(entry.extensions));
        object.insert(QString::fromLatin1(kFieldCommandLine),
                      QString::fromStdString(entry.commandLine));
        array.append(object);
    }

    const std::unique_ptr<QSettings> settings = openSettings(mIniFilePath);
    settings->setValue(QString::fromLatin1(kSettingsKey),
                       QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    settings->sync();
    if (settings->status() != QSettings::NoError)
        qCWarning(lcApp) << "failed to write Open with programs, status=" << settings->status();
}
