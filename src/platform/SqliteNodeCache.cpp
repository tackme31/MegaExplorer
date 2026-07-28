#include "SqliteNodeCache.h"

#include "app/Logging.h"

#include <QString>

#include <sqlite3.h>

namespace
{

// One row per (parent, child) pair. The composite primary key already gives
// SQLite an index covering the "WHERE parent_is_root = ? AND parent_handle =
// ?" lookup loadChildren needs, so no separate CREATE INDEX is added --
// it would just duplicate the PK's own index.
constexpr const char* kCreateTableSql = R"(
CREATE TABLE IF NOT EXISTS node_cache (
    parent_is_root INTEGER NOT NULL,
    parent_handle  INTEGER NOT NULL,
    handle         INTEGER NOT NULL,
    name           TEXT NOT NULL,
    size_bytes     INTEGER NOT NULL,
    is_folder      INTEGER NOT NULL,
    mtime          INTEGER NOT NULL,
    has_thumbnail  INTEGER NOT NULL,
    PRIMARY KEY (parent_is_root, parent_handle, handle)
);
)";

constexpr const char* kSelectSql =
    "SELECT handle, name, size_bytes, is_folder, mtime, has_thumbnail "
    "FROM node_cache WHERE parent_is_root = ? AND parent_handle = ?;";

constexpr const char* kDeleteSql =
    "DELETE FROM node_cache WHERE parent_is_root = ? AND parent_handle = ?;";

constexpr const char* kInsertSql =
    "INSERT INTO node_cache "
    "(parent_is_root, parent_handle, handle, name, size_bytes, is_folder, mtime, has_thumbnail) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

constexpr const char* kClearAllSql = "DELETE FROM node_cache;";

// MEGA handles are 64-bit and practically never use the sign bit; storing
// them in SQLite's signed INTEGER column via a bit-reinterpreting cast
// round-trips losslessly regardless.
sqlite3_int64 toSqlHandle(std::uint64_t handle)
{
    return static_cast<sqlite3_int64>(handle);
}

std::uint64_t fromSqlHandle(sqlite3_int64 handle)
{
    return static_cast<std::uint64_t>(handle);
}

// For the INFO-level cache-hit/refresh logs below -- verifying Phase 6's
// cache-then-refresh behavior end to end otherwise requires eyeballing the
// UI for a flash of cached content, which is easy to miss.
QString describeParent(const INodeCache::ParentKey& parent)
{
    return parent.isRoot ? QStringLiteral("root") : QStringLiteral("folder %1").arg(parent.handle);
}

} // namespace

SqliteNodeCache::SqliteNodeCache(const std::string& dbPath)
{
    if (sqlite3_open(dbPath.c_str(), &mDb) != SQLITE_OK)
    {
        qCWarning(lcCache) << "failed to open node cache db:"
                           << (mDb ? sqlite3_errmsg(mDb) : "sqlite3_open returned no handle");
        if (mDb)
        {
            sqlite3_close(mDb);
            mDb = nullptr;
        }
        return;
    }

    mUsable = migrate();
}

SqliteNodeCache::~SqliteNodeCache()
{
    if (mDb)
        sqlite3_close(mDb);
}

bool SqliteNodeCache::migrate()
{
    char* errMsg = nullptr;
    if (sqlite3_exec(mDb, kCreateTableSql, nullptr, nullptr, &errMsg) != SQLITE_OK)
    {
        qCWarning(lcCache) << "node cache schema migration failed:" << errMsg;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

Result<std::vector<FileEntry>> SqliteNodeCache::loadChildren(const ParentKey& parent) const
{
    if (!mUsable)
        return Result<std::vector<FileEntry>>::fail("node cache unusable");

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(mDb, kSelectSql, -1, &stmt, nullptr) != SQLITE_OK)
        return Result<std::vector<FileEntry>>::fail(sqlite3_errmsg(mDb));

    sqlite3_bind_int(stmt, 1, parent.isRoot ? 1 : 0);
    sqlite3_bind_int64(stmt, 2, toSqlHandle(parent.isRoot ? 0 : parent.handle));

    std::vector<FileEntry> entries;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        FileEntry entry;
        entry.handle = fromSqlHandle(sqlite3_column_int64(stmt, 0));
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        entry.name = name ? reinterpret_cast<const char*>(name) : std::string();
        entry.sizeBytes = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 2));
        entry.isFolder = sqlite3_column_int(stmt, 3) != 0;
        entry.modificationTime = sqlite3_column_int64(stmt, 4);
        entry.hasThumbnail = sqlite3_column_int(stmt, 5) != 0;
        entries.push_back(std::move(entry));
    }

    const bool failed = rc != SQLITE_DONE;
    const std::string errMsg = failed ? sqlite3_errmsg(mDb) : std::string();
    sqlite3_finalize(stmt);

    if (failed)
        return Result<std::vector<FileEntry>>::fail(errMsg);
    // entries may legitimately be empty here, for both "never cached" and
    // "cached as an empty folder" -- INodeCache.h documents why that
    // ambiguity is resolved by the caller, not here. Only log the
    // non-empty case as a "cache hit": FolderNavigationService::
    // loadWithCache only calls onCacheHit (i.e. only actually uses this
    // result) when it's non-empty, so this line firing is a reliable proxy
    // for "the cache was shown to the user" without needing to eyeball the
    // UI for a flash of cached content.
    if (!entries.empty())
        qCInfo(lcCache) << "cache hit for" << describeParent(parent) << "-" << entries.size()
                        << "entries";
    return Result<std::vector<FileEntry>>::ok(std::move(entries));
}

Result<void> SqliteNodeCache::saveChildren(const ParentKey& parent,
                                           const std::vector<FileEntry>& entries)
{
    if (!mUsable)
        return Result<void>::fail("node cache unusable");

    // Logs and rolls back in one place: FolderNavigationService (the only
    // caller) deliberately swallows this Result<void> without logging --
    // src/core stays Qt-free, so lcCache is only ever touched from here.
    auto failAndRollback = [this](const std::string& errMsg) {
        qCWarning(lcCache) << "cache write-through failed:" << errMsg.c_str();
        sqlite3_exec(mDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return Result<void>::fail(errMsg);
    };

    if (sqlite3_exec(mDb, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        qCWarning(lcCache) << "cache write-through failed to begin transaction:"
                           << sqlite3_errmsg(mDb);
        return Result<void>::fail(sqlite3_errmsg(mDb));
    }

    sqlite3_stmt* deleteStmt = nullptr;
    if (sqlite3_prepare_v2(mDb, kDeleteSql, -1, &deleteStmt, nullptr) != SQLITE_OK)
        return failAndRollback(sqlite3_errmsg(mDb));

    sqlite3_bind_int(deleteStmt, 1, parent.isRoot ? 1 : 0);
    sqlite3_bind_int64(deleteStmt, 2, toSqlHandle(parent.isRoot ? 0 : parent.handle));
    const bool deleteFailed = sqlite3_step(deleteStmt) != SQLITE_DONE;
    const std::string deleteErrMsg = deleteFailed ? sqlite3_errmsg(mDb) : std::string();
    sqlite3_finalize(deleteStmt);
    if (deleteFailed)
        return failAndRollback(deleteErrMsg);

    sqlite3_stmt* insertStmt = nullptr;
    if (sqlite3_prepare_v2(mDb, kInsertSql, -1, &insertStmt, nullptr) != SQLITE_OK)
        return failAndRollback(sqlite3_errmsg(mDb));

    for (const FileEntry& entry : entries)
    {
        sqlite3_reset(insertStmt);
        sqlite3_clear_bindings(insertStmt);
        sqlite3_bind_int(insertStmt, 1, parent.isRoot ? 1 : 0);
        sqlite3_bind_int64(insertStmt, 2, toSqlHandle(parent.isRoot ? 0 : parent.handle));
        sqlite3_bind_int64(insertStmt, 3, toSqlHandle(entry.handle));
        sqlite3_bind_text(insertStmt, 4, entry.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insertStmt, 5, static_cast<sqlite3_int64>(entry.sizeBytes));
        sqlite3_bind_int(insertStmt, 6, entry.isFolder ? 1 : 0);
        sqlite3_bind_int64(insertStmt, 7, entry.modificationTime);
        sqlite3_bind_int(insertStmt, 8, entry.hasThumbnail ? 1 : 0);

        if (sqlite3_step(insertStmt) != SQLITE_DONE)
        {
            const std::string errMsg = sqlite3_errmsg(mDb);
            sqlite3_finalize(insertStmt);
            return failAndRollback(errMsg);
        }
    }
    sqlite3_finalize(insertStmt);

    if (sqlite3_exec(mDb, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return failAndRollback(sqlite3_errmsg(mDb));

    // FolderNavigationService::loadWithCache only calls saveChildren after a
    // successful network fetch, so this line firing is a reliable proxy for
    // "refreshed from the server and re-cached" -- fires even when the
    // fetch happened to return the same rows as before, since v1 always
    // does a full replace rather than diffing.
    qCInfo(lcCache) << "refreshed and cached" << entries.size() << "entries for"
                    << describeParent(parent);
    return Result<void>::ok();
}


Result<void> SqliteNodeCache::clearAll()
{
    if (!mUsable)
        return Result<void>::fail("node cache unusable");

    if (sqlite3_exec(mDb, kClearAllSql, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        qCWarning(lcCache) << "cache clearAll failed:" << sqlite3_errmsg(mDb);
        return Result<void>::fail(sqlite3_errmsg(mDb));
    }

    qCInfo(lcCache) << "cache cleared (logout)";
    return Result<void>::ok();
}
