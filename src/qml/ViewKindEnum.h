#pragma once
#include "core/ViewKind.h"

#include <QObject>

#include <QtQml/qqmlregistration.h>

// QML-facing mirror of core's ViewKind: src/core links no Qt, so Q_ENUM cannot sit
// next to the enum itself. Never instantiated -- it only carries the enum names.
class ViewKindEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ViewKind)
    QML_UNCREATABLE("Enum holder for core's ViewKind")

public:
    // Unscoped on purpose, so QML reads these as ViewKind.Favourites.
    enum Kind
    {
        CloudDrive,
        Favourites,
        Rubbish,
        Recents,
    };
    Q_ENUM(Kind)
};

static_assert(ViewKindEnum::CloudDrive == static_cast<int>(ViewKind::CloudDrive));
static_assert(ViewKindEnum::Favourites == static_cast<int>(ViewKind::Favourites));
static_assert(ViewKindEnum::Rubbish == static_cast<int>(ViewKind::Rubbish));
static_assert(ViewKindEnum::Recents == static_cast<int>(ViewKind::Recents));
