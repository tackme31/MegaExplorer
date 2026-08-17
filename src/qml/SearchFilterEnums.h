#pragma once
#include "core/SearchFilter.h"

#include <QObject>

#include <QtQml/qqmlregistration.h>

// QML-facing mirrors of core's SearchFilter enums, for the advanced-search popup;
// src/core links no Qt, so Q_ENUM cannot sit next to the enums themselves. Three
// holders rather than one: QML flattens a type's enumerators into that type's scope,
// so a single holder would need the three "Any" entries renamed apart.
// Never instantiated -- they only carry the enum names. Same shape as ViewKindEnum.h.

class SearchNodeTypeEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SearchNodeType)
    QML_UNCREATABLE("Enum holder for core's SearchNodeType")

public:
    // Unscoped on purpose, so QML reads these as SearchNodeType.Files.
    enum Kind
    {
        Any,
        Files,
        Folders,
    };
    Q_ENUM(Kind)
};

class SearchCategoryEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SearchCategory)
    QML_UNCREATABLE("Enum holder for core's SearchCategory")

public:
    enum Kind
    {
        Any,
        Photo,
        Audio,
        Video,
        Document,
        Pdf,
        Presentation,
        Spreadsheet,
        Archive,
        Program,
        Other,
    };
    Q_ENUM(Kind)
};

class SearchTimeWindowEnum : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SearchTimeWindow)
    QML_UNCREATABLE("Enum holder for core's SearchTimeWindow")

public:
    enum Kind
    {
        Any,
        PastDay,
        PastWeek,
        PastMonth,
        PastYear,
    };
    Q_ENUM(Kind)
};

static_assert(SearchNodeTypeEnum::Any == static_cast<int>(SearchNodeType::Any));
static_assert(SearchNodeTypeEnum::Files == static_cast<int>(SearchNodeType::Files));
static_assert(SearchNodeTypeEnum::Folders == static_cast<int>(SearchNodeType::Folders));

static_assert(SearchCategoryEnum::Any == static_cast<int>(SearchCategory::Any));
static_assert(SearchCategoryEnum::Photo == static_cast<int>(SearchCategory::Photo));
static_assert(SearchCategoryEnum::Audio == static_cast<int>(SearchCategory::Audio));
static_assert(SearchCategoryEnum::Video == static_cast<int>(SearchCategory::Video));
static_assert(SearchCategoryEnum::Document == static_cast<int>(SearchCategory::Document));
static_assert(SearchCategoryEnum::Pdf == static_cast<int>(SearchCategory::Pdf));
static_assert(SearchCategoryEnum::Presentation == static_cast<int>(SearchCategory::Presentation));
static_assert(SearchCategoryEnum::Spreadsheet == static_cast<int>(SearchCategory::Spreadsheet));
static_assert(SearchCategoryEnum::Archive == static_cast<int>(SearchCategory::Archive));
static_assert(SearchCategoryEnum::Program == static_cast<int>(SearchCategory::Program));
static_assert(SearchCategoryEnum::Other == static_cast<int>(SearchCategory::Other));

static_assert(SearchTimeWindowEnum::Any == static_cast<int>(SearchTimeWindow::Any));
static_assert(SearchTimeWindowEnum::PastDay == static_cast<int>(SearchTimeWindow::PastDay));
static_assert(SearchTimeWindowEnum::PastWeek == static_cast<int>(SearchTimeWindow::PastWeek));
static_assert(SearchTimeWindowEnum::PastMonth == static_cast<int>(SearchTimeWindow::PastMonth));
static_assert(SearchTimeWindowEnum::PastYear == static_cast<int>(SearchTimeWindow::PastYear));
