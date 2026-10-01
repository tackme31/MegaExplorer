#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

class IMegaClient;

// The methods a plugin calls on the app (STUDY_PLUGIN_V1_DESIGN.md §6-3), apart
// from the transport: PluginRun hands over a request's method and params and
// writes back whatever this answers.
class PluginHostApi
{
public:
    // Application error codes, JSON-RPC's -32000..-32099 "server error" range.
    // -32001 is reserved for PermissionDenied (STUDY_PLUGIN_V1_DESIGN.md §6-3).
    static constexpr int kItemNotFound = -32002;

    struct Reply
    {
        QJsonValue result;
        // Set when the call failed; result is then ignored.
        std::optional<int> errorCode;
        QString errorMessage;
    };

    explicit PluginHostApi(std::shared_ptr<IMegaClient> client);

    Reply call(const QString& method, const QJsonObject& params) const;

    // The context's ItemRef form: {handle, name, type, parent}. nullopt when the
    // node no longer exists.
    std::optional<QJsonObject> itemRef(std::uint64_t handle) const;

private:
    Reply itemsGet(const QJsonObject& params) const;
    Reply itemsChildren(const QJsonObject& params) const;

    std::shared_ptr<IMegaClient> mClient;
};
