#pragma once
#include "core/DownloadService.h"
#include "core/UploadService.h"

#include <QObject>

#include <QtQml/qqmlregistration.h>

// QML-facing mirrors of what a TransferListModel row carries; src/core links no Qt,
// so Q_ENUM cannot sit next to DownloadState/UploadState themselves. Never
// instantiated -- they only carry the enum names. Same shape as ViewKindEnum.h.

class TransferDirectionEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TransferDirection)
    QML_UNCREATABLE("Enum holder for TransferListModel's direction role")

public:
    // Unscoped on purpose, so QML reads these as TransferDirection.Download.
    enum Kind
    {
        Download,
        Upload,
    };
    Q_ENUM(Kind)
};

class TransferStateEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TransferState)
    QML_UNCREATABLE("Enum holder for TransferListModel's state role")

public:
    enum Kind
    {
        Queued,
        Active,
        Completed,
        Failed,
        Cancelled,
    };
    Q_ENUM(Kind)
};

// One list merges both queues, so a row's state must mean the same thing whichever
// service produced it. These pin that: adding a state to one core enum without the
// other, or reordering either, stops compiling here rather than silently mislabelling
// rows.
static_assert(TransferStateEnum::Queued == static_cast<int>(DownloadState::Queued));
static_assert(TransferStateEnum::Active == static_cast<int>(DownloadState::Active));
static_assert(TransferStateEnum::Completed == static_cast<int>(DownloadState::Completed));
static_assert(TransferStateEnum::Failed == static_cast<int>(DownloadState::Failed));
static_assert(TransferStateEnum::Cancelled == static_cast<int>(DownloadState::Cancelled));

static_assert(TransferStateEnum::Queued == static_cast<int>(UploadState::Queued));
static_assert(TransferStateEnum::Active == static_cast<int>(UploadState::Active));
static_assert(TransferStateEnum::Completed == static_cast<int>(UploadState::Completed));
static_assert(TransferStateEnum::Failed == static_cast<int>(UploadState::Failed));
static_assert(TransferStateEnum::Cancelled == static_cast<int>(UploadState::Cancelled));
