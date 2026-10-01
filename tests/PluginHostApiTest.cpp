#include "qml/PluginHostApi.h"

#include "MockMegaClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
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
        mApi.call(method, params, [&reply](const PluginHostApi::Reply& r) { reply = r; });
        for (int i = 0; i < 20 && !reply; ++i)
            QCoreApplication::processEvents();
        EXPECT_TRUE(reply.has_value());
        return reply.value_or(PluginHostApi::Reply{});
    }

    std::shared_ptr<NiceMock<MockMegaClient>> mClient = std::make_shared<NiceMock<MockMegaClient>>();
    QObject mGuiContext;
    PluginHostApi mApi{mClient, &mGuiContext};
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

TEST_F(PluginHostApiTest, ItemRefCarriesParentAndIsNullForAMissingNode)
{
    NodeSnapshot root = node(1, "", true);
    root.hasParent = false;
    EXPECT_CALL(*mClient, getNodeSnapshot(1)).WillOnce(Return(Result<NodeSnapshot>::ok(root)));
    EXPECT_CALL(*mClient, getNodeSnapshot(99))
        .WillOnce(Return(Result<NodeSnapshot>::fail("gone", -9)));
    const std::optional<QJsonObject> ref = mApi.itemRef(1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_TRUE(ref->value(QStringLiteral("parent")).isNull());
    EXPECT_FALSE(mApi.itemRef(99).has_value());
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

    const PluginHostApi::Reply both = call(
        QStringLiteral("items.update"),
        {{QStringLiteral("handle"), QStringLiteral("h7")},
         {QStringLiteral("tags"),
          QJsonObject{{QStringLiteral("add"), QJsonArray{QStringLiteral("x")}},
                      {QStringLiteral("remove"), QJsonArray{QStringLiteral("x")}}}}});
    EXPECT_EQ(both.errorCode, -32602);
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
