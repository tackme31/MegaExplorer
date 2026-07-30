#pragma once
#include "core/IMegaClient.h"

#include <gmock/gmock.h>

class MockMegaClient : public IMegaClient
{
public:
    MOCK_METHOD(void,
                login,
                (const std::string&, const std::string&, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void, fetchNodes, (std::function<void(Result<void>)>), (override));
    MOCK_METHOD(void,
                loginWithSession,
                (const std::string&, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void,
                multiFactorAuthLogin,
                (const std::string&,
                 const std::string&,
                 const std::string&,
                 std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void, logout, (std::function<void(Result<void>)>), (override));
    MOCK_METHOD(Result<std::string>, currentSessionToken, (), (const, override));
    MOCK_METHOD(void,
                getRootChildren,
                (SortOrder, std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                getChildren,
                (std::uint64_t, SortOrder, std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                search,
                (std::uint64_t,
                 bool,
                 const std::string&,
                 SortOrder,
                 std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                download,
                (std::uint64_t,
                 const std::string&,
                 std::function<void(std::uint64_t, std::uint64_t)>,
                 std::function<void(Result<DownloadOutcome>)>),
                (override));
    MOCK_METHOD(void,
                getThumbnail,
                (std::uint64_t, const std::string&, std::function<void(Result<std::string>)>),
                (override));
    MOCK_METHOD(void,
                getPath,
                (std::uint64_t, bool, std::function<void(Result<std::vector<PathSegment>>)>),
                (override));
    MOCK_METHOD(void,
                getNodeInfo,
                (std::uint64_t, std::function<void(Result<NodeInfo>)>),
                (override));
};
