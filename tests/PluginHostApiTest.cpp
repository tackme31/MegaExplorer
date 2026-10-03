#include "qml/PluginHostApi.h"
#include "qml/PluginManifest.h"

#include "MockMegaClient.h"
#include "core/MegaErrorCodes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
        mRun.permissions = {QStringLiteral("items.read"), QStringLiteral("items.write"), QStringLiteral("items.edit"),
                            QStringLiteral("content.read"), QStringLiteral("content.download")};
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

TEST_F(PluginHostApiTest, EveryHostMethodNeedsAKnownPermission)
{
    for (const char* method :
         {"items.get", "items.children", "items.descendants", "items.update", "items.fetchPreview", "items.fetchFile",
          "items.readRange", "items.upload", "items.createFolder", "transfers.download"})
    {
        const std::optional<QString> permission = PluginHostApi::requiredPermission(QString::fromLatin1(method));
        ASSERT_TRUE(permission.has_value()) << method;
        EXPECT_TRUE(isKnownPluginPermission(*permission)) << method;
    }
    EXPECT_FALSE(PluginHostApi::requiredPermission(QStringLiteral("items.nothing")).has_value());
}

TEST_F(PluginHostApiTest, AnUndeclaredPermissionIsRefusedAndRecordedOnce)
{
    mRun.permissions = {QStringLiteral("items.read")};
    EXPECT_CALL(*mClient, getNodeSnapshot(_)).Times(0);

    for (int i = 0; i < 3; ++i)
    {
        const PluginHostApi::Reply reply =
            call(QStringLiteral("items.fetchPreview"), {{QStringLiteral("handle"), QStringLiteral("h7")}});
        ASSERT_EQ(reply.errorCode, PluginHostApi::kPermissionDenied);
        EXPECT_EQ(reply.errorData.toObject().value(QStringLiteral("permission")).toString(),
                  QStringLiteral("content.read"));
    }
    const PluginHostApi::Reply edit = call(QStringLiteral("items.update"), {{QStringLiteral("handle"), QStringLiteral("h7")}});
    EXPECT_EQ(edit.errorCode, PluginHostApi::kPermissionDenied);
    EXPECT_FALSE(edit.mutated);

    EXPECT_EQ(mRun.denied, (QStringList{QStringLiteral("content.read"), QStringLiteral("items.edit")}));
}

TEST_F(PluginHostApiTest, AnUnknownMethodIsNotFoundRatherThanDenied)
{
    mRun.permissions = {};
    const PluginHostApi::Reply reply = call(QStringLiteral("items.nothing"), {});
    EXPECT_EQ(reply.errorCode, -32601);
    EXPECT_TRUE(mRun.denied.isEmpty());
}

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

TEST_F(PluginHostApiTest, FieldsTrimTheItemButAlwaysKeepTheHandle) {
  NodeSnapshot cat = node(7, "cat.jpg", false);
  cat.sizeBytes = 123;
  ON_CALL(*mClient, getNodeSnapshot(7))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(cat)));
  const auto keysFor = [this](const QJsonArray &fields) {
    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.get"),
             {{QStringLiteral("handles"), QJsonArray{QStringLiteral("h7")}},
              {QStringLiteral("fields"), fields}});
    EXPECT_FALSE(reply.errorCode.has_value());
    QStringList keys = reply.result.toObject()
                           .value(QStringLiteral("items"))
                           .toArray()
                           .at(0)
                           .toObject()
                           .keys();
    keys.sort();
    return keys;
  };

  EXPECT_EQ(keysFor({QStringLiteral("size"), QStringLiteral("parent")}),
            (QStringList{QStringLiteral("handle"), QStringLiteral("parent"),
                         QStringLiteral("size")}));
  EXPECT_EQ(keysFor({QStringLiteral("handle")}),
            QStringList{QStringLiteral("handle")});
  EXPECT_EQ(keysFor({}), QStringList{QStringLiteral("handle")});
}

TEST_F(PluginHostApiTest, FieldsApplyToChildrenAndDescendants) {
  ON_CALL(*mClient, getNodeSnapshot(_)).WillByDefault([](std::uint64_t h) {
    return Result<NodeSnapshot>::ok(node(h, h == 1 ? "dir" : "a", h == 1));
  });
  ON_CALL(*mClient, getChildSnapshots(1))
      .WillByDefault(Return(
          Result<std::vector<NodeSnapshot>>::ok({node(10, "a", false)})));
  ON_CALL(*mClient, listDescendants(1, _))
      .WillByDefault(
          [](std::uint64_t,
             std::function<void(Result<std::vector<DescendantNode>>)> onDone) {
            onDone(Result<std::vector<DescendantNode>>::ok({{10, false}}));
          });
  const QJsonObject params{
      {QStringLiteral("handle"), QStringLiteral("h1")},
      {QStringLiteral("fields"), QJsonArray{QStringLiteral("name")}}};

  for (const QString &method : {QStringLiteral("items.children"),
                                QStringLiteral("items.descendants")}) {
    const PluginHostApi::Reply reply = call(method, params);
    ASSERT_FALSE(reply.errorCode.has_value()) << method.toStdString();
    const QJsonObject item = reply.result.toObject()
                                 .value(QStringLiteral("items"))
                                 .toArray()
                                 .at(0)
                                 .toObject();
    EXPECT_EQ(item,
              (QJsonObject{{QStringLiteral("handle"), QStringLiteral("h10")},
                           {QStringLiteral("name"), QStringLiteral("a")}}))
        << method.toStdString();
  }
}

TEST_F(PluginHostApiTest, AnUnknownFieldIsRejectedBeforeAnythingIsListed) {
  EXPECT_CALL(*mClient, listDescendants(_, _)).Times(0);
  for (const QString &method :
       {QStringLiteral("items.get"), QStringLiteral("items.children"),
        QStringLiteral("items.descendants")}) {
    const PluginHostApi::Reply reply =
        call(method,
             {{QStringLiteral("handle"), QStringLiteral("h1")},
              {QStringLiteral("handles"), QJsonArray{QStringLiteral("h1")}},
              {QStringLiteral("fields"), QJsonArray{QStringLiteral("sise")}}});
    EXPECT_EQ(reply.errorCode, -32602) << method.toStdString();
    EXPECT_TRUE(reply.errorMessage.contains(QStringLiteral("sise")))
        << method.toStdString();
  }
  const PluginHostApi::Reply notAList =
      call(QStringLiteral("items.children"),
           {{QStringLiteral("handle"), QStringLiteral("h1")},
            {QStringLiteral("fields"), QStringLiteral("name")}});
  EXPECT_EQ(notAList.errorCode, -32602);
}

TEST_F(PluginHostApiTest,
       UiRevealHandsOverTheNodeAndItsNameWithoutAnyPermission) {
  std::vector<std::pair<std::uint64_t, QString>> revealed;
  PluginHostApi api{mClient,
                    &mGuiContext,
                    {},
                    [&revealed](std::uint64_t handle, const QString &name) {
                      revealed.emplace_back(handle, name);
                    }};
  NodeSnapshot root = node(1, "Cloud Drive", true);
  root.hasParent = false;
  ON_CALL(*mClient, getNodeSnapshot(7))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(node(7, "cat.jpg", false))));
  ON_CALL(*mClient, getNodeSnapshot(1))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(root)));
  mRun.permissions = {};
  const auto reveal = [&](const QString &handle) {
    std::optional<PluginHostApi::Reply> reply;
    api.call(QStringLiteral("ui.reveal"), {{QStringLiteral("handle"), handle}},
             mRun, [&reply](const PluginHostApi::Reply &r) { reply = r; });
    EXPECT_TRUE(reply.has_value());
    return reply.value_or(PluginHostApi::Reply{});
  };

  EXPECT_FALSE(PluginHostApi::requiredPermission(QStringLiteral("ui.reveal"))
                   .has_value());
  EXPECT_FALSE(reveal(QStringLiteral("h7")).errorCode.has_value());
  EXPECT_EQ(reveal(QStringLiteral("h9")).errorCode,
            PluginHostApi::kItemNotFound);
  EXPECT_EQ(reveal(QStringLiteral("h1")).errorCode, -32602);
  EXPECT_EQ(reveal(QStringLiteral("zz")).errorCode, -32602);

  ASSERT_EQ(revealed.size(), 1u);
  EXPECT_EQ(revealed[0].first, 7u);
  EXPECT_EQ(revealed[0].second, QStringLiteral("cat.jpg"));
}

TEST_F(PluginHostApiTest, UiSearchHandsOverTheWholeCriteriaWithoutAnyPermission) {
  std::vector<std::pair<QString, SearchFilter>> searched;
  PluginHostApi api{mClient, &mGuiContext, {}, {},
                    [&searched](const QString &query, const SearchFilter &filter) {
                      searched.emplace_back(query, filter);
                    }};
  mRun.permissions = {};
  const auto search = [&](const QJsonObject &params) {
    std::optional<PluginHostApi::Reply> reply;
    api.call(QStringLiteral("ui.search"), params, mRun,
             [&reply](const PluginHostApi::Reply &r) { reply = r; });
    EXPECT_TRUE(reply.has_value());
    return reply.value_or(PluginHostApi::Reply{});
  };

  EXPECT_FALSE(PluginHostApi::requiredPermission(QStringLiteral("ui.search"))
                   .has_value());
  EXPECT_FALSE(search({{QStringLiteral("query"), QStringLiteral("tag:\"a b\"")},
                       {QStringLiteral("type"), QStringLiteral("files")},
                       {QStringLiteral("category"), QStringLiteral("spreadsheet")},
                       {QStringLiteral("createdWithin"), QStringLiteral("pastMonth")},
                       {QStringLiteral("favouritesOnly"), true},
                       {QStringLiteral("thisFolderOnly"), true}})
                   .errorCode.has_value());
  // Left out means the default, not "keep what the tab had".
  EXPECT_FALSE(search({}).errorCode.has_value());

  for (const QJsonObject &bad :
       {QJsonObject{{QStringLiteral("type"), QStringLiteral("file")}},
        QJsonObject{{QStringLiteral("category"), 1}},
        QJsonObject{{QStringLiteral("createdWithin"), QStringLiteral("today")}},
        QJsonObject{{QStringLiteral("favouritesOnly"), QStringLiteral("yes")}},
        QJsonObject{{QStringLiteral("query"), 5}},
        QJsonObject{{QStringLiteral("query"), QStringLiteral("a\nb")}}})
    EXPECT_EQ(search(bad).errorCode, -32602);

  ASSERT_EQ(searched.size(), 2u);
  EXPECT_EQ(searched[0].first, QStringLiteral("tag:\"a b\""));
  EXPECT_EQ(searched[0].second.nodeType, SearchNodeType::Files);
  EXPECT_EQ(searched[0].second.category, SearchCategory::Spreadsheet);
  EXPECT_EQ(searched[0].second.createdWithin, SearchTimeWindow::PastMonth);
  EXPECT_TRUE(searched[0].second.favouritesOnly);
  EXPECT_TRUE(searched[0].second.thisFolderOnly);
  EXPECT_EQ(searched[1].first, QString());
  EXPECT_TRUE(searched[1].second.isDefault());
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
    ON_CALL(*mClient, getNodeSnapshot(_)).WillByDefault([&nodes](std::uint64_t h) {
        const auto it = nodes.find(h);
        return it == nodes.end() ? Result<NodeSnapshot>::fail("gone", -9) : Result<NodeSnapshot>::ok(it->second);
    });
    // The client's own walk order, which the listing keeps.
    ON_CALL(*mClient, listDescendants(1, _))
        .WillByDefault([](std::uint64_t, std::function<void(Result<std::vector<DescendantNode>>)> onDone) {
            onDone(Result<std::vector<DescendantNode>>::ok(
                {{10, false}, {11, true}, {12, false}, {13, true}, {15, false}, {14, false}}));
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

    // The listing was fixed by the first page: a node deleted since is skipped, so
    // this page comes up one short.
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
    ON_CALL(*mClient, listDescendants(_, _))
        .WillByDefault([](std::uint64_t h, std::function<void(Result<std::vector<DescendantNode>>)> onDone) {
            onDone(Result<std::vector<DescendantNode>>::fail(
                "", h == 7 ? MegaErrorCode::kEArgs : MegaErrorCode::kENoEnt));
        });
    EXPECT_EQ(call(QStringLiteral("items.descendants"), {{QStringLiteral("handle"), QStringLiteral("h7")}})
                  .errorCode,
              -32602);
    EXPECT_EQ(call(QStringLiteral("items.descendants"), {{QStringLiteral("handle"), QStringLiteral("h8")}})
                  .errorCode,
              PluginHostApi::kItemNotFound);
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

namespace {
NodeSnapshot sizedFile(std::uint64_t handle, const std::string &name,
                       std::uint64_t size) {
  NodeSnapshot n = node(handle, name, false);
  n.sizeBytes = size;
  return n;
}

using DownloadDone = std::function<void(Result<DownloadOutcome>)>;
using ProgressFn = std::function<void(std::uint64_t, std::uint64_t)>;
using ChunkFn = std::function<bool(const char *, std::size_t)>;
using VoidDone = std::function<void(Result<void>)>;
} // namespace

TEST_F(PluginHostApiTest,
       ItemsFetchFileDownloadsIntoAFolderOfItsOwnInTheRunsTempDir) {
  ON_CALL(*mClient, getNodeSnapshot(26))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(sizedFile(26, "cat.jpg", 10))));
  std::string requestedPath;
  EXPECT_CALL(*mClient, download(26, _, _, _, _))
      .WillOnce([&requestedPath](std::uint64_t, const std::string &path,
                                 std::uint64_t, ProgressFn,
                                 DownloadDone onDone) {
        requestedPath = path;
        onDone(Result<DownloadOutcome>::ok(DownloadOutcome{path}));
      });

  const PluginHostApi::Reply reply =
      call(QStringLiteral("items.fetchFile"),
           {{QStringLiteral("handle"), QStringLiteral("h26")}});

  ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
  EXPECT_EQ(QDir::fromNativeSeparators(QString::fromStdString(requestedPath)),
            QDir(mTempDir.path()).filePath(QStringLiteral("files/1/cat.jpg")));
  EXPECT_EQ(reply.result.toObject()
                .value(QStringLiteral("path"))
                .toString()
                .toStdString(),
            requestedPath);
}

TEST_F(PluginHostApiTest, ItemsFetchFileWithARangeWritesOnlyThoseBytes) {
  ON_CALL(*mClient, getNodeSnapshot(26))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(sizedFile(26, "a.zip", 100))));
  EXPECT_CALL(*mClient, readFileRangeStreamed(26, 90, 10, _, _))
      .WillOnce([](std::uint64_t, std::uint64_t, std::uint64_t, ChunkFn onChunk,
                   VoidDone onDone) {
        onChunk("0123456789", 10);
        onDone(Result<void>::ok());
      });

  // length is cut at the end of the file.
  const PluginHostApi::Reply reply =
      call(QStringLiteral("items.fetchFile"),
           {{QStringLiteral("handle"), QStringLiteral("h26")},
            {QStringLiteral("offset"), 90},
            {QStringLiteral("length"), 50}});

  ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
  QFile file(reply.result.toObject().value(QStringLiteral("path")).toString());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), QByteArray("0123456789"));
}

TEST_F(PluginHostApiTest, ItemsReadRangeReturnsBase64AndCapsTheLength) {
  ON_CALL(*mClient, getNodeSnapshot(26))
      .WillByDefault(Return(
          Result<NodeSnapshot>::ok(sizedFile(26, "a.bin", 10 * 1024 * 1024))));
  EXPECT_CALL(*mClient, readFileRangeStreamed(26, 4, 3, _, _))
      .WillOnce([](std::uint64_t, std::uint64_t, std::uint64_t, ChunkFn onChunk,
                   VoidDone onDone) {
        onChunk("abc", 3);
        onDone(Result<void>::ok());
      });

  const PluginHostApi::Reply reply =
      call(QStringLiteral("items.readRange"),
           {{QStringLiteral("handle"), QStringLiteral("h26")},
            {QStringLiteral("offset"), 4},
            {QStringLiteral("length"), 3}});
  ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
  EXPECT_EQ(reply.result.toObject().value(QStringLiteral("data")).toString(),
            QStringLiteral("YWJj"));

  EXPECT_EQ(call(QStringLiteral("items.readRange"),
                 {{QStringLiteral("handle"), QStringLiteral("h26")},
                  {QStringLiteral("offset"), 0},
                  {QStringLiteral("length"), 1024 * 1024 + 1}})
                .errorCode,
            -32602);
  EXPECT_EQ(call(QStringLiteral("items.readRange"),
                 {{QStringLiteral("handle"), QStringLiteral("h26")},
                  {QStringLiteral("offset"), 10 * 1024 * 1024},
                  {QStringLiteral("length"), 1}})
                .errorCode,
            -32602);
}

TEST_F(PluginHostApiTest,
       TransfersRunOneAtATimeAndCancelStopsTheRunningAndTheQueued) {
  ON_CALL(*mClient, getNodeSnapshot(_)).WillByDefault([](std::uint64_t h) {
    return Result<NodeSnapshot>::ok(sizedFile(h, "f" + std::to_string(h), 10));
  });
  DownloadDone firstDone;
  std::uint64_t firstId = 0;
  EXPECT_CALL(*mClient, download(1, _, _, _, _))
      .WillOnce([&](std::uint64_t, const std::string &, std::uint64_t id,
                    ProgressFn, DownloadDone onDone) {
        firstId = id;
        firstDone = std::move(onDone);
      });
  EXPECT_CALL(*mClient, download(2, _, _, _, _)).Times(0);

  mRun.tempDir = mTempDir.path();
  std::optional<PluginHostApi::Reply> first;
  std::optional<PluginHostApi::Reply> second;
  mApi.call(QStringLiteral("items.fetchFile"),
            {{QStringLiteral("handle"), QStringLiteral("h1")}}, mRun,
            [&first](const PluginHostApi::Reply &r) { first = r; });
  mApi.call(QStringLiteral("items.fetchFile"),
            {{QStringLiteral("handle"), QStringLiteral("h2")}}, mRun,
            [&second](const PluginHostApi::Reply &r) { second = r; });
  ASSERT_TRUE(firstDone);
  EXPECT_FALSE(second.has_value());

  EXPECT_CALL(*mClient, cancelDownload(firstId))
      .WillOnce([&firstDone](std::uint64_t) {
        firstDone(Result<DownloadOutcome>::fail("aborted",
                                                MegaErrorCode::kEIncomplete));
      });
  PluginHostApi::cancelTransfers(mRun);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->errorCode, PluginHostApi::kCancelled);
  for (int i = 0; i < 20 && !first; ++i)
    QCoreApplication::processEvents();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->errorCode, PluginHostApi::kCancelled);

  // Later ones are refused straight away.
  std::optional<PluginHostApi::Reply> third;
  mApi.call(QStringLiteral("items.fetchFile"),
            {{QStringLiteral("handle"), QStringLiteral("h2")}}, mRun,
            [&third](const PluginHostApi::Reply &r) { third = r; });
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->errorCode, PluginHostApi::kCancelled);
}

TEST_F(PluginHostApiTest,
       ItemsCreateFolderReturnsTheNewFolderOrTheExistingOne) {
  ON_CALL(*mClient, getNodeSnapshot(1))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(node(1, "root", true))));
  ON_CALL(*mClient, getNodeSnapshot(50))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(node(50, "Reports (2)", true))));
  ON_CALL(*mClient, getChildSnapshots(1))
      .WillByDefault(Return(Result<std::vector<NodeSnapshot>>::ok(
          {node(9, "Reports", true), node(8, "notes", false)})));
  EXPECT_CALL(*mClient, createFolder(1, false, std::string("Reports (2)"), _))
      .WillOnce([](std::uint64_t, bool, const std::string &,
                   std::function<void(Result<std::uint64_t>)> onDone) {
        onDone(Result<std::uint64_t>::ok(50));
      });
  const auto handleOf = [](const PluginHostApi::Reply &reply) {
    return reply.result.toObject()
        .value(QStringLiteral("item"))
        .toObject()
        .value(QStringLiteral("handle"))
        .toString();
  };

  // "existing", the default: no request at all.
  PluginHostApi::Reply reply =
      call(QStringLiteral("items.createFolder"),
           {{QStringLiteral("parent"), QStringLiteral("h1")},
            {QStringLiteral("name"), QStringLiteral("Reports")}});
  ASSERT_FALSE(reply.errorCode.has_value());
  EXPECT_FALSE(
      reply.result.toObject().value(QStringLiteral("created")).toBool());
  EXPECT_EQ(handleOf(reply), QStringLiteral("h9"));
  EXPECT_FALSE(reply.mutated);

  reply = call(QStringLiteral("items.createFolder"),
               {{QStringLiteral("parent"), QStringLiteral("h1")},
                {QStringLiteral("name"), QStringLiteral("Reports")},
                {QStringLiteral("onConflict"), QStringLiteral("fail")}});
  EXPECT_EQ(reply.errorCode, PluginHostApi::kConflict);
  EXPECT_EQ(
      reply.errorData.toObject().value(QStringLiteral("reason")).toString(),
      QStringLiteral("exists"));

  reply = call(QStringLiteral("items.createFolder"),
               {{QStringLiteral("parent"), QStringLiteral("h1")},
                {QStringLiteral("name"), QStringLiteral("Reports")},
                {QStringLiteral("onConflict"), QStringLiteral("rename")}});
  ASSERT_FALSE(reply.errorCode.has_value());
  EXPECT_TRUE(
      reply.result.toObject().value(QStringLiteral("created")).toBool());
  EXPECT_EQ(handleOf(reply), QStringLiteral("h50"));
  EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiTest,
       ItemsUploadRenamesOnAClashAndRefusesVersionWhenVersioningIsOff) {
  QFile local(QDir(mTempDir.path()).filePath(QStringLiteral("notes.txt")));
  ASSERT_TRUE(local.open(QIODevice::WriteOnly));
  local.write("x");
  local.close();
  ON_CALL(*mClient, getNodeSnapshot(1))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(node(1, "root", true))));
  ON_CALL(*mClient, getNodeSnapshot(60))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(node(60, "notes (2).txt", false))));
  ON_CALL(*mClient, checkUpload(1, false))
      .WillByDefault(Return(Result<void>::ok()));
  ON_CALL(*mClient, getChildSnapshots(1))
      .WillByDefault(Return(Result<std::vector<NodeSnapshot>>::ok(
          {node(8, "notes.txt", false)})));
  ON_CALL(*mClient, getFileVersioningEnabled(_))
      .WillByDefault([](std::function<void(Result<bool>)> onDone) {
        onDone(Result<bool>::ok(false));
      });
  EXPECT_CALL(*mClient,
              upload(_, 1, false, std::string("notes (2).txt"), _, _, _))
      .WillOnce([](const std::string &, std::uint64_t, bool,
                   const std::string &, std::uint64_t, ProgressFn,
                   std::function<void(Result<UploadOutcome>)> onDone) {
        onDone(Result<UploadOutcome>::ok(UploadOutcome{60}));
      });
  const QJsonObject base{{QStringLiteral("parent"), QStringLiteral("h1")},
                         {QStringLiteral("localPath"),
                          QDir::toNativeSeparators(local.fileName())}};

  PluginHostApi::Reply reply = call(QStringLiteral("items.upload"), base);
  ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
  EXPECT_EQ(reply.result.toObject()
                .value(QStringLiteral("item"))
                .toObject()
                .value(QStringLiteral("handle"))
                .toString(),
            QStringLiteral("h60"));
  EXPECT_TRUE(reply.mutated);

  QJsonObject failParams = base;
  failParams.insert(QStringLiteral("onConflict"), QStringLiteral("fail"));
  reply = call(QStringLiteral("items.upload"), failParams);
  EXPECT_EQ(reply.errorCode, PluginHostApi::kConflict);
  EXPECT_EQ(
      reply.errorData.toObject().value(QStringLiteral("reason")).toString(),
      QStringLiteral("exists"));

  QJsonObject versionParams = base;
  versionParams.insert(QStringLiteral("onConflict"), QStringLiteral("version"));
  reply = call(QStringLiteral("items.upload"), versionParams);
  EXPECT_EQ(reply.errorCode, PluginHostApi::kConflict);
  EXPECT_EQ(
      reply.errorData.toObject().value(QStringLiteral("reason")).toString(),
      QStringLiteral("versioningDisabled"));

  QJsonObject missing = base;
  missing.insert(QStringLiteral("localPath"),
                 QDir(mTempDir.path()).filePath(QStringLiteral("nope.txt")));
  EXPECT_EQ(call(QStringLiteral("items.upload"), missing).errorCode, -32602);
}

TEST_F(PluginHostApiTest,
       TransfersDownloadQueuesUnderTheDownloadsFolderAndHonoursOnConflict) {
  QTemporaryDir downloads;
  std::vector<QString> queuedPaths;
  PluginHostApi api{
      mClient,
      &mGuiContext,
      {downloads.path(), [&queuedPaths](std::uint64_t, const QString &,
                                        std::uint64_t, const QString &path) {
         queuedPaths.push_back(QDir::fromNativeSeparators(path));
         return true;
       }}};
  ON_CALL(*mClient, getNodeSnapshot(26))
      .WillByDefault(
          Return(Result<NodeSnapshot>::ok(sizedFile(26, "cat.jpg", 10))));
  ON_CALL(*mClient, getNodeSnapshot(3))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(node(3, "dir", true))));
  const auto download = [&](const QJsonObject &params) {
    std::optional<PluginHostApi::Reply> reply;
    api.call(QStringLiteral("transfers.download"), params, mRun,
             [&reply](const PluginHostApi::Reply &r) { reply = r; });
    return reply.value_or(PluginHostApi::Reply{});
  };
  const auto items = [](const QString &subPath) {
    return QJsonArray{
        QJsonObject{{QStringLiteral("handle"), QStringLiteral("h26")},
                    {QStringLiteral("subPath"), subPath}}};
  };

  PluginHostApi::Reply reply = download(
      {{QStringLiteral("items"), items(QStringLiteral("trip\\day1"))}});
  ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
  EXPECT_EQ(reply.result.toObject().value(QStringLiteral("queued")).toInt(), 1);
  ASSERT_EQ(queuedPaths.size(), 1u);
  EXPECT_EQ(
      queuedPaths[0],
      QDir(downloads.path()).filePath(QStringLiteral("trip/day1/cat.jpg")));
  EXPECT_TRUE(QDir(downloads.path()).exists(QStringLiteral("trip/day1")));

  QFile existing(queuedPaths[0]);
  ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
  existing.close();
  reply =
      download({{QStringLiteral("items"), items(QStringLiteral("trip/day1"))},
                {QStringLiteral("onConflict"), QStringLiteral("skip")}});
  EXPECT_EQ(reply.result.toObject().value(QStringLiteral("skipped")).toInt(),
            1);
  EXPECT_EQ(queuedPaths.size(), 1u);

  for (const QString &bad : {QStringLiteral("../out"), QStringLiteral("/abs"),
                             QStringLiteral("C:/x"), QStringLiteral("a/./b")})
    EXPECT_EQ(download({{QStringLiteral("items"), items(bad)}}).errorCode,
              -32602)
        << bad.toStdString();
  EXPECT_EQ(download({{QStringLiteral("items"),
                       QJsonArray{QJsonObject{
                           {QStringLiteral("handle"), QStringLiteral("h3")}}}}})
                .errorCode,
            -32602);
  EXPECT_EQ(queuedPaths.size(), 1u);
}

TEST_F(PluginHostApiTest, TransfersDownloadOverwriteLeavesTheFileAloneForAnAlreadyQueuedItem) {
  ON_CALL(*mClient, getNodeSnapshot(40))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(sizedFile(40, "a.txt", 3))));
  ON_CALL(*mClient, getNodeSnapshot(41))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(sizedFile(41, "b.txt", 3))));
  QTemporaryDir downloads;
  for (const char *name : {"a.txt", "b.txt"}) {
    QFile file(QDir(downloads.path()).filePath(QString::fromLatin1(name)));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("old");
  }
  int enqueued = 0;
  PluginHostApi api{mClient, &mGuiContext,
                    PluginHostApi::UserDownloads{
                        downloads.path(),
                        [&enqueued](std::uint64_t, const QString &, std::uint64_t,
                                    const QString &) {
                          ++enqueued;
                          return true;
                        },
                        [](std::uint64_t handle) { return handle == 40; }}};

  std::optional<PluginHostApi::Reply> reply;
  api.call(QStringLiteral("transfers.download"),
           {{QStringLiteral("onConflict"), QStringLiteral("overwrite")},
            {QStringLiteral("items"),
             QJsonArray{QJsonObject{{QStringLiteral("handle"), QStringLiteral("h40")}},
                        QJsonObject{{QStringLiteral("handle"), QStringLiteral("h41")}}}}},
           mRun, [&reply](const PluginHostApi::Reply &r) { reply = r; });

  ASSERT_TRUE(reply.has_value());
  ASSERT_FALSE(reply->errorCode.has_value()) << reply->errorMessage.toStdString();
  EXPECT_EQ(reply->result.toObject().value(QStringLiteral("queued")).toInt(), 1);
  EXPECT_EQ(reply->result.toObject().value(QStringLiteral("skipped")).toInt(), 1);
  EXPECT_EQ(enqueued, 1);
  EXPECT_TRUE(QFile::exists(QDir(downloads.path()).filePath(QStringLiteral("a.txt"))));
}

TEST_F(PluginHostApiTest, ItemsUploadChecksForAClashWhenItsTurnComesNotWhenQueued) {
  QFile local(QDir(mTempDir.path()).filePath(QStringLiteral("notes.txt")));
  ASSERT_TRUE(local.open(QIODevice::WriteOnly));
  local.write("x");
  local.close();
  ON_CALL(*mClient, getNodeSnapshot(1))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(node(1, "root", true))));
  ON_CALL(*mClient, getNodeSnapshot(60))
      .WillByDefault(Return(Result<NodeSnapshot>::ok(node(60, "notes.txt", false))));
  ON_CALL(*mClient, checkUpload(1, false)).WillByDefault(Return(Result<void>::ok()));
  std::vector<NodeSnapshot> children;
  ON_CALL(*mClient, getChildSnapshots(1)).WillByDefault([&children](std::uint64_t) {
    return Result<std::vector<NodeSnapshot>>::ok(children);
  });
  std::function<void(Result<UploadOutcome>)> firstDone;
  EXPECT_CALL(*mClient, upload(_, 1, false, std::string("notes.txt"), _, _, _))
      .WillOnce([&firstDone](const std::string &, std::uint64_t, bool, const std::string &,
                             std::uint64_t, ProgressFn,
                             std::function<void(Result<UploadOutcome>)> onDone) {
        firstDone = std::move(onDone);
      });
  const QJsonObject params{{QStringLiteral("parent"), QStringLiteral("h1")},
                           {QStringLiteral("localPath"), QDir::toNativeSeparators(local.fileName())},
                           {QStringLiteral("onConflict"), QStringLiteral("fail")}};

  mRun.tempDir = mTempDir.path();
  std::optional<PluginHostApi::Reply> first;
  std::optional<PluginHostApi::Reply> second;
  mApi.call(QStringLiteral("items.upload"), params, mRun,
            [&first](const PluginHostApi::Reply &r) { first = r; });
  mApi.call(QStringLiteral("items.upload"), params, mRun,
            [&second](const PluginHostApi::Reply &r) { second = r; });
  ASSERT_TRUE(firstDone);
  EXPECT_FALSE(second.has_value());

  children = {node(60, "notes.txt", false)};
  firstDone(Result<UploadOutcome>::ok(UploadOutcome{60}));
  for (int i = 0; i < 20 && !second; ++i)
    QCoreApplication::processEvents();

  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(first->errorCode.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->errorCode, PluginHostApi::kConflict);
}

namespace
{
// root(1) holds Dest(2), a.txt(5) and Photos(6); Dest holds a.txt(20).
class PluginHostApiOrganiseTest : public PluginHostApiTest
{
protected:
    void SetUp() override
    {
        PluginHostApiTest::SetUp();
        mRun.permissions << QStringLiteral("items.rubbish");
        NodeSnapshot root = node(1, "root", true);
        root.hasParent = false;
        root.path = "/";
        for (const NodeSnapshot& n : {root, node(2, "Dest", true), node(5, "a.txt", false), node(6, "Photos", true)})
            ON_CALL(*mClient, getNodeSnapshot(n.handle)).WillByDefault(Return(Result<NodeSnapshot>::ok(n)));
        ON_CALL(*mClient, getChildSnapshots(1))
            .WillByDefault(Return(Result<std::vector<NodeSnapshot>>::ok(
                {node(2, "Dest", true), node(5, "a.txt", false), node(6, "Photos", true)})));
        NodeSnapshot inDest = node(20, "a.txt", false);
        inDest.parentHandle = 2;
        ON_CALL(*mClient, getChildSnapshots(2))
            .WillByDefault(Return(Result<std::vector<NodeSnapshot>>::ok({inDest})));
        ON_CALL(*mClient, checkMove(_, _, _)).WillByDefault(Return(Result<void>::ok()));
        ON_CALL(*mClient, checkUpload(_, _)).WillByDefault(Return(Result<void>::ok()));
    }

    static QJsonObject params(const char* handle, const char* to)
    {
        return {{QStringLiteral("handle"), QString::fromLatin1(handle)}, {QStringLiteral("to"), QString::fromLatin1(to)}};
    }
};
} // namespace

TEST_F(PluginHostApiTest, OrganisingMethodsNeedWriteOrRubbish)
{
    EXPECT_EQ(PluginHostApi::requiredPermission(QStringLiteral("items.copy")), QStringLiteral("items.write"));
    EXPECT_EQ(PluginHostApi::requiredPermission(QStringLiteral("items.move")), QStringLiteral("items.write"));
    EXPECT_EQ(PluginHostApi::requiredPermission(QStringLiteral("items.moveToRubbish")), QStringLiteral("items.rubbish"));
    EXPECT_TRUE(isKnownPluginPermission(QStringLiteral("items.rubbish")));

    EXPECT_CALL(*mClient, moveToRubbish(_, _)).Times(0);
    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.moveToRubbish"), {{QStringLiteral("handle"), QStringLiteral("h5")}});
    EXPECT_EQ(reply.errorCode, PluginHostApi::kPermissionDenied);
}

TEST_F(PluginHostApiOrganiseTest, MoveRenamesOnAClashOfTheSameTypeAndKeepsTheHandle)
{
    EXPECT_CALL(*mClient, moveNode(5u, 2u, false, std::string("a (2).txt"), _))
        .WillOnce(::testing::InvokeArgument<4>(Result<void>::ok()));
    const PluginHostApi::Reply reply = call(QStringLiteral("items.move"), params("h5", "h2"));
    ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
    EXPECT_TRUE(reply.result.toObject().value(QStringLiteral("moved")).toBool());
    EXPECT_EQ(reply.result.toObject().value(QStringLiteral("item")).toObject().value(QStringLiteral("handle")).toString(),
              QStringLiteral("h5"));
    EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiOrganiseTest, MoveWithFailOrVersionChangesNothing)
{
    EXPECT_CALL(*mClient, moveNode(_, _, _, _, _)).Times(0);
    QJsonObject p = params("h5", "h2");
    p.insert(QStringLiteral("onConflict"), QStringLiteral("fail"));
    PluginHostApi::Reply reply = call(QStringLiteral("items.move"), p);
    EXPECT_EQ(reply.errorCode, PluginHostApi::kConflict);
    EXPECT_EQ(reply.errorData.toObject().value(QStringLiteral("reason")).toString(), QStringLiteral("exists"));

    p.insert(QStringLiteral("onConflict"), QStringLiteral("version"));
    reply = call(QStringLiteral("items.move"), p);
    EXPECT_EQ(reply.errorCode, -32602);
}

TEST_F(PluginHostApiOrganiseTest, MoveIntoItsOwnFolderIsANoOpUnlessItRenames)
{
    EXPECT_CALL(*mClient, moveNode(_, _, _, _, _)).Times(0);
    PluginHostApi::Reply reply = call(QStringLiteral("items.move"), params("h5", "h1"));
    ASSERT_FALSE(reply.errorCode.has_value());
    EXPECT_FALSE(reply.result.toObject().value(QStringLiteral("moved")).toBool());
    EXPECT_FALSE(reply.mutated);

    QJsonObject p = params("h5", "h1");
    p.insert(QStringLiteral("name"), QStringLiteral("b.txt"));
    reply = call(QStringLiteral("items.move"), p);
    EXPECT_EQ(reply.errorCode, -32602);
}

TEST_F(PluginHostApiOrganiseTest, MoveReportsWhyMegaWouldRefuseIt)
{
    EXPECT_CALL(*mClient, moveNode(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*mClient, checkMove(6u, 2u, false))
        .WillOnce(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)))
        .WillOnce(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));
    PluginHostApi::Reply reply = call(QStringLiteral("items.move"), params("h6", "h2"));
    EXPECT_EQ(reply.errorCode, -32602);
    EXPECT_EQ(reply.errorData.toObject().value(QStringLiteral("reason")).toString(), QStringLiteral("circular"));
    reply = call(QStringLiteral("items.move"), params("h6", "h2"));
    EXPECT_EQ(reply.errorCode, PluginHostApi::kMegaError);
    EXPECT_EQ(reply.errorData.toObject().value(QStringLiteral("reason")).toString(), QStringLiteral("access"));
}

TEST_F(PluginHostApiOrganiseTest, CopyIntoItsOwnFolderClashesWithItselfAndReturnsTheCopy)
{
    ON_CALL(*mClient, getNodeSnapshot(50)).WillByDefault(Return(Result<NodeSnapshot>::ok(node(50, "a (2).txt", false))));
    EXPECT_CALL(*mClient, copyNode(5u, 1u, false, std::string("a (2).txt"), _))
        .WillOnce(::testing::InvokeArgument<4>(Result<std::uint64_t>::ok(50)));
    const PluginHostApi::Reply reply = call(QStringLiteral("items.copy"), params("h5", "h1"));
    ASSERT_FALSE(reply.errorCode.has_value()) << reply.errorMessage.toStdString();
    EXPECT_EQ(reply.result.toObject().value(QStringLiteral("item")).toObject().value(QStringLiteral("handle")).toString(),
              QStringLiteral("h50"));
    EXPECT_TRUE(reply.mutated);
}

TEST_F(PluginHostApiOrganiseTest, CopyWithoutAClashKeepsTheSourceName)
{
    EXPECT_CALL(*mClient, copyNode(6u, 2u, false, std::string(), _))
        .WillOnce(::testing::InvokeArgument<4>(Result<std::uint64_t>::ok(60)));
    EXPECT_FALSE(call(QStringLiteral("items.copy"), params("h6", "h2")).errorCode.has_value());
}

TEST_F(PluginHostApiOrganiseTest, CopyAsAVersionOnlyForAFileAndOnlyWithVersioningOn)
{
    QJsonObject p = params("h6", "h2");
    p.insert(QStringLiteral("onConflict"), QStringLiteral("version"));
    EXPECT_EQ(call(QStringLiteral("items.copy"), p).errorCode, -32602);

    bool versioning = false;
    ON_CALL(*mClient, getFileVersioningEnabled(_)).WillByDefault([&versioning](std::function<void(Result<bool>)> onDone) {
        onDone(Result<bool>::ok(versioning));
    });
    p = params("h5", "h2");
    p.insert(QStringLiteral("onConflict"), QStringLiteral("version"));
    EXPECT_CALL(*mClient, copyNode(5u, 2u, false, std::string(), _))
        .WillOnce(::testing::InvokeArgument<4>(Result<std::uint64_t>::ok(21)));

    PluginHostApi::Reply reply = call(QStringLiteral("items.copy"), p);
    EXPECT_EQ(reply.errorCode, PluginHostApi::kConflict);
    EXPECT_EQ(reply.errorData.toObject().value(QStringLiteral("reason")).toString(), QStringLiteral("versioningDisabled"));

    versioning = true;
    reply = call(QStringLiteral("items.copy"), p);
    EXPECT_FALSE(reply.errorCode.has_value());
}

TEST_F(PluginHostApiOrganiseTest, MoveToRubbishRefusesARootAndAnItemAlreadyBinned)
{
    NodeSnapshot binned = node(7, "old.txt", false);
    binned.inRubbish = true;
    ON_CALL(*mClient, getNodeSnapshot(7)).WillByDefault(Return(Result<NodeSnapshot>::ok(binned)));
    EXPECT_CALL(*mClient, moveToRubbish(5u, _)).WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));

    EXPECT_EQ(call(QStringLiteral("items.moveToRubbish"), {{QStringLiteral("handle"), QStringLiteral("h1")}}).errorCode,
              -32602);
    EXPECT_EQ(call(QStringLiteral("items.moveToRubbish"), {{QStringLiteral("handle"), QStringLiteral("h7")}}).errorCode,
              PluginHostApi::kItemNotFound);
    const PluginHostApi::Reply reply =
        call(QStringLiteral("items.moveToRubbish"), {{QStringLiteral("handle"), QStringLiteral("h5")}});
    EXPECT_FALSE(reply.errorCode.has_value());
    EXPECT_TRUE(reply.mutated);
}
