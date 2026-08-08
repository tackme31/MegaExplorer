#include "TextPreviewDecoder.h"

#include <QStringDecoder>

#include <utility>

namespace
{

std::optional<QString> decodeWith(QStringDecoder decoder, const QByteArray& bytes)
{
    if (!decoder.isValid())
        return std::nullopt;
    QString text = decoder.decode(bytes);
    if (decoder.hasError())
        return std::nullopt;
    return text;
}

} // namespace

std::optional<QString> decodePreviewText(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return QString();

    // A BOM settles the question outright, and UTF-16 has to be recognised here
    // rather than below: half the bytes of a Latin UTF-16 file are NUL, so the
    // binary check would throw it out.
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return decodeWith(QStringDecoder(QStringDecoder::Utf8), bytes.mid(3));
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF"))
        return decodeWith(QStringDecoder(QStringDecoder::Utf16), bytes);

    // A NUL anywhere means this is not text, whatever the extension claimed. The
    // whole buffer, not just its head: it is 50 KB at most.
    if (bytes.contains('\0'))
        return std::nullopt;

    if (std::optional<QString> utf8 = decodeWith(QStringDecoder(QStringDecoder::Utf8), bytes))
        return utf8;

    // Invalid as UTF-8, which in practice means a Japanese file in CP932. Qt reaches
    // that codec only through the ICU-backed name lookup, so where Qt was built
    // without ICU this falls back to the system codepage -- CP932 itself on the
    // Japanese Windows this matters on.
    QStringDecoder shiftJis("Shift-JIS");
    if (shiftJis.isValid())
        return decodeWith(std::move(shiftJis), bytes);
    return decodeWith(QStringDecoder(QStringDecoder::System), bytes);
}
