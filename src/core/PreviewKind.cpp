#include "PreviewKind.h"

#include <cctype>
#include <unordered_set>

namespace
{

// Mirrors what the SDK's own generators cover (third_party/sdk/src/gfx/freeimage.cpp).
// A file outside this list can still carry a preview uploaded by another client, but
// the extension is the only thing knowable before spending a round trip.
const std::unordered_set<std::string>& mediaExtensions()
{
    static const std::unordered_set<std::string> extensions{// FreeImage
                                                            "jpg",
                                                            "jpeg",
                                                            "png",
                                                            "bmp",
                                                            "gif",
                                                            "webp",
                                                            "tga",
                                                            "tif",
                                                            "tiff",
                                                            "ico",
                                                            // FreeImage, RAW
                                                            "cr2",
                                                            "nef",
                                                            "arw",
                                                            "dng",
                                                            "raf",
                                                            "orf",
                                                            "rw2",
                                                            "pef",
                                                            "srw",
                                                            "mrw",
                                                            // FFmpeg
                                                            "mp4",
                                                            "mkv",
                                                            "mov",
                                                            "avi",
                                                            "webm",
                                                            "wmv",
                                                            "ts",
                                                            "m4v",
                                                            "mpg",
                                                            "mpeg",
                                                            "flv",
                                                            "3gp",
                                                            "ogv",
                                                            // PDFium
                                                            "pdf"};
    return extensions;
}

// svg is here rather than in mediaExtensions(): whether the uploading client
// rasterized one into a preview is not predictable, and the markup itself is
// readable.
const std::unordered_set<std::string>& textExtensions()
{
    // "ts" is deliberately absent: mediaExtensions() claims it for MPEG-TS and is
    // consulted first, so adding it here would be dead. A TypeScript file therefore
    // lands on "no preview available" rather than showing its source.
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
    if (mediaExtensions().count(extension) > 0)
    {
        return PreviewKind::Image;
    }
    if (textExtensions().count(extension) > 0)
    {
        return PreviewKind::Text;
    }
    return PreviewKind::None;
}
