#pragma once

// The subset of MegaApi's USER_ATTR_* values this app reads. Parameterises
// IMegaClient::getMyUserAttribute rather than giving each attribute its own
// port method, so the "join first + last, degrade to whatever arrived" policy
// stays in AccountService where MockMegaClient can test it -- MegaSdkClient
// has no adapter test (it needs a live account, docs/ARCHITECTURE.md).
//
// Qt-free like SortOrder.h/DownloadOutcome.h so src/core stays Qt-agnostic.
enum class UserAttribute
{
    FirstName,
    LastName,
};
