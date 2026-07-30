#pragma once
#include "core/IPinnedFolderStore.h"

#include <gmock/gmock.h>

class MockPinnedFolderStore : public IPinnedFolderStore
{
public:
    MOCK_METHOD(Result<std::vector<PinnedFolder>>, load, (), (const, override));
    MOCK_METHOD(Result<void>, save, (const std::vector<PinnedFolder>&), (override));
};
