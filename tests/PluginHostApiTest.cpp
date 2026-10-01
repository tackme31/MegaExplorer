#include "qml/PluginHostApi.h"

#include "MockMegaClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <tuple>

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace
{
NodeSnapshot node(std::uint64_t handle, const std::string& name, bool isFolder)
{
    NodeSnapshot n;
    n.handle = handle;
    n.name = name;
    n.isFolder = isFolder;
    n.hasParent = true;
    n.parentHandle = 1;
    n.path = "/" + name;
    return n;
}

class PluginHostApiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // "h<n>" <-> n, so the tests read without a real base64 codec.
        ON_CALL(*mClient, handleToBase64(_)).WillByDefault([](std::uint64_t h) {
            return "h" + std::to_string(h);
        });
        ON_CALL(*mClient, base64ToHandle(_)).WillByDefault([](const std::string& s) {
            if (s.size() < 2 || s[0] != 'h')
                return Result<std::uint64_t>::fail("bad", -2);
            return Result<std::uint64_t>::ok(std::stoull(s.substr(1)));
        });
        ON_CALL(*mClient, getNodeSnapshot(_))
            .WillByDefault(Return(Result<NodeSnapshot>::fail("gone", -9)));
    }

    // Drains the posted SDK answers, as the GUI thread's event loop would.
    PluginHostApi::Reply call(const QString& method, const QJsonObject& params)
    {
        std::optional<PluginHostApi::Reply> reply;
        mRun.tempDir = mTempDir.path();
        mApi.call(method, params, mRun, [&reply](const PluginHostApi::Reply& r) { reply = r; });
        for (int i = 0; i < 20 && !reply; ++i)
            QCoreApplication::processEvents();
        EXPECT_TRUE(reply.has_value());
        return reply.value_or(PluginHostApi::Reply{});
    }

    std::shared_ptr<NiceMock<MockMegaClient>> mClient = std::make_shared<NiceMock<MockMegaClient>>();
    QObject mGuiContext;
    PluginHostApi mApi{mClient, &mGuiContext};
    QTemporaryDir mTempDir;
    PluginHostApi::RunState mRun;
};
} // namespace

TEST_F(PluginHostApiTest, ItemsGetReturnsTheFullItem)
{
    NodeSnapshot cat = node(7, "cat.jpg", false);
    cat.sizeBytes = 123;
    cat.tags = {"pet"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7)).WillOnce(Return(Result<NodeSnapshot>::ok(cat)));

    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.get"), {{QStringLiteral("handles"), QJsonArray{QStringLiteral("h7")}}});

    ASSERT_FALSE(reply.errorCode.has_value());
    const QJsonObject item = reply.result.toObject().value(QStringLiteral("items")).toArray().at(0).toObject();
    EXPECT_EQ(item.value(QStringLiteral("handle")).toString(), QStringLiteral("h7"));
    EXPECT_EQ(item.value(QStringLiteral("type")).toString(), QStringLiteral("file"));
    EXPECT_EQ(item.value(QStringLiteral("parent")).toString(), QStringLiteral("h1"));
    EXPECT_EQ(item.value(QStringLiteral("size")).toInt(), 123);
    EXPECT_EQ(item.value(QStringLiteral("path")).toString(), QStringLiteral("/cat.jpg"));
    EXPECT_EQ(item.value(QStringLiteral("tags")).toArray().at(0).toString(), QStringLiteral("pet"));
}

TEST_F(PluginHostApiTest, ItemsGetRejectsAMalformedHandle)
{
    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.get"), {{QStringLiteral("handles"), QJsonArray{QStringLiteral("zz")}}});
    EXPECT_EQ(reply.errorCode, -32602);
}

TEST_F(PluginHostApiTest, ItemsGetReportsAMissingNode)
{
    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.get"), {{QStringLiteral("handles"), QJsonArray{QStringLiteral("h9")}}});
    EXPECT_EQ(reply.errorCode, PluginHostApi::kItemNotFound);
}

TEST_F(PluginHostApiTest, ItemsChildrenPagesWithACursor)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(1))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(1, "dir", true))));
    std::vector<NodeSnapshot> children{
        node(10, "a", false), node(11, "b", false), node(12, "c", false)};
    EXPECT_CALL(*mClient, getChildSnapshots(1))
        .WillRepeatedly(Return(Result<std::vector<NodeSnapshot>>::ok(children)));

    const PluginHostApi::Reply first =
        call(QStringLiteral("items.children"),
                  {{QStringLiteral("handle"), QStringLiteral("h1")}, {QStringLiteral("limit"), 2}});
    ASSERT_FALSE(first.errorCode.has_value());
    const QJsonObject page1 = first.result.toObject();
    EXPECT_EQ(page1.value(QStringLiteral("items")).toArray().size(), 2);
    const QString cursor = page1.value(QStringLiteral("nextCursor")).toString();
    ASSERT_FALSE(cursor.isEmpty());

    const PluginHostApi::Reply second = call(QStringLiteral("items.children"),
                                                  {{QStringLiteral("handle"), QStringLiteral("h1")},
                                                   {QStringLiteral("limit"), 2},
                                                   {QStringLiteral("cursor"), cursor}});
    const QJsonObject page2 = second.result.toObject();
    const QJsonArray items = page2.value(QStringLiteral("items")).toArray();
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("c"));
    EXPECT_TRUE(page2.value(QStringLiteral("nextCursor")).isNull());
}

TEST_F(PluginHostApiTest, ItemsChildrenRejectsAFile)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(7, "cat.jpg", false))));
    EXPECT_CALL(*mClient, getChildSnapshots(7))
        .WillOnce(Return(Result<std::vector<NodeSnapshot>>::fail("Not a folder", -2)));
    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.children"), {{QStringLiteral("handle"), QStringLiteral("h7")}});
    EXPECT_EQ(reply.errorCode, -32602);
}

namespace
{
QStringList names(const PluginHostApi::Reply& reply)
{
    QStringList result;
    for (const QJsonValue item : reply.result.toObject().value(QStringLiteral("items")).toArray())
        result.append(item.toObject().value(QStringLiteral("name")).toString());
    return result;
}
} // namespace

TEST_F(PluginHostApiTest, ItemsDescendantsListsDepthFirstFoldersBeforeTheirContents)
{
    // 1 { a, s { b, t { c } }, z }
    std::map<std::uint64_t, NodeSnapshot> nodes{{1, node(1, "root", true)},  {10, node(10, "a", false)},
                                                {11, node(11, "s", true)},   {12, node(12, "b", false)},
                                                {13, node(13, "t", true)},   {15, node(15, "c", false)},
                                                {14, node(14, "z", false)}};
    std::map<std::uint64_t, std::vector<std::uint64_t>> tree{{1, {10, 11, 14}}, {11, {12, 13}}, {13, {15}}};
    ON_CALL(*mClient, getNodeSnapshot(_)).WillByDefault([&nodes](std::uint64_t h) {
        const auto it = nodes.find(h);
        return it == nodes.end() ? Result<NodeSnapshot>::fail("gone", -9) : Result<NodeSnapshot>::ok(it->second);
    });
    ON_CALL(*mClient, getChildSnapshots(_)).WillByDefault([&nodes, &tree](std::uint64_t h) {
        if (!nodes.count(h) || !nodes.at(h).isFolder)
            return Result<std::vector<NodeSnapshot>>::fail("Not a folder", -2);
        std::vector<NodeSnapshot> children;
        for (std::uint64_t child : tree[h])
            children.push_back(nodes.at(child));
        return Result<std::vector<NodeSnapshot>>::ok(children);
    });

    const PluginHostApi::Reply all =
        call(QStringLiteral("items.descendants"), {{QStringLiteral("handle"), QStringLiteral("h1")}});
    ASSERT_FALSE(all.errorCode.has_value());
    EXPECT_EQ(names(all), (QStringList{"a", "s", "b", "t", "c", "z"}));
    EXPECT_TRUE(all.result.toObject().value(QStringLiteral("nextCursor")).isNull());

    const QJsonObject filesParams{{QStringLiteral("handle"), QStringLiteral("h1")},
                                  {QStringLiteral("type"), QStringLiteral("file")},
                                  {QStringLiteral("limit"), 2}};
    const PluginHostApi::Reply first = call(QStringLiteral("items.descendants"), filesParams);
    EXPECT_EQ(names(first), (QStringList{"a", "b"}));
    const QString cursor = first.result.toObject().value(QStringLiteral("nextCursor")).toString();
    ASSERT_FALSE(cursor.isEmpty());

    // The listing was fixed by the first page: a file added later is not in it, and
    // one deleted since is skipped, so this page comes up one short.
    nodes.emplace(16, node(16, "new", false));
    tree[1].push_back(16);
    nodes.erase(15);
    QJsonObject nextParams = filesParams;
    nextParams.insert(QStringLiteral("cursor"), cursor);
    const PluginHostApi::Reply second = call(QStringLiteral("items.descendants"), nextParams);
    ASSERT_FALSE(second.errorCode.has_value());
    EXPECT_EQ(names(second), (QStringList{"z"}));
    EXPECT_TRUE(second.result.toObject().value(QStringLiteral("nextCursor")).isNull());
}

TEST_F(PluginHostApiTest, ItemsDescendantsRejectsAFileAndAForeignCursor)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(7, "cat.jpg", false))));
    EXPECT_CALL(*mClient, getChildSnapshots(7))
        .WillRepeatedly(Return(Result<std::vector<NodeSnapshot>>::fail("Not a folder", -2)));
    EXPECT_EQ(call(QStringLiteral("items.descendants"), {{QStringLiteral("handle"), QStringLiteral("h7")}})
                  .errorCode,
              -32602);
    // No listing was ever started in this run, so no cursor can be valid.
    EXPECT_EQ(call(QStringLiteral("items.descendants"),
                   {{QStringLiteral("handle"), QStringLiteral("h7")}, {QStringLiteral("cursor"), QStringLiteral("0:0")}})
                  .errorCode,
              -32602);
    EXPECT_EQ(call(QStringLiteral("items.descendants"),
                   {{QStringLiteral("handle"), QStringLiteral("h7")}, {QStringLiteral("cursor"), QStringLiteral("x")}})
                  .errorCode,
              -32602);
}

TEST_F(PluginHostApiTest, ItemsGetKeepsTheOrderOfHandles)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(3))
        .WillOnce(Return(Result<NodeSnapshot>::ok(node(3, "c", false))));
    EXPECT_CALL(*mClient, getNodeSnapshot(2))
        .WillOnce(Return(Result<NodeSnapshot>::ok(node(2, "b", false))));
    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.get"),
        {{QStringLiteral("handles"), QJsonArray{QStringLiteral("h3"), QStringLiteral("h2")}}});
    const QJsonArray items = reply.result.toObject().value(QStringLiteral("items")).toArray();
    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("c"));
    EXPECT_EQ(items.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("b"));
}

TEST_F(PluginHostApiTest, ItemsChildrenFiltersByType)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(1))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(1, "dir", true))));
    std::vector<NodeSnapshot> children{node(10, "a", false), node(11, "sub", true), node(12, "c", false)};
    EXPECT_CALL(*mClient, getChildSnapshots(1))
        .WillRepeatedly(Return(Result<std::vector<NodeSnapshot>>::ok(children)));

    const PluginHostApi::Reply folders = call(
        QStringLiteral("items.children"),
        {{QStringLiteral("handle"), QStringLiteral("h1")}, {QStringLiteral("type"), QStringLiteral("folder")}});
    const QJsonArray items = folders.result.toObject().value(QStringLiteral("items")).toArray();
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("sub"));

    const PluginHostApi::Reply bad = call(
        QStringLiteral("items.children"),
        {{QStringLiteral("handle"), QStringLiteral("h1")}, {QStringLiteral("type"), QStringLiteral("link")}});
    EXPECT_EQ(bad.errorCode, -32602);
}

TEST_F(PluginHostApiTest, UnknownMethodIsMethodNotFound)
{
    EXPECT_EQ(call(QStringLiteral("items.nope"), {}).errorCode, -32601);
}

TEST_F(PluginHostApiTest, ContextItemIsAFullItemAndIsNullForAMissingNode)
{
    NodeSnapshot root = node(1, "", true);
    root.hasParent = false;
    NodeSnapshot file = node(2, "a.jpg", false);
    file.tags = {"wd:1girl"};
    file.description = "d";
    EXPECT_CALL(*mClient, getNodeSnapshot(1)).WillOnce(Return(Result<NodeSnapshot>::ok(root)));
    EXPECT_CALL(*mClient, getNodeSnapshot(2)).WillRepeatedly(Return(Result<NodeSnapshot>::ok(file)));
    EXPECT_CALL(*mClient, getNodeSnapshot(99))
        .WillOnce(Return(Result<NodeSnapshot>::fail("gone", -9)));

    const std::optional<QJsonObject> rootItem = mApi.item(1);
    ASSERT_TRUE(rootItem.has_value());
    EXPECT_TRUE(rootItem->value(QStringLiteral("parent")).isNull());

    // The same shape items.get returns, tags and description included.
    const std::optional<QJsonObject> fileItem = mApi.item(2);
    ASSERT_TRUE(fileItem.has_value());
    EXPECT_EQ(fileItem->value(QStringLiteral("tags")).toArray(), QJsonArray{QStringLiteral("wd:1girl")});
    EXPECT_EQ(fileItem->value(QStringLiteral("description")).toString(), QStringLiteral("d"));
    EXPECT_TRUE(fileItem->contains(QStringLiteral("size")));
    EXPECT_EQ(*fileItem, call(QStringLiteral("items.get"),
                              {{QStringLiteral("handles"), QJsonArray{QStringLiteral("h2")}}})
                             .result.toObject()
                             .value(QStringLiteral("items"))
                             .toArray()
                             .at(0)
                             .toObject());

    EXPECT_FALSE(mApi.item(99).has_value());
}

// ---- items.update -------------------------------------------------------

namespace
{
auto succeed()
{
    return [](auto&&... args) {
        auto onDone = std::get<sizeof...(args) - 1>(std::forward_as_tuple(args...));
        onDone(Result<void>::ok());
    };
}
} // namespace

TEST_F(PluginHostApiTest, ItemsUpdateSendsOnlyRealChangesAndReturnsTheNewItem)
{
    NodeSnapshot before = node(7, "a.jpg", false);
    before.tags = {"keep", "old"};
    NodeSnapshot after = before;
    after.name = "b.jpg";
    after.tags = {"keep", "new"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillOnce(Return(Result<NodeSnapshot>::ok(before)))
        .WillOnce(Return(Result<NodeSnapshot>::ok(after)));
    EXPECT_CALL(*mClient, renameNode(7, "b.jpg", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, addNodeTag(7, "new", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, removeNodeTag(7, "old", _)).WillOnce(succeed());
    // "keep" is already there and "gone" never was: neither reaches the SDK.
    EXPECT_CALL(*mClient, addNodeTag(7, "keep", _)).Times(0);
    EXPECT_CALL(*mClient, removeNodeTag(7, "gone", _)).Times(0);
    EXPECT_CALL(*mClient, setNodeFavourite(_, _, _)).Times(0);

    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("name"), QStringLiteral("b.jpg")},
         {QStringLiteral("favourite"), false},
         {QStringLiteral("tags"),
          QJsonObject{{QStringLiteral("add"), QJsonArray{QStringLiteral("keep"), QStringLiteral("new")}},
                      {QStringLiteral("remove"), QJsonArray{QStringLiteral("old"), QStringLiteral("gone")}}}}});

    ASSERT_FALSE(reply.errorCode.has_value());
    EXPECT_TRUE(reply.mutated);
    const QJsonObject item = reply.result.toObject().value(QStringLiteral("item")).toObject();
    EXPECT_EQ(item.value(QStringLiteral("name")).toString(), QStringLiteral("b.jpg"));
}

TEST_F(PluginHostApiTest, ItemsUpdateRemovesTagsBeforeAddingThem)
{
    // A replacement on an item at the 10-tag limit only fits if the old tag goes first.
    NodeSnapshot before = node(7, "a.jpg", false);
    before.tags = {"wd:old"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7)).WillRepeatedly(Return(Result<NodeSnapshot>::ok(before)));
    {
        testing::InSequence order;
        EXPECT_CALL(*mClient, removeNodeTag(7, "wd:old", _)).WillOnce(succeed());
        EXPECT_CALL(*mClient, addNodeTag(7, "wd:new", _)).WillOnce(succeed());
    }

    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("tags"),
          QJsonObject{{QStringLiteral("add"), QJsonArray{QStringLiteral("wd:new")}},
                      {QStringLiteral("remove"), QJsonArray{QStringLiteral("wd:old")}}}}});

    ASSERT_FALSE(reply.errorCode.has_value());
}

TEST_F(PluginHostApiTest, ItemsUpdateStopsAtTheFirstFailureAndSaysItChangedSomething)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(7, "a.jpg", false))));
    EXPECT_CALL(*mClient, renameNode(7, "b.jpg", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, setNodeDescription(7, "d", _))
        .WillOnce([](std::uint64_t, const std::string&, std::function<void(Result<void>)> onDone) {
            onDone(Result<void>::fail("too long", -2));
        });
    EXPECT_CALL(*mClient, setNodeFavourite(_, _, _)).Times(0);

    const PluginHostApi::Reply reply = call(QStringLiteral("items.update"),
                                            {{QStringLiteral("handle"), QStringLiteral("h7")},
                                             {QStringLiteral("name"), QStringLiteral("b.jpg")},
                                             {QStringLiteral("description"), QStringLiteral("d")},
                                             {QStringLiteral("favourite"), true}});
    EXPECT_EQ(reply.errorCode, PluginHostApi::kMegaError);
    EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiTest, ItemsUpdateRejectsBadParamsBeforeSendingAnything)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(7, "a.jpg", false))));
    EXPECT_CALL(*mClient, renameNode(_, _, _)).Times(0);

    const PluginHostApi::Reply comma = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("name"), QStringLiteral("b.jpg")},
         {QStringLiteral("tags"), QJsonObject{{QStringLiteral("add"), QJsonArray{QStringLiteral("a,b")}}}}});
    EXPECT_EQ(comma.errorCode, -32602);
    EXPECT_FALSE(comma.mutated);
}

TEST_F(PluginHostApiTest, ItemsUpdateKeepsATagThatIsBothRemovedAndAdded)
{
    // A retag: drop every old plugin tag, add every new one; only the difference is sent.
    NodeSnapshot before = node(7, "a.jpg", false);
    before.tags = {"mine", "wd:1girl smile", "rating:general"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7)).WillRepeatedly(Return(Result<NodeSnapshot>::ok(before)));
    EXPECT_CALL(*mClient, removeNodeTag(7, "rating:general", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, addNodeTag(7, "rating:sensitive", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, removeNodeTag(7, "wd:1girl smile", _)).Times(0);
    EXPECT_CALL(*mClient, addNodeTag(7, "wd:1girl smile", _)).Times(0);
    EXPECT_CALL(*mClient, removeNodeTag(7, "mine", _)).Times(0);

    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("tags"),
          QJsonObject{{QStringLiteral("add"),
                       QJsonArray{QStringLiteral("wd:1girl smile"), QStringLiteral("rating:sensitive")}},
                      {QStringLiteral("remove"),
                       QJsonArray{QStringLiteral("wd:1girl smile"), QStringLiteral("rating:general")}}}}});

    ASSERT_FALSE(reply.errorCode.has_value());
    EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiTest, ItemsUpdateMatchesTagsIgnoringCaseButNotAccents)
{
    NodeSnapshot before = node(7, "a.jpg", false);
    before.tags = {"Long_Hair", "Old", "cafe"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7)).WillRepeatedly(Return(Result<NodeSnapshot>::ok(before)));
    // The SDK would answer EEXIST to adding "long_hair" next to "Long_Hair".
    EXPECT_CALL(*mClient, addNodeTag(7, "long_hair", _)).Times(0);
    // A remove names the tag as the item stores it, whatever case the plugin used.
    EXPECT_CALL(*mClient, removeNodeTag(7, "Old", _)).WillOnce(succeed());
    // The SDK compares add/remove with accents, so "cafe" with an accent is a different tag.
    EXPECT_CALL(*mClient, addNodeTag(7, "caf\xC3\xA9", _)).WillOnce(succeed());
    EXPECT_CALL(*mClient, addNodeTag(7, "CAF\xC3\x89", _)).Times(0);

    const PluginHostApi::Reply reply = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("tags"),
          QJsonObject{{QStringLiteral("add"),
                       QJsonArray{QStringLiteral("long_hair"), QString::fromUtf8("caf\xC3\xA9"), QString::fromUtf8("CAF\xC3\x89")}},
                      {QStringLiteral("remove"), QJsonArray{QStringLiteral("OLD")}}}}});

    ASSERT_FALSE(reply.errorCode.has_value());
    EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiTest, ItemsUpdateWithNothingToChangeIsANoOp)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillRepeatedly(Return(Result<NodeSnapshot>::ok(node(7, "a.jpg", false))));
    const PluginHostApi::Reply reply = call(QStringLiteral("items.update"),
                                            {{QStringLiteral("handle"), QStringLiteral("h7")},
                                             {QStringLiteral("name"), QStringLiteral("a.jpg")}});
    ASSERT_FALSE(reply.errorCode.has_value());
    EXPECT_FALSE(reply.mutated);
}

TEST_F(PluginHostApiTest, ItemsFetchPreviewSavesIntoTheRunsTempDir)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(26))
        .WillOnce(Return(Result<NodeSnapshot>::ok(node(26, "cat.jpg", false))));
    std::string requestedPath;
    EXPECT_CALL(*mClient, getPreview(26, _, _))
        .WillOnce([&requestedPath](std::uint64_t,
                                   const std::string& path,
                                   std::function<void(Result<std::string>)> onDone) {
            requestedPath = path;
            onDone(Result<std::string>::ok(path));
        });

    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.fetchPreview"), {{QStringLiteral("handle"), QStringLiteral("h26")}});

    ASSERT_FALSE(reply.errorCode.has_value());
    const QString path = reply.result.toObject().value(QStringLiteral("path")).toString();
    EXPECT_EQ(path.toStdString(), requestedPath);
    EXPECT_EQ(QDir::fromNativeSeparators(path), QDir(mTempDir.path()).filePath(QStringLiteral("1a.jpg")));
    EXPECT_FALSE(reply.mutated);
}

TEST_F(PluginHostApiTest, ItemsFetchPreviewReportsAMissingPreviewAsNotFound)
{
    EXPECT_CALL(*mClient, getNodeSnapshot(7))
        .WillOnce(Return(Result<NodeSnapshot>::ok(node(7, "a.txt", false))));
    EXPECT_CALL(*mClient, getPreview(7, _, _))
        .WillOnce([](std::uint64_t, const std::string&, std::function<void(Result<std::string>)> onDone) {
            onDone(Result<std::string>::fail("not found", -9));
        });

    EXPECT_EQ(call(QStringLiteral("items.fetchPreview"), {{QStringLiteral("handle"), QStringLiteral("h7")}})
                  .errorCode,
              PluginHostApi::kItemNotFound);
}

TEST_F(PluginHostApiTest, ItemsFetchPreviewRejectsFoldersAndMissingItemsWithoutFetching)
{
    ON_CALL(*mClient, getNodeSnapshot(3))
        .WillByDefault(Return(Result<NodeSnapshot>::ok(node(3, "dir", true))));
    EXPECT_CALL(*mClient, getPreview(_, _, _)).Times(0);

    EXPECT_EQ(call(QStringLiteral("items.fetchPreview"), {{QStringLiteral("handle"), QStringLiteral("h3")}})
                  .errorCode,
              -32602);
    EXPECT_EQ(call(QStringLiteral("items.fetchPreview"), {{QStringLiteral("handle"), QStringLiteral("h4")}})
                  .errorCode,
              PluginHostApi::kItemNotFound);
}
