#pragma once
#include <cstdint>

// Success payload for IMegaClient::upload's completion callback.
//
// Deliberately has no counterpart to DownloadOutcome::alreadyPresent: the
// SDK's upload-side shortcut (an existing node with a matching fingerprint,
// whose modification time is simply updated) is indistinguishable from a real
// upload both to the user and to us -- there is no upload equivalent of the
// transferredBytes == 0 && totalBytes > 0 tell that download relies on.
struct UploadOutcome
{
    std::uint64_t nodeHandle = 0; // MegaTransfer::getNodeHandle() of the created node
};
