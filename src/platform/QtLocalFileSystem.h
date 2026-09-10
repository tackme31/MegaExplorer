#pragma once
#include "core/ILocalFileSystem.h"

// The only ILocalFileSystem in production: QDir/QFileInfo/QSaveFile behind the
// Qt-free port src/core reaches the local disk through.
class QtLocalFileSystem : public ILocalFileSystem
{
public:
    std::unique_ptr<ILocalFileWriter> createFile(const std::string& path) override;
    std::optional<LocalEntry> entryFor(const std::string& path) const override;
    std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const override;
};
