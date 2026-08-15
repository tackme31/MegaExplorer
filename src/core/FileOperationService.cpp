#include "FileOperationService.h"

#include "MegaErrorCodes.h"

namespace
{
// Splits at the last dot, but only when there is a name in front of it, so a
// dotfile keeps its leading dot in the stem.
std::size_t extensionStart(const std::string& name)
{
    const std::size_t dot = name.rfind('.');
    return (dot == std::string::npos || dot == 0) ? name.size() : dot;
}
} // namespace

FileOperationService::FileOperationService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

bool FileOperationService::isValidName(const std::string& name)
{
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        return false;
    // Path separators would break DownloadService's local-path composition
    // downstream, so names this app creates are rejected here, at the entry.
    // Names that arrive already-made (a link import, another client's rename)
    // never pass through here, so the download path defends itself too --
    // DownloadService::safeLocalFileName.
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

void FileOperationService::rename(std::uint64_t handle,
                                  const std::string& newName,
                                  std::function<void(Result<void>)> onDone)
{
    if (!isValidName(newName))
    {
        onDone(Result<void>::fail("Invalid name: empty, or contains a path separator",
                                  MegaErrorCode::kEArgs));
        return;
    }

    mClient->renameNode(handle, newName, std::move(onDone));
}

std::string FileOperationService::uniqueCopyName(const std::string& name,
                                                 bool isFolder,
                                                 const std::set<std::string>& taken)
{
    if (taken.count(name) == 0)
        return name;

    const std::size_t split = isFolder ? name.size() : extensionStart(name);
    const std::string stem = name.substr(0, split);
    const std::string extension = name.substr(split);

    std::string candidate = stem + " - Copy" + extension;
    // Bounded rather than while(true): a folder pathological enough to exhaust
    // this is better served by a duplicate name than by a hung paste.
    for (int n = 2; taken.count(candidate) != 0 && n < 10000; ++n)
        candidate = stem + " - Copy (" + std::to_string(n) + ")" + extension;

    return candidate;
}

std::string FileOperationService::uniqueMoveName(const std::string& name,
                                                 bool isFolder,
                                                 const std::set<std::string>& taken)
{
    if (taken.count(name) == 0)
        return name;

    const std::size_t split = isFolder ? name.size() : extensionStart(name);
    const std::string stem = name.substr(0, split);
    const std::string extension = name.substr(split);

    std::string candidate;
    for (int n = 2; n < 10000; ++n)
    {
        candidate = stem + " (" + std::to_string(n) + ")" + extension;
        if (taken.count(candidate) == 0)
            break;
    }
    return candidate;
}

void FileOperationService::copy(std::uint64_t handle,
                                std::uint64_t newParentHandle,
                                bool newParentIsRoot,
                                const std::string& newName,
                                std::function<void(Result<void>)> onDone)
{
    if (!newName.empty() && !isValidName(newName))
    {
        onDone(Result<void>::fail("Invalid name: empty, or contains a path separator",
                                  MegaErrorCode::kEArgs));
        return;
    }

    Result<void> allowed = canCopy(handle, newParentHandle, newParentIsRoot);
    if (!allowed.success)
    {
        onDone(std::move(allowed));
        return;
    }

    mClient->copyNode(handle, newParentHandle, newParentIsRoot, newName, std::move(onDone));
}

void FileOperationService::createFolder(std::uint64_t parentHandle,
                                        bool parentIsRoot,
                                        const std::string& name,
                                        std::function<void(Result<void>)> onDone)
{
    if (!isValidName(name))
    {
        onDone(Result<void>::fail("Invalid name: empty, or contains a path separator",
                                  MegaErrorCode::kEArgs));
        return;
    }

    mClient->createFolder(parentHandle, parentIsRoot, name, std::move(onDone));
}

void FileOperationService::moveToRubbish(std::uint64_t handle,
                                         std::function<void(Result<void>)> onDone)
{
    mClient->moveToRubbish(handle, std::move(onDone));
}

void FileOperationService::removeNode(std::uint64_t handle,
                                      std::function<void(Result<void>)> onDone)
{
    mClient->removeNode(handle, std::move(onDone));
}

void FileOperationService::emptyRubbishBin(std::function<void(Result<void>)> onDone)
{
    mClient->cleanRubbishBin(std::move(onDone));
}

Result<RestoreTarget> FileOperationService::restoreTargetFor(std::uint64_t handle) const
{
    return mClient->getRestoreTarget(handle);
}

void FileOperationService::setFavourite(std::uint64_t handle,
                                        bool favourite,
                                        std::function<void(Result<void>)> onDone)
{
    mClient->setNodeFavourite(handle, favourite, std::move(onDone));
}

void FileOperationService::move(std::uint64_t handle,
                                std::uint64_t newParentHandle,
                                bool newParentIsRoot,
                                const std::string& newName,
                                std::function<void(Result<void>)> onDone)
{
    if (!newName.empty() && !isValidName(newName))
    {
        onDone(Result<void>::fail("Invalid name: empty, or contains a path separator",
                                  MegaErrorCode::kEArgs));
        return;
    }

    Result<void> allowed = canMove(handle, newParentHandle, newParentIsRoot);
    if (!allowed.success)
    {
        onDone(std::move(allowed));
        return;
    }

    mClient->moveNode(handle, newParentHandle, newParentIsRoot, newName, std::move(onDone));
}

Result<void> FileOperationService::canMove(std::uint64_t handle,
                                           std::uint64_t newParentHandle,
                                           bool newParentIsRoot) const
{
    return mClient->checkMove(handle, newParentHandle, newParentIsRoot);
}

Result<void> FileOperationService::canCopy(std::uint64_t handle,
                                           std::uint64_t newParentHandle,
                                           bool newParentIsRoot) const
{
    Result<void> allowed = mClient->checkMove(handle, newParentHandle, newParentIsRoot);
    // kEArgs here means "already in that folder", which a copy is entitled to do
    // -- it produces a "... - Copy" sibling. Every other code a move fails with
    // (gone, own descendant, read-only) is a refusal for a copy too.
    if (!allowed.success && allowed.errorCode == MegaErrorCode::kEArgs)
        return Result<void>::ok();
    return allowed;
}

Result<void> FileOperationService::canAddChildren(std::uint64_t parentHandle,
                                                  bool parentIsRoot) const
{
    return mClient->checkUpload(parentHandle, parentIsRoot);
}
