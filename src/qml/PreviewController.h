#pragma once
#include "core/PreviewService.h"
#include "core/Result.h"
#include "PreviewImageStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>
#include <optional>
#include <QtQml/qqmlregistration.h>
#include <string>
#include <vector>

// Drives the window's single preview pane: given the one selected row, it decides
// what kind of preview that name can have, fetches it, and exposes the result as a
// state machine PreviewPane.qml switches on.
//
// One per window, not per tab. The pane is a single item in Main.qml's SplitView,
// and nothing here is tab-specific -- the input arrives as arguments. Which tab is
// asking only matters in that switching tabs must invalidate whatever is in flight,
// and bumping the generation on every showSelection()/clear() that changes the shown
// node already does that.
//
// No NotificationController, unlike every other controller that can fail: "this file
// has no preview" is the normal case, not an error, so failures land in the pane's
// own wording and in qCDebug -- never a toast. AuthController is the other
// controller deliberately built without one.
class PreviewController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the previewController context property")

public:
    enum State
    {
        Empty,      // nothing selected, several rows selected, or a folder
        Loading,    // fetch in flight
        Ready,      // imageSource or text is set, per kind
        Unsupported // nothing to show; reason says why
    };
    Q_ENUM(State)

    enum Kind
    {
        NoKind,
        Image, // covers video and PDF: all three arrive as one server-side JPEG
        Text,
        Archive // entry listing of a zip, read from its central directory
    };
    Q_ENUM(Kind)

    // A code, not a sentence: QML composes the wording, as FileListModel.h requires
    // of everything user-facing.
    enum Reason
    {
        NoReason,
        NoPreviewAvailable, // the type is previewable, this file has no preview stored
        UnsupportedType,    // the extension is not one this app previews
        TooLarge,           // text past kMaxTextPreviewBytes, refused without a request
        BinaryContent,      // a text extension whose bytes are not text
        ArchiveUnreadable,  // a zip whose central directory could not be located
        ArchiveEmpty        // a readable zip that holds no entries
    };
    Q_ENUM(Reason)

    // One signal for all five, not one each: they only ever move together as a state
    // transition, and separate NOTIFYs would let QML observe kind == Text alongside
    // the previous imageSource. DownloadController splits its signals because its
    // progress and file name genuinely change independently.
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // One flattened row per archive entry: name, depth, isDirectory, formattedSize.
    Q_PROPERTY(QVariantList archiveEntries READ archiveEntries NOTIFY changed)
    Q_PROPERTY(Reason reason READ reason NOTIFY changed)

    explicit PreviewController(std::shared_ptr<PreviewService> service,
                               std::shared_ptr<PreviewImageStore> imageStore,
                               QObject* parent = nullptr);

    // The single selected row. Everything needed to classify it is passed in, so
    // this never reaches back into a model that may belong to another tab by the
    // time a result lands. Repeating the handle already shown does nothing -- QML
    // re-emits the selection on a click that changed nothing.
    Q_INVOKABLE void
    showSelection(quint64 handle, const QString& name, qulonglong sizeBytes, bool isFolder);

    // Back to Empty, and invalidates anything in flight. Called when the selection
    // is not exactly one row, and when the pane is hidden.
    Q_INVOKABLE void clear();

    State state() const
    {
        return mState;
    }
    Kind kind() const
    {
        return mKind;
    }
    QString imageSource() const
    {
        return mImageSource;
    }
    QString text() const
    {
        return mText;
    }
    QVariantList archiveEntries() const
    {
        return mArchiveEntries;
    }
    Reason reason() const
    {
        return mReason;
    }

signals:
    void changed();

private:
    void publish(State state, Kind kind, Reason reason);
    void requestImage(quint64 handle);
    void
    onImageFetched(quint64 generation, const QString& destinationPath, Result<std::string> result);
    void requestText(quint64 handle, qulonglong sizeBytes);
    void onTextFetched(quint64 generation, Result<std::vector<char>> result);
    void requestArchive(quint64 handle, qulonglong sizeBytes);
    void onArchiveTailFetched(quint64 generation, Result<std::vector<char>> result);
    void onArchiveDirectoryFetched(quint64 generation, Result<std::vector<char>> result);

    // Unique per request, so no two previews ever share an image:// URL --
    // QQuickPixmapCache keys on it, and a repeated URL would show the first picture
    // forever. Deliberately the inverse of ThumbnailController's fixed name per
    // handle, which exists precisely so repeats reuse one cached file.
    QString computeDestinationPath(quint64 handle, quint64 generation) const;

    std::shared_ptr<PreviewService> mService;
    std::shared_ptr<PreviewImageStore> mImageStore;

    // GUI thread only: showSelection/clear come from QML, and results are hopped
    // back before they are compared against it.
    quint64 mGeneration = 0;

    // The node the pane is currently showing (or fetching), so a repeat of the same
    // selection is a no-op rather than a refetch.
    std::optional<quint64> mShownHandle;

    State mState = Empty;
    Kind mKind = NoKind;
    QString mImageSource;
    QString mText;
    QVariantList mArchiveEntries;
    Reason mReason = NoReason;

    // Carried between the two range reads a zip listing takes: the tail read has to
    // say where in the file its slice started before the EOCD offsets mean anything.
    quint64 mArchiveHandle = 0;
    quint64 mArchiveSize = 0;
    quint64 mArchiveTailOffset = 0;
};
