#pragma once
#include <string>

// Success payload for IMegaClient::download's completion callback.
struct DownloadOutcome
{
    std::string localPath;       // actual saved path; may differ from the requested
                                 // destinationPath if a name collision caused the SDK
                                 // to rename the saved file (see IMegaClient::download)
    bool alreadyPresent = false; // true if a local file with a matching fingerprint
                                 // already existed at destinationPath, so the SDK
                                 // skipped the transfer entirely (no bytes written)
                                 // instead of downloading/renaming/overwriting
};
