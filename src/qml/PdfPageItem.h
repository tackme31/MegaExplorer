#pragma once
#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QPdfDocument>
#include <QQuickPaintedItem>
#include <QUrl>

#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

// Draws one page of a PDF fetched over the SDK's local HTTP server, and is the whole
// of the in-app PDF viewer's rendering: PdfViewer.qml owns the window and the page
// controls, this owns the document.
//
// The file is pulled into memory whole before anything is drawn. Qt PDF needs a
// randomly seekable QIODevice, which a stream off that server is not, so the
// alternatives were a QIODevice written over the SDK's startStreaming or a temp file
// -- the first is far more than this costs, the second puts decrypted bytes on disk,
// which is the property the streaming viewer exists to keep
// (docs/investigations/STUDY_INAPP_VIEWER.md section 3-3).
//
// Not QtQuick.Pdf's own PdfMultiPageView: its `document` is typed to the QML
// PdfDocument, whose source it resolves with QUrl::toLocalFile(), so an http URL
// reaches pdfium as an empty filename.
class PdfPageItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged FINAL)
    Q_PROPERTY(
        int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged FINAL)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged FINAL)

public:
    // Unscoped so QML reads them as PdfPageItem.Ready. Loading covers both halves of
    // the wait -- the fetch and pdfium's parse -- because a viewer showing a spinner
    // has no use for the difference.
    enum Status
    {
        Null,
        Loading,
        Ready,
        Error
    };
    Q_ENUM(Status)

    explicit PdfPageItem(QQuickItem* parent = nullptr);
    ~PdfPageItem() override;

    QUrl source() const
    {
        return mSource;
    }
    void setSource(const QUrl& source);

    int pageCount() const
    {
        return mDocument.pageCount();
    }

    int currentPage() const
    {
        return mCurrentPage;
    }
    void setCurrentPage(int page);

    Status status() const
    {
        return mStatus;
    }

    void paint(QPainter* painter) override;

Q_SIGNALS:
    void sourceChanged();
    void pageCountChanged();
    void currentPageChanged();
    void statusChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void onFetchFinished();
    void onDocumentStatusChanged();
    void setStatus(Status status);
    void reset();
    // Renders on the GUI thread and caches the result, so paint() only blits.
    // QQuickPaintedItem::paint runs on the render thread, and pdfium is not the place
    // to rely on the sync-phase block that makes that usually safe.
    void renderPage();

    QUrl mSource;
    QPdfDocument mDocument;
    // Held for as long as the document is open: QPdfDocument keeps reading through
    // the device it was handed, it does not copy.
    QByteArray mBytes;
    QBuffer mBuffer;
    QNetworkAccessManager* mNetwork = nullptr;
    QNetworkReply* mReply = nullptr;
    QImage mRendered;
    int mCurrentPage = 0;
    Status mStatus = Null;
};
