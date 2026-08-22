#pragma once
#include <cstdint>
#include <string>

// Image covers video and PDF as well: the server-side preview is a single JPEG for
// all three, generated at upload time by FreeImage/FFmpeg/PDFium.
enum class PreviewKind
{
    None,
    Image,
    Text,
    Archive // listing only -- the entry names, never the contents
};

// Decided from the extension alone -- nothing else about a node is knowable without
// fetching it, and the SDK's own MIME helper hands back a char* the caller must
// delete.
PreviewKind previewKindForName(const std::string& name);

// Text files at or below this are fetched whole; larger ones are refused without any
// network request, since FileEntry::sizeBytes is already in the model. Reading the
// whole file rather than a leading slice is what keeps multi-byte characters from
// being cut in half -- docs/investigations/PREVIEW_PANE_INVESTIGATION.md section 4.5.
constexpr std::uint64_t kMaxTextPreviewBytes = 50 * 1024;
