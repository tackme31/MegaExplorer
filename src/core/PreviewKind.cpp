#include "PreviewKind.h"

#include <cctype>
#include <unordered_set>

namespace
{

// Mirrors what the SDK's own generators cover (third_party/sdk/src/gfx/freeimage.cpp).
// A file outside this list can still carry a preview uploaded by another client, but
// the extension is the only thing knowable before spending a round trip.
//
// Split by generator rather than kept as one media set: all three preview identically,
// but the in-app viewer needs to tell them apart.
const std::unordered_set<std::string>& imageExtensions()
{
    // clang-format off
    static const std::unordered_set<std::string> extensions{
        // FreeImage
        "jpg", "jpeg", "png", "bmp", "gif", "webp", "tga", "tif", "tiff", "ico",
        // FreeImage, RAW
        "cr2", "nef", "arw", "dng", "raf", "orf", "rw2", "pef", "srw", "mrw"};
    // clang-format on
    return extensions;
}

const std::unordered_set<std::string>& videoExtensions()
{
    // clang-format off
    static const std::unordered_set<std::string> extensions{
        // FFmpeg
        "mp4", "mkv", "mov", "avi", "webm", "wmv", "ts", "m4v", "mpg", "mpeg", "flv",
        "3gp", "ogv"};
    // clang-format on
    return extensions;
}

// Unlike the three above, nothing on the server side generates a preview for these --
// they are classified only so the in-app viewer knows to open an audio window. Kept in
// step with FileTypeIcons.qml's music-note list, so the icon and the viewer agree on
// what counts as audio.
const std::unordered_set<std::string>& audioExtensions()
{
    // clang-format off
    static const std::unordered_set<std::string> extensions{
        "mp3", "m4a", "flac", "wav", "aac", "ogg", "opus", "wma"};
    // clang-format on
    return extensions;
}

// svg is here rather than in imageExtensions(): whether the uploading client
// rasterized one into a preview is not predictable, and the markup itself is
// readable.
const std::unordered_set<std::string>& textExtensions()
{
    // "ts" is deliberately absent: videoExtensions() claims it for MPEG-TS and is
    // consulted first, so adding it here would be dead. A TypeScript file therefore
    // lands on "no preview available" rather than showing its source.
    // clang-format off
    static const std::unordered_set<std::string> extensions{
        // Plain text, documents and markup
        "txt", "md", "markdown", "rst", "adoc", "asciidoc", "tex", "bib", "log",
        // Structured data
        "json", "json5", "jsonc", "jsonl", "ndjson", "ipynb", "xml", "plist", "csv",
        "tsv", "yaml", "yml", "toml", "ini", "cfg", "conf", "properties", "env", "reg",
        // Web
        "html", "htm", "xhtml", "css", "scss", "sass", "less", "svg", "vue", "svelte",
        "ejs", "erb", "hbs", "graphql", "gql", "proto",
        // Build and project files
        "cmake", "pro", "pri", "qbs", "qrc", "ui", "rc", "gradle", "mk", "dockerfile",
        "sln", "csproj", "vcxproj", "vbproj", "props", "targets",
        // C family
        "c", "cc", "cpp", "cxx", "h", "hpp", "hxx", "m", "mm", "cs", "vb", "vbs",
        // JVM
        "java", "kt", "kts", "scala", "sbt", "groovy", "clj", "cljs",
        // Scripting
        "js", "jsx", "tsx", "coffee", "py", "rb", "php", "pl", "pm", "lua", "tcl",
        "r", "jl", "awk", "qml", "gd",
        // Shells
        "sh", "bash", "zsh", "fish", "bat", "cmd", "ps1", "psm1", "psd1",
        // Everything else with a mainstream text form
        "go", "rs", "swift", "dart", "zig", "nim", "v", "hs", "ex", "exs", "erl",
        "hrl", "ml", "mli", "fs", "fsx", "el", "lisp", "scm", "d", "pas", "f90",
        "asm", "vhd", "vhdl", "sql",
        // Patches, subtitles and playlists
        "diff", "patch", "po", "pot", "srt", "vtt", "m3u", "m3u8"};
    // clang-format on
    return extensions;
}

std::string lowercaseExtension(const std::string& name)
{
    const std::string::size_type dot = name.find_last_of('.');
    // Position 0 is a dotfile (".gitignore"), not an extension; a trailing dot leaves
    // nothing after it.
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size())
    {
        return {};
    }

    std::string extension = name.substr(dot + 1);
    for (char& c : extension)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

} // namespace

PreviewKind previewKindForName(const std::string& name)
{
    const std::string extension = lowercaseExtension(name);
    if (extension.empty())
    {
        return PreviewKind::None;
    }
    if (imageExtensions().count(extension) > 0)
    {
        return PreviewKind::Image;
    }
    if (videoExtensions().count(extension) > 0)
    {
        return PreviewKind::Video;
    }
    // PDFium's whole share of the media set, so no table of its own.
    if (extension == "pdf")
    {
        return PreviewKind::Pdf;
    }
    if (audioExtensions().count(extension) > 0)
    {
        return PreviewKind::Audio;
    }
    if (textExtensions().count(extension) > 0)
    {
        return PreviewKind::Text;
    }
    // Only zip so far: rar and 7z need a sequential scan and an LZMA decoder
    // respectively (docs/investigations/STUDY_ARCHIVE_PREVIEW.md).
    if (extension == "zip")
    {
        return PreviewKind::Archive;
    }
    return PreviewKind::None;
}
