#include "qml/PluginHostApi.h"

#include "MockMegaClient.h"

#include <QJsonArray>
#include <QJsonObject>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

    std::shared_ptr<NiceMock<MockMegaClient>> mClient =
        std::make_shared<NiceMock<MockMegaClient>>();
    PluginHostApi mApi{mClient};
};
} // namespace

TEST_F(PluginHostApiTest, ItemsGetReturnsTheFullItem)
{
    NodeSnapshot cat = node(7, "cat.jpg", false);
    cat.sizeBytes = 123;
    cat.tags = {"pet"};
    EXPECT_CALL(*mClient, getNodeSnapshot(7)).WillOnce(Return(Result<NodeSnapshot>::ok(cat)));

    const PluginHostApi::Reply reply =
        mApi.call(QStringLiteral("items.get"), {{QStringLiteral("handle"), QStringLiteral("h7")}});

    ASSERT_FALSE(reply.errorCode.has_value());
    const QJsonObject item = reply.result.toObject().value(QStringLiteral("item")).toObject();
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
        mApi.call(QStringLiteral("items.get"), {{QStringLiteral("handle"), QStringLiteral("zz")}});
    EXPECT_EQ(reply.errorCode, -32602);
}

TEST_F(PluginHostApiTest, ItemsGetReportsAMissingNode)
{
    const PluginHostApi::Reply reply =
        mApi.call(QStringLiteral("items.get"), {{QStringLiteral("handle"), QStringLiteral("h9")}});
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
        mApi.call(QStringLiteral("items.children"),
                  {{QStringLiteral("handle"), QStringLiteral("h1")}, {QStringLiteral("limit"), 2}});
    ASSERT_FALSE(first.errorCode.has_value());
    const QJsonObject page1 = first.result.toObject();
    EXPECT_EQ(page1.value(QStringLiteral("items")).toArray().size(), 2);
    const QString cursor = page1.value(QStringLiteral("nextCursor")).toString();
    ASSERT_FALSE(cursor.isEmpty());

    const PluginHostApi::Reply second = mApi.call(QStringLiteral("items.children"),
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
    const PluginHostApi::Reply reply = mApi.call(
        QStringLiteral("items.children"), {{QStringLiteral("handle"), QStringLiteral("h7")}});
    EXPECT_EQ(reply.errorCode, -32602);
}

TEST_F(PluginHostApiTest, UnknownMethodIsMethodNotFound)
{
    EXPECT_EQ(mApi.call(QStringLiteral("items.nope"), {}).errorCode, -32601);
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
