#pragma once
#include "PreviewKind.h"

#include <cstdint>
#include <vector>

// What a file's first bytes prove it to be, for Open as
// (docs/investigations/STUDY_OPEN_AS.md section 3). Only the families a viewer can be
// mistaken across; video and audio share one, since a container does not say which
// tracks it holds.
enum class SniffedFormat
{
    Unknown,
    Image,
    Media,
    Pdf,
    Zip
};

// Covers every signature below, PDF's included: it may sit behind a short prefix.
constexpr std::uint64_t kFormatSniffBytes = 64;

SniffedFormat sniffFormat(const std::vector<char>& head);

// True only when the bytes prove a format the chosen viewer cannot show. Unknown never
// rules anything out: formats without a signature (TGA, MPEG-TS, raw MP3) are left to
// the viewer's own decode.
bool sniffRulesOut(SniffedFormat sniffed, PreviewKind chosen);
