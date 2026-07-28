#pragma once
#include "core/INodeCache.h"

#include <string>

struct sqlite3; // sqlite3.h stays confined to the .cpp

// The only file in this project allowed to include <sqlite3.h> or touch
// sqlite3_* symbols directly, mirroring src/mega's exclusivity over
// <megaapi.h>. Not part of MegaExplorerCore (parallels src/mega being
// compiled directly into appMegaExplorer rather than into the SDK-free
// static lib) -- but unlike MegaSdkClient, this adapter needs no live
// account to test, so it gets its own adapter-level test
// (tests/SqliteNodeCacheTest.cpp) exercising the real sqlite3 engine against
// a ":memory:" database.
class SqliteNodeCache : public INodeCache
{
public:
    // dbPath: a fully resolved file path, or ":memory:" (tests use the same
    // constructor/code path as production, no separate test-only ctor). If
    // the database can't be opened or migrated, the cache becomes
    // permanently unusable (mUsable stays false) and every subsequent
    // loadChildren/saveChildren call returns Result::fail immediately
    // without touching sqlite3 again. Never throws, never crashes the app --
    // a broken cache must only ever degrade to network-only behavior.
    explicit SqliteNodeCache(const std::string& dbPath);
    ~SqliteNodeCache() override;

    SqliteNodeCache(const SqliteNodeCache&) = delete;
    SqliteNodeCache& operator=(const SqliteNodeCache&) = delete;

    Result<std::vector<FileEntry>> loadChildren(const ParentKey& parent) const override;
    Result<void> saveChildren(const ParentKey& parent,
                              const std::vector<FileEntry>& entries) override;

private:
    // Runs CREATE TABLE IF NOT EXISTS. Returns false (and leaves mUsable
    // false) on any sqlite error.
    bool migrate();

    sqlite3* mDb = nullptr;
    bool mUsable = false;
};
