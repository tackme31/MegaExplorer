#pragma once
#include "core/ILocalFileSystem.h"

// The only ILocalFileSystem in production: QDir/QFileInfo behind the Qt-free port
// src/core reads local directories through.
class QtLocalFileSystem : public ILocalFileSystem
{
public:
    std::optional<LocalEntry> entryFor(const std::string& path) const override;
    std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const override;
};
