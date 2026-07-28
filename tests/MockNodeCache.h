#pragma once
#include "core/INodeCache.h"

#include <gmock/gmock.h>

class MockNodeCache : public INodeCache
{
public:
    MOCK_METHOD(Result<std::vector<FileEntry>>,
                loadChildren,
                (const ParentKey&),
                (const, override));
    MOCK_METHOD(Result<void>,
                saveChildren,
                (const ParentKey&, const std::vector<FileEntry>&),
                (override));
};
