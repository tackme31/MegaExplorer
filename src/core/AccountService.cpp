#include "AccountService.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{

// First/last name are free text the user typed, so a value that is only
// whitespace must not become a leading or trailing space in the join.
std::string trimmed(const std::string& value)
{
    const auto isNotSpace = [](unsigned char c) {
        return std::isspace(c) == 0;
    };
    const auto begin = std::find_if(value.begin(), value.end(), isNotSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), isNotSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string joinName(const std::string& firstName, const std::string& lastName)
{
    const std::string first = trimmed(firstName);
    const std::string last = trimmed(lastName);
    if (first.empty())
        return last;
    if (last.empty())
        return first;
    return first + " " + last;
}

} // namespace

AccountService::AccountService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client)) {}

Result<AccountIdentity> AccountService::identity() const
{
    return mClient->currentAccountIdentity();
}

void AccountService::loadDisplayName(std::function<void(std::string)> onDone)
{
    mClient->getMyUserAttribute(
        UserAttribute::FirstName,
        [this, onDone = std::move(onDone)](Result<std::string> first) mutable {
            // An unset attribute fails rather than returning "", so both
            // branches feed the join the same way: whatever arrived, or
            // nothing.
            std::string firstName = first.success ? std::move(first.value()) : std::string();
            mClient->getMyUserAttribute(
                UserAttribute::LastName,
                [firstName = std::move(firstName),
                 onDone = std::move(onDone)](Result<std::string> last) {
                    onDone(joinName(firstName, last.success ? last.value() : std::string()));
                });
        });
}

void AccountService::loadAvatar(const std::string& destinationPath,
                                std::function<void(Result<AvatarOutcome>)> onDone)
{
    mClient->getMyAvatar(destinationPath, [onDone = std::move(onDone)](Result<std::string> result) {
        AvatarOutcome outcome;
        if (result.success)
        {
            outcome.localPath = std::move(result.value());
            outcome.hasAvatar = true;
        }
        else
        {
            outcome.errorCode = result.errorCode;
            outcome.errorMessage = std::move(result.errorMessage);
        }
        onDone(Result<AvatarOutcome>::ok(std::move(outcome)));
    });
}

void AccountService::loadAccountInfo(std::function<void(Result<AccountInfo>)> onDone)
{
    mClient->getAccountInfo(std::move(onDone));
}
