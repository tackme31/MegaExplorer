#include "FileOperationService.h"

#include "MegaErrorCodes.h"

FileOperationService::FileOperationService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

bool FileOperationService::isValidName(const std::string& name)
{
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        return false;
    // Path separators would break DownloadService's local-path composition
    // downstream, so they're rejected here rather than at download time.
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

void FileOperationService::rename(std::uint64_t handle,
                                  const std::string& newName,
                                  std::function<void(Result<void>)> onDone)
{
    if (!isValidName(newName))
    {
        onDone(Result<void>::fail("Invalid name: empty, or contains a path separator"));
        return;
    }

    mClient->renameNode(handle, newName, std::move(onDone));
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

void FileOperationService::move(std::uint64_t handle,
                                std::uint64_t newParentHandle,
                                bool newParentIsRoot,
                                std::function<void(Result<void>)> onDone)
{
    Result<void> allowed = canMove(handle, newParentHandle, newParentIsRoot);
    if (!allowed.success)
    {
        onDone(std::move(allowed));
        return;
    }

    mClient->moveNode(handle, newParentHandle, newParentIsRoot, std::move(onDone));
}

Result<void> FileOperationService::canMove(std::uint64_t handle,
                                           std::uint64_t newParentHandle,
                                           bool newParentIsRoot) const
{
    return mClient->checkMove(handle, newParentHandle, newParentIsRoot);
}
