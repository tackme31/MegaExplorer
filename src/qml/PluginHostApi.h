#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class IMegaClient;
class QObject;

// The methods a plugin calls on the app (STUDY_PLUGIN_V1_DESIGN.md §6-3), apart
// from the transport: PluginRun hands over a request's method and params and
// writes back whatever this answers.
class PluginHostApi
{
public:
    // Application error codes, JSON-RPC's -32000..-32099 "server error" range.
    // -32001 is reserved for PermissionDenied (STUDY_PLUGIN_V1_DESIGN.md §6-3).
    static constexpr int kItemNotFound = -32002;
    static constexpr int kMegaError = -32010;

    struct Reply
    {
        QJsonValue result;
        // Set when the call failed; result is then ignored.
        std::optional<int> errorCode;
        QString errorMessage;
        // Whether the account changed, failed or not: a write can stop part-way.
        bool mutated = false;
    };
    using Done = std::function<void(const Reply&)>;

    // guiContext must live as long as the client can still call back (the app's
    // lifetime); SDK answers are posted to its thread before done runs.
    PluginHostApi(std::shared_ptr<IMegaClient> client, QObject* guiContext);

    // What one plugin run keeps between its calls; owned by the run, so it all goes
    // when the run ends.
    struct RunState
    {
        // The run's own folder for fetched files; created on first use.
        QString tempDir;
        // items.descendants listings, fixed at their first page; a cursor indexes one.
        std::vector<std::vector<std::uint64_t>> listings;
    };

    // done always runs on guiContext's thread: in-stack for the in-memory reads,
    // later for anything that goes to the server. run is only touched before
    // call returns.
    void call(const QString& method, const QJsonObject& params, RunState& run, const Done& done) const;

    // The Item form items.get returns, also used for the context's items. nullopt
    // when the node no longer exists.
    std::optional<QJsonObject> item(std::uint64_t handle) const;

private:
    Reply itemsGet(const QJsonObject& params) const;
    Reply itemsChildren(const QJsonObject& params) const;
    Reply itemsDescendants(const QJsonObject& params, RunState& run) const;
    void itemsUpdate(const QJsonObject& params, const Done& done) const;
    void itemsFetchPreview(const QJsonObject& params, const QString& tempDir, const Done& done) const;

    std::shared_ptr<IMegaClient> mClient;
    QObject* mGuiContext;
};
