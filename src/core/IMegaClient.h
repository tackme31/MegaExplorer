#pragma once
#include "FileEntry.h"
#include "Result.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Callbacks may be invoked from a background thread, not the caller's thread.
class IMegaClient
{
public:
    virtual ~IMegaClient() = default;

    virtual void login(const std::string& email,
                        const std::string& password,
                        std::function<void(Result<void>)> onDone) = 0;

    // Must be called after a successful login(), before getRootChildren().
    virtual void fetchNodes(std::function<void(Result<void>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency.
    virtual void getRootChildren(
        std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;

    // Must be called after a successful fetchNodes(). Synchronous under the
    // hood, but kept callback-shaped for interface consistency.
    virtual void getChildren(std::uint64_t handle,
                              std::function<void(Result<std::vector<FileEntry>>)> onDone) = 0;
};
