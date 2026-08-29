#pragma once
#include <cstdint>
#include <string>

// Image, Video and Pdf share one preview path -- the server-side preview is a single
// JPEG for all three, generated at upload time by FreeImage/FFmpeg/PDFium. They are
// separate values because the in-app viewer opens each with a different Qt type.
enum class PreviewKind
{
    None,
    Image,
    Video,
    Pdf,
    Text,
    Archive, // listing only -- the entry names, never the contents
    Audio    // viewer only -- no server-side preview exists for it (USE_MEDIAINFO is off)
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
