#pragma once
#include "core/IMegaClient.h"

#include <gmock/gmock.h>

class MockMegaClient : public IMegaClient
{
public:
    MOCK_METHOD(void, login,
                (const std::string&, const std::string&, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void, fetchNodes, (std::function<void(Result<void>)>), (override));
    MOCK_METHOD(void, getRootChildren,
                (std::function<void(Result<std::vector<FileEntry>>)>), (override));
    MOCK_METHOD(void, getChildren,
                (std::uint64_t, std::function<void(Result<std::vector<FileEntry>>)>), (override));
    MOCK_METHOD(void, search,
                (std::uint64_t, bool, const std::string&,
                 std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
};
