#pragma once
#include <QByteArray>
#include <QString>

#include <optional>

// Turns a previewed file's raw bytes into text, or reports that they aren't text.
// nullopt means "binary" -- the caller shows that rather than a screen of mojibake.
//
// Lives in src/qml rather than beside PreviewKind in src/core because it needs
// QStringDecoder: src/core links no Qt at all.
std::optional<QString> decodePreviewText(const QByteArray& bytes);
