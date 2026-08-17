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
    MOCK_METHOD(void,
                fetchNodes,
                (std::function<void(std::uint64_t, std::uint64_t)>,
                 std::function<void(Result<void>)>),
                (override));
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
    MOCK_METHOD(Result<std::uint64_t>, currentUserHandle, (), (const, override));
    MOCK_METHOD(void, syncPendingChanges, (std::function<void(Result<void>)>), (override));
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
                 const SearchFilter&,
                 SortOrder,
                 std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                listFavourites,
                (SortOrder,
                 const std::string&,
                 std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                listRecent,
                (SortOrder,
                 const std::string&,
                 std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(void,
                getRubbishChildren,
                (SortOrder, std::function<void(Result<std::vector<FileEntry>>)>),
                (override));
    MOCK_METHOD(Result<RestoreTarget>, getRestoreTarget, (std::uint64_t), (const, override));
    MOCK_METHOD(void, removeNode, (std::uint64_t, std::function<void(Result<void>)>), (override));
    MOCK_METHOD(void, cleanRubbishBin, (std::function<void(Result<void>)>), (override));
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
                getPreview,
                (std::uint64_t, const std::string&, std::function<void(Result<std::string>)>),
                (override));
    MOCK_METHOD(void,
                readFileContent,
                (std::uint64_t, std::uint64_t, std::function<void(Result<std::vector<char>>)>),
                (override));
    MOCK_METHOD(void,
                getPath,
                (std::uint64_t, bool, std::function<void(Result<std::vector<PathSegment>>)>),
                (override));
    MOCK_METHOD(void,
                getNodeInfo,
                (std::uint64_t, std::function<void(Result<NodeInfo>)>),
                (override));
    MOCK_METHOD(void,
                renameNode,
                (std::uint64_t, const std::string&, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void,
                moveToRubbish,
                (std::uint64_t, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(
        void,
        moveNode,
        (std::uint64_t, std::uint64_t, bool, const std::string&, std::function<void(Result<void>)>),
        (override));
    MOCK_METHOD(
        void,
        copyNode,
        (std::uint64_t, std::uint64_t, bool, const std::string&, std::function<void(Result<void>)>),
        (override));
    MOCK_METHOD(void,
                createFolder,
                (std::uint64_t, bool, const std::string&, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(void,
                setNodeFavourite,
                (std::uint64_t, bool, std::function<void(Result<void>)>),
                (override));
    MOCK_METHOD(Result<void>, checkMove, (std::uint64_t, std::uint64_t, bool), (const, override));
    MOCK_METHOD(void,
                upload,
                (const std::string&,
                 std::uint64_t,
                 bool,
                 std::function<void(std::uint64_t, std::uint64_t)>,
                 std::function<void(Result<UploadOutcome>)>),
                (override));
    MOCK_METHOD(void, cancelDownload, (), (override));
    MOCK_METHOD(void, cancelUpload, (), (override));
    MOCK_METHOD(Result<void>, checkUpload, (std::uint64_t, bool), (const, override));
    MOCK_METHOD(Result<std::vector<FileEntry>>,
                findChildFiles,
                (std::uint64_t, bool, const std::vector<std::string>&),
                (const, override));
    MOCK_METHOD(Result<std::vector<FileEntry>>,
                findChildFolders,
                (std::uint64_t, bool, const std::vector<std::string>&),
                (const, override));
    MOCK_METHOD(Result<bool>,
                siblingNameTaken,
                (std::uint64_t, const std::string&),
                (const, override));
    MOCK_METHOD(Result<bool>, hasSubfolders, (std::uint64_t, bool), (const, override));
    MOCK_METHOD(Result<std::uint64_t>, subtreeSize, (std::uint64_t, bool), (const, override));
    MOCK_METHOD(Result<AccountIdentity>, currentAccountIdentity, (), (const, override));
    MOCK_METHOD(void,
                getMyAvatar,
                (const std::string&, std::function<void(Result<std::string>)>),
                (override));
    MOCK_METHOD(void,
                getMyUserAttribute,
                (UserAttribute, std::function<void(Result<std::string>)>),
                (override));
    MOCK_METHOD(void, getFileVersioningEnabled, (std::function<void(Result<bool>)>), (override));
    MOCK_METHOD(void, getAccountInfo, (std::function<void(Result<AccountInfo>)>), (override));
};
