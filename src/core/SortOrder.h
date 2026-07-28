#pragma once

// Sort specification passed down to IMegaClient so sorting happens
// server-side (MegaApi::getChildren/search's order argument) rather than in
// app memory -- avoids sorting tens of thousands of entries client-side (see
// MEMO.md's Phase 6b decision, 2026-07-28). Qt-free like DownloadOutcome.h so
// it stays usable from src/core without pulling Qt into that layer.
enum class SortKey
{
    Name,
    Size,
    ModificationTime,
};

struct SortOrder
{
    SortKey key = SortKey::Name;
    bool ascending = true;
};
