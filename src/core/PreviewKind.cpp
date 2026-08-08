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
    static const std::unordered_set<std::string> extensions{
        "txt", "md",   "markdown", "json", "log",  "xml", "csv", "tsv",  "ini",
        "cfg", "conf", "yaml",     "yml",  "toml", "sql", "svg", "html", "htm",
        "css", "scss", "c",        "cc",   "cpp",  "cxx", "h",   "hpp",  "hxx",
        "cs",  "java", "js",       "ts",   "jsx",  "tsx", "py",  "rb",   "go",
        "rs",  "php",  "sh",       "bat",  "ps1",  "qml", "pro", "pri",  "cmake"};
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
