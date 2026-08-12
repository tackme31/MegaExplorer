pragma Singleton
import QtQuick
import MegaExplorer

// The whitelist that turns a file name into the icon drawn in front of it. One
// block per type, carrying the *pair* -- font family and code point -- plus the
// extensions that claim it, so a type whose picture is missing from Segoe Fluent
// Icons can name a different font here instead of growing a special case in
// FileIcon.qml. An extension nobody claims keeps the generic page, which is what
// lets this stay a whitelist.
//
// The code points were checked against both Segoe Fluent Icons and Segoe MDL2
// Assets with the script in docs/DESIGN_IMPROVEMENT.md section 11, then drawn at
// 16px before being picked -- presence in a cmap is necessary but not sufficient
// (that section's S9 note).
QtObject {
    id: root

    component IconSpec: QtObject {
        property string family: Theme.font.iconFamily
        property string glyph
        // Lower-case, no leading dot.
        property var extensions: []
    }

    // Shares Theme's file glyph rather than restating it, so the two cannot drift.
    readonly property IconSpec fallback: IconSpec {
        glyph: Theme.glyph.file
    }

    readonly property list<IconSpec> types: [
        IconSpec {
            glyph: "\uE91B" // Photo2
            extensions: ["png", "jpg", "jpeg", "jfif", "jpe", "gif", "bmp", "webp", "tif", "tiff",
                "svg", "ico", "heic", "avif", "psd", "cr2", "nef", "arw", "dng", "raf"]
        },
        IconSpec {
            glyph: "\uE714" // Video
            extensions: ["mp4", "m4v", "mkv", "avi", "mov", "wmv", "webm", "flv", "mpg", "mpeg"]
        },
        IconSpec {
            glyph: "\uEC4F" // MusicNote
            extensions: ["mp3", "flac", "wav", "aac", "ogg", "opus", "m4a", "wma"]
        },
        IconSpec {
            glyph: "\uEA90" // PDF
            extensions: ["pdf"]
        },
        IconSpec {
            glyph: "\uF012" // ZipFolder
            extensions: ["zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "iso"]
        },
        IconSpec {
            glyph: "\uE943" // Code
            extensions: ["c", "h", "cpp", "hpp", "cc", "cs", "js", "ts", "py", "rb", "go", "rs",
                "java", "kt", "php", "sh", "bat", "ps1", "qml", "json", "xml", "yaml", "yml", "toml",
                "ini", "html", "css"]
        },
        IconSpec {
            glyph: "\uE8A5" // Document
            extensions: ["txt", "md", "log", "rtf", "doc", "docx", "odt"]
        },
        IconSpec {
            glyph: "\uE9F9" // ReportDocument
            extensions: ["csv", "xls", "xlsx", "ods"]
        }
    ]

    // A dot at position 0 is a leading-dot name (".gitignore"), not an extension.
    function forFileName(fileName) {
        const dot = fileName.lastIndexOf(".");
        if (dot <= 0 || dot === fileName.length - 1)
            return root.fallback;
        const extension = fileName.substring(dot + 1).toLowerCase();
        for (let i = 0; i < root.types.length; ++i) {
            if (root.types[i].extensions.indexOf(extension) !== -1)
                return root.types[i];
        }
        return root.fallback;
    }
}
