#include "PdfPageItem.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QQuickWindow>

#include <algorithm>

PdfPageItem::PdfPageItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    // load(QIODevice*) settles synchronously for a random-access device, but the
    // sequential path exists too; taking the signal covers both without asserting
    // which one pdfium picked.
    connect(&mDocument, &QPdfDocument::statusChanged, this, &PdfPageItem::onDocumentStatusChanged);
    connect(&mDocument, &QPdfDocument::pageCountChanged, this, &PdfPageItem::pageCountChanged);
    // The first render can land before the item has a window, and so at the wrong
    // device pixel ratio; gaining one is the cue to redo it.
    connect(this, &QQuickItem::windowChanged, this, &PdfPageItem::renderPage);
}

PdfPageItem::~PdfPageItem()
{
    // mBuffer is declared after mDocument and so destroyed before it; closing here
    // is what keeps the document from unloading through a dead device.
    mDocument.close();
}

void PdfPageItem::setSource(const QUrl& source)
{
    if (mSource == source)
        return;
    mSource = source;
    Q_EMIT sourceChanged();

    reset();
    if (mSource.isEmpty())
    {
        setStatus(Null);
        return;
    }

    setStatus(Loading);
    if (!mNetwork)
        mNetwork = new QNetworkAccessManager(this);
    mReply = mNetwork->get(QNetworkRequest(mSource));
    connect(mReply, &QNetworkReply::finished, this, &PdfPageItem::onFetchFinished);
}

void PdfPageItem::setCurrentPage(int page)
{
    const int clamped = std::clamp(page, 0, std::max(0, pageCount() - 1));
    if (clamped == mCurrentPage)
        return;
    mCurrentPage = clamped;
    Q_EMIT currentPageChanged();
    renderPage();
}

void PdfPageItem::onFetchFinished()
{
    QNetworkReply* reply = mReply;
    mReply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        setStatus(Error);
        return;
    }

    mBytes = reply->readAll();
    mBuffer.setBuffer(&mBytes);
    if (!mBuffer.open(QIODevice::ReadOnly))
    {
        setStatus(Error);
        return;
    }
    mDocument.load(&mBuffer);
    onDocumentStatusChanged();
}

void PdfPageItem::onDocumentStatusChanged()
{
    // Only while a source is in flight: close() during reset() also lands here, and
    // that is not the document failing.
    if (mStatus != Loading)
        return;

    switch (mDocument.status())
    {
        case QPdfDocument::Status::Ready:
            setStatus(Ready);
            renderPage();
            break;
        case QPdfDocument::Status::Error:
            setStatus(Error);
            break;
        case QPdfDocument::Status::Null:
        case QPdfDocument::Status::Loading:
        case QPdfDocument::Status::Unloading:
            break;
    }
}

void PdfPageItem::setStatus(Status status)
{
    if (mStatus == status)
        return;
    mStatus = status;
    Q_EMIT statusChanged();
}

void PdfPageItem::reset()
{
    if (mReply)
    {
        // Disconnected first: abort() emits finished(), and the slot would then treat
        // the aborted fetch as this source's failure.
        mReply->disconnect(this);
        mReply->abort();
        mReply->deleteLater();
        mReply = nullptr;
    }
    mDocument.close();
    mBuffer.close();
    mBytes.clear();
    mRendered = QImage();
    if (mCurrentPage != 0)
    {
        mCurrentPage = 0;
        Q_EMIT currentPageChanged();
    }
    update();
}

void PdfPageItem::renderPage()
{
    mRendered = QImage();
    if (mStatus == Ready && width() > 0 && height() > 0)
    {
        const QSizeF pagePoints = mDocument.pagePointSize(mCurrentPage);
        if (!pagePoints.isEmpty())
        {
            const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
            const qreal scale =
                std::min(width() / pagePoints.width(), height() / pagePoints.height());
            const QSize target = (pagePoints * scale * dpr).toSize();
            if (!target.isEmpty())
                mRendered = mDocument.render(mCurrentPage, target);
        }
    }
    update();
}

void PdfPageItem::paint(QPainter* painter)
{
    if (mRendered.isNull())
        return;

    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSizeF drawn = QSizeF(mRendered.size()) / dpr;
    const QRectF target(QPointF((width() - drawn.width()) / 2, (height() - drawn.height()) / 2),
                        drawn);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // The paper, which the render does not draw: QPdfDocument::render hands back an
    // ARGB image cleared to transparent, so an unpainted area of the page would show
    // the window's dark ground through it and take the text with it.
    painter->fillRect(target, Qt::white);
    painter->drawImage(target, mRendered);
}

void PdfPageItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        renderPage();
}
