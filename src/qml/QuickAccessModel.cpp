#include "QuickAccessModel.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace
{
// One in-flight validation sweep. Shared by all N callbacks so the last one
// to arrive can commit the reconciled list in a single write.
struct Sweep
{
    // Snapshot of the pins at sweep start. A surviving pin gets its name
    // refreshed in place; status[i] is what says whether it survived at all.
    // Handles stay intact even for dropped pins, because the commit looks
    // results up by handle rather than by position.
    std::vector<PinnedFolder> resolved;
    std::vector<QuickAccessService::PinStatus> status;
    int remaining = 0;
};
} // namespace

QuickAccessModel::QuickAccessModel(std::shared_ptr<QuickAccessService> service,
                                   NotificationController* notifications,
                                   QObject* parent)
    : QAbstractListModel(parent), mService(std::move(service)), mNotifications(notifications)
{
    // No invokeOnGuiThread: the service calls this synchronously from inside
    // the mutator that failed, and every mutator is reached from the GUI
    // thread (validateAll's commit included -- it runs inside its own
    // invokeOnGuiThread).
    mService->setOnPersistenceFailed([this](const Result<void>& failure) {
        qCWarning(lcQuickAccess) << "failed to persist quick-access pins:"
                                 << failure.errorMessage.c_str() << "code=" << failure.errorCode;
        if (mNotifications)
        {
            mNotifications->notifyError(QStringLiteral("quickAccessSave"));
        }
    });
}

QuickAccessModel::~QuickAccessModel() = default;

int QuickAccessModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(mService->pins().size());
}

QVariant QuickAccessModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    const PinnedFolder& pin = mService->pins()[static_cast<std::size_t>(index.row())];
    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:
            return QString::fromStdString(pin.name);
        case HandleRole:
            return QVariant(static_cast<qulonglong>(pin.handle));
        default:
            return {};
    }
}

QHash<int, QByteArray> QuickAccessModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {NameRole, "name"}, {HandleRole, "handle"}};
}

int QuickAccessModel::count() const
{
    return rowCount();
}

void QuickAccessModel::reload()
{
    beginResetModel();
    ++mGeneration;
    const Result<void> loaded = mService->load();
    endResetModel();
    // Logged, not toasted: the pin list simply comes up empty, and nothing the
    // user did is being lost at this point (a sign-out mid-login reaches this
    // too, via currentUserHandle).
    if (!loaded.success)
    {
        qCWarning(lcQuickAccess) << "failed to load quick-access pins:"
                                 << loaded.errorMessage.c_str() << "code=" << loaded.errorCode;
    }
    emit countChanged();

    validateAll();
}

void QuickAccessModel::reset()
{
    beginResetModel();
    ++mGeneration;
    mService->clear();
    endResetModel();
    emit countChanged();
}

bool QuickAccessModel::isPinned(quint64 handle) const
{
    return mService->isPinned(handle);
}

void QuickAccessModel::pin(quint64 handle, const QString& name)
{
    // Checked before beginInsertRows rather than relying on pin()'s return
    // value: announcing an insertion the service then refuses would leave the
    // view believing in a row that doesn't exist.
    if (handle == 0 || mService->isPinned(handle))
        return;

    PinnedFolder folder;
    folder.name = name.toStdString();
    folder.handle = handle;

    const int row = rowCount();
    beginInsertRows(QModelIndex(), row, row);
    mService->pin(folder);
    endInsertRows();
    emit countChanged();
}

void QuickAccessModel::unpin(quint64 handle)
{
    const int row = rowFor(handle);
    if (row < 0)
        return;

    beginRemoveRows(QModelIndex(), row, row);
    mService->unpin(handle);
    endRemoveRows();
    emit countChanged();
}

void QuickAccessModel::move(quint64 handle, int toRow)
{
    const int from = rowFor(handle);
    if (from < 0)
        return;

    const int to = std::clamp(toRow, 0, rowCount() - 1);
    if (to == from)
        return;

    // beginMoveRows' destination is an insertion point in the *pre-move*
    // coordinates, so moving down has to name the row after the target.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    mService->move(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    endMoveRows();
}

void QuickAccessModel::activate(quint64 handle, bool inNewTab)
{
    // Captured now: if the pin turns out to be gone, missing() reports the
    // label the user actually clicked, which unpin() will then remove.
    const QString name = nameFor(handle);
    const std::uint64_t generation = mGeneration;

    mService->resolveFolder(
        handle, [this, handle, inNewTab, name, generation](Result<NodeInfo> resolved) {
            invokeOnGuiThread(this, [this, handle, inNewTab, name, generation, resolved]() {
                if (generation != mGeneration)
                    return;
                switch (QuickAccessService::classify(resolved))
                {
                    case QuickAccessService::PinStatus::Usable:
                        emit activated(handle, inNewTab);
                        break;
                    case QuickAccessService::PinStatus::Gone:
                        emit missing(handle, name);
                        break;
                    // Not missing(): that offers to unpin, and nothing here
                    // says the target is actually gone. A toast, so the click
                    // doesn't just look ignored.
                    case QuickAccessService::PinStatus::Unknown:
                        qCWarning(lcQuickAccess)
                            << "could not verify quick-access pin" << name << "handle=" << handle
                            << "code=" << resolved.errorCode;
                        if (mNotifications)
                        {
                            mNotifications->notifyError(QStringLiteral("quickAccessUnavailable"));
                        }
                        break;
                }
            });
        });
}

void QuickAccessModel::validateAll()
{
    const std::vector<PinnedFolder> snapshot = mService->pins();
    if (snapshot.empty())
        return;

    auto sweep = std::make_shared<Sweep>();
    sweep->resolved = snapshot;
    sweep->status.assign(snapshot.size(), QuickAccessService::PinStatus::Unknown);
    sweep->remaining = static_cast<int>(snapshot.size());

    const std::uint64_t generation = mGeneration;

    for (std::size_t i = 0; i < snapshot.size(); ++i)
    {
        mService->resolveFolder(
            snapshot[i].handle, [this, sweep, i, generation](Result<NodeInfo> resolved) {
                invokeOnGuiThread(this, [this, sweep, i, generation, resolved]() {
                    if (generation != mGeneration)
                        return;

                    PinnedFolder& pin = sweep->resolved[i];
                    const QuickAccessService::PinStatus status =
                        QuickAccessService::classify(resolved);
                    sweep->status[i] = status;
                    switch (status)
                    {
                        case QuickAccessService::PinStatus::Usable:
                            // A handle is stable across moves and renames, so
                            // re-reading the name is all it takes to follow one.
                            pin.name = resolved.value().name;
                            break;
                        case QuickAccessService::PinStatus::Gone:
                            qCInfo(lcQuickAccess)
                                << "dropping dangling quick-access pin"
                                << QString::fromStdString(pin.name) << "handle=" << pin.handle;
                            break;
                        case QuickAccessService::PinStatus::Unknown:
                            // Left in the list *and* left unrenamed, so the
                            // commit below sees a value identical to what's
                            // already stored and skips the write entirely.
                            qCWarning(lcQuickAccess)
                                << "keeping unverified quick-access pin"
                                << QString::fromStdString(pin.name) << "handle=" << pin.handle
                                << "code=" << resolved.errorCode;
                            break;
                    }

                    if (--sweep->remaining > 0)
                        return;

                    // Committed against the *current* list, not the snapshot's order:
                    // a reorder or a pin() may have landed mid-sweep, and replaying
                    // the snapshot would undo it. The sweep contributes only names
                    // and drops, keyed by handle, and only Gone drops -- Unknown
                    // passes through like a handle the sweep never saw.
                    const std::vector<PinnedFolder>& current = mService->pins();
                    std::vector<PinnedFolder> survivors;
                    survivors.reserve(current.size());
                    for (const PinnedFolder& pinned : current)
                    {
                        const auto found =
                            std::find_if(sweep->resolved.begin(),
                                         sweep->resolved.end(),
                                         [&pinned](const PinnedFolder& candidate) {
                                             return candidate.handle == pinned.handle;
                                         });
                        if (found == sweep->resolved.end())
                            survivors.push_back(pinned);
                        else if (sweep->status[static_cast<std::size_t>(found -
                                                                        sweep->resolved.begin())] !=
                                 QuickAccessService::PinStatus::Gone)
                            survivors.push_back(*found);
                    }

                    // Skip the model reset when the sweep changed nothing, which is
                    // the common case.
                    if (survivors == mService->pins())
                        return;

                    beginResetModel();
                    mService->replaceAll(std::move(survivors));
                    endResetModel();
                    emit countChanged();
                });
            });
    }
}

int QuickAccessModel::rowFor(quint64 handle) const
{
    const std::vector<PinnedFolder>& pins = mService->pins();
    for (std::size_t row = 0; row < pins.size(); ++row)
    {
        if (pins[row].handle == handle)
            return static_cast<int>(row);
    }
    return -1;
}

QString QuickAccessModel::nameFor(quint64 handle) const
{
    const int row = rowFor(handle);
    if (row < 0)
        return {};
    return QString::fromStdString(mService->pins()[static_cast<std::size_t>(row)].name);
}
