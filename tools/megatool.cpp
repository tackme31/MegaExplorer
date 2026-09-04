// megatool -- a CLI over IMegaClient, for setting up MEGA-side fixtures without
// driving the GUI, and for answering "which account is the app signed in to?".
// Built for the /evolve loop (CLAUDE.md, "Loop engineering"); useful by hand too.
//
// It authenticates from MEGAEXPLORER_TEST_ACCOUNT / MEGAEXPLORER_TEST_PASSWORD,
// deliberately *not* from the app's saved session, so every mutating command
// hits the test account even when the app happens to be signed in elsewhere.
// `whoami` is the one command that reads the app's session, because comparing
// the two is its whole job.
#include "app/Logging.h"
#include "core/IMegaClient.h"
#include "core/MegaErrorCodes.h"
#include "mega/MegaSdkClient.h"
#include "platform/WindowsSessionStore.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <windows.h> // SetErrorMode: no crash dialog in an unattended run

namespace
{

// Long enough for a fetchNodes on a large account (385s measured on 640k nodes)
// and for the fixture uploads, short enough that a wedged request fails the
// cycle instead of parking it forever.
constexpr std::chrono::minutes kAwaitBudget{10};

// Only the closing logout waits on this. The command it follows has already
// finished, so a MEGA that stopped answering must not park a completed run for
// ten minutes.
constexpr std::chrono::seconds kLogoutBudget{20};

// IMegaClient is callback-based and its callbacks arrive on the SDK's own
// thread, so a console tool needs to block on each one. The app instead marshals
// them onto the GUI thread and never waits -- do not copy this helper into src/.
//
// The wait is bounded on purpose. An unbounded one would turn a stalled SDK
// request into a hang, and a hang is exactly what this tool must not do: nothing
// else runs (there is no event loop), so an unattended cycle would sit there
// until someone killed the process. R is always a Result<T>, so a timeout can be
// reported through the same channel as any other failure.
template<typename R>
R await(const std::function<void(std::function<void(R)>)>& start,
        std::chrono::seconds budget = kAwaitBudget)
{
    struct State
    {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<R> result;
    };
    // Shared rather than captured by reference: the waiter would otherwise be
    // free to return and destroy the condition_variable while the SDK thread is
    // still inside notify_one().
    auto state = std::make_shared<State>();

    start([state](R r) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->result = std::move(r);
        }
        state->ready.notify_one();
    });

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->ready.wait_for(lock, budget, [&] {
            return state->result.has_value();
        }))
    {
        // Safe to walk away: the callback owns its own reference to the state,
        // so a late delivery writes into live storage and is simply ignored.
        return R::fail("timed out after " + std::to_string(budget.count()) + " seconds", 0);
    }
    return std::move(*state->result);
}

// The explicit stop point has to run on every exit path, including the timeout
// above: ~MegaApi otherwise fires every pending request as a failure while the
// stack is already unwinding.
struct ClientStop
{
    std::shared_ptr<MegaSdkClient> client;
    ~ClientStop()
    {
        if (client)
            client->shutdown();
    }
};

// MegaApi::logout is the only call that deletes the SDK's state-cache DB, and
// that DB is named after the session id -- so without this every invocation
// leaves a fresh set behind that nothing will ever reuse, plus a MEGA-side
// session that stays listed. Best-effort: the command's own exit code stands.
struct SessionEnd
{
    std::shared_ptr<MegaSdkClient> client;
    ~SessionEnd()
    {
        if (!client)
            return;
        const Result<void> out = await<Result<void>>(
            [&](auto done) {
                client->logout(std::move(done));
            },
            kLogoutBudget);
        if (!out.success)
            std::fprintf(stderr, "megatool: logout: %s\n", out.errorMessage.c_str());
    }
};

int fail(const std::string& message)
{
    std::fprintf(stderr, "megatool: %s\n", message.c_str());
    return 1;
}

int fail(const std::string& what, const Result<void>& r)
{
    return fail(what + ": " + r.errorMessage + " (code " + std::to_string(r.errorCode) + ")");
}

// qEnvironmentVariable rather than std::getenv: MSVC deprecates the latter and
// this project builds warning-free at /W4.
std::string env(const char* name)
{
    return qEnvironmentVariable(name).toStdString();
}

std::string appDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toStdString();
}

// Its own directory, not the app's: the SDK's state-cache DB is a SQLite file
// and two processes opening the same one is asking for trouble. The cost is a
// full fetchNodes per invocation, which on a test account is seconds.
std::shared_ptr<MegaSdkClient> makeClient()
{
    const QString base = QString::fromStdString(appDataDir()) + "/megatool";
    QDir().mkpath(base);
    return std::make_shared<MegaSdkClient>(QDir::toNativeSeparators(base).toStdString());
}

// The listeners in src/mega call onProgress unconditionally, so an empty
// std::function here throws bad_function_call on the SDK thread and takes the
// process down with a Windows error dialog -- which, unattended, hangs forever
// rather than failing. Never pass nullptr for a progress callback.
const auto kIgnoreProgress = [](std::uint64_t, std::uint64_t) {};

struct Node
{
    std::uint64_t handle = 0;
    bool isRoot = false;
};

const SortOrder kByName{};

std::vector<std::string> splitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char c : path)
    {
        if (c == '/' || c == '\\')
        {
            if (!current.empty())
                parts.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
        parts.push_back(current);

    // "." is the Cloud Drive root. Git Bash rewrites a bare "/" argument into
    // its own install directory before the process ever sees it, so "/" is not
    // usable as "root" from the shell this tool is normally called from.
    parts.erase(std::remove(parts.begin(), parts.end(), "."), parts.end());
    return parts;
}

Result<std::vector<FileEntry>> listChildren(IMegaClient& client, const Node& node)
{
    if (node.isRoot)
    {
        return await<Result<std::vector<FileEntry>>>([&](auto done) {
            client.getRootChildren(kByName, std::move(done));
        });
    }
    return await<Result<std::vector<FileEntry>>>([&](auto done) {
        client.getChildren(node.handle, kByName, std::move(done));
    });
}

// Resolves a "/a/b/c" style path against the Cloud Drive root. Empty or "/"
// is the root itself.
Result<Node> resolve(IMegaClient& client, const std::string& path)
{
    Node node{0, true};
    for (const std::string& part : splitPath(path))
    {
        const Result<std::vector<FileEntry>> children = listChildren(client, node);
        if (!children.success)
            return Result<Node>::fail(children.errorMessage, children.errorCode);

        const FileEntry* match = nullptr;
        for (const FileEntry& entry : children.value())
        {
            if (entry.name == part)
            {
                match = &entry;
                break;
            }
        }
        // kENoEnt so callers can tell "this path is absent" (fine, and expected
        // by `fixture reset`) from "the listing itself failed" (not fine).
        if (!match)
            return Result<Node>::fail("no such path component: " + part, MegaErrorCode::kENoEnt);
        node = Node{match->handle, false};
    }
    return Result<Node>::ok(node);
}

// mkdir -p: every missing component is created, and an existing one is reused.
Result<Node> makeDirs(IMegaClient& client, const std::string& path)
{
    Node node{0, true};
    for (const std::string& part : splitPath(path))
    {
        const Result<std::vector<FileEntry>> children = listChildren(client, node);
        if (!children.success)
            return Result<Node>::fail(children.errorMessage, children.errorCode);

        const FileEntry* match = nullptr;
        for (const FileEntry& entry : children.value())
        {
            if (entry.name == part && entry.isFolder)
            {
                match = &entry;
                break;
            }
        }
        if (match)
        {
            node = Node{match->handle, false};
            continue;
        }

        const Result<void> created = await<Result<void>>([&](auto done) {
            client.createFolder(node.handle, node.isRoot, part, std::move(done));
        });
        if (!created.success)
            return Result<Node>::fail(created.errorMessage, created.errorCode);

        const Result<std::vector<FileEntry>> again = listChildren(client, node);
        if (!again.success)
            return Result<Node>::fail(again.errorMessage, again.errorCode);

        // Failing here rather than carrying on: node creation is eventually
        // consistent, so a re-list that lands too early would otherwise leave
        // `node` on the parent and quietly build the rest of the path one level
        // too high -- "a/b" becoming "b", reported as success.
        const FileEntry* created2 = nullptr;
        for (const FileEntry& entry : again.value())
        {
            if (entry.name == part && entry.isFolder)
            {
                created2 = &entry;
                break;
            }
        }
        if (!created2)
            return Result<Node>::fail("created " + part + " but it is not in the listing yet",
                                      MegaErrorCode::kEAgain);
        node = Node{created2->handle, false};
    }
    return Result<Node>::ok(node);
}

// Logs in from the environment and fetches the tree. Everything except whoami
// starts here.
int signIn(IMegaClient& client)
{
    const std::string email = env("MEGAEXPLORER_TEST_ACCOUNT");
    const std::string password = env("MEGAEXPLORER_TEST_PASSWORD");
    if (email.empty())
        return fail("MEGAEXPLORER_TEST_ACCOUNT is not set (.claude/settings.local.json env block)");
    if (password.empty())
        return fail("MEGAEXPLORER_TEST_PASSWORD is not set (Windows user environment variable; a "
                    "new value needs a fresh session to be visible)");

    // Never echo the password, not even at a debug level -- MegaSdkLogger sends
    // whatever it is given to the shared log file.
    const Result<void> login = await<Result<void>>([&](auto done) {
        client.login(email, password, std::move(done));
    });
    if (!login.success)
        return fail("login as " + email, login);

    const Result<void> fetched = await<Result<void>>([&](auto done) {
        client.fetchNodes(kIgnoreProgress, std::move(done));
    });
    if (!fetched.success)
        return fail("fetchNodes", fetched);
    return 0;
}

int cmdWhoami()
{
    const std::string expected = env("MEGAEXPLORER_TEST_ACCOUNT");
    std::printf("env    : %s\n", expected.empty() ? "(unset)" : expected.c_str());

    WindowsSessionStore store(appDataDir() + "/session.dat");
    const Result<std::string> token = store.loadSession();
    // An absent file is a *successful* empty read, not a failure -- so testing
    // only token.success would send "" to fastLogin and turn "the app was never
    // signed in" into an opaque SDK error a round-trip later.
    if (!token.success || token.value().empty())
    {
        std::printf("session: (none)\n");
        std::printf(
            "match  : no -- the app has no saved session; sign it in to the test account\n");
        return 1;
    }

    auto client = makeClient();
    // No logout() anywhere here: that would invalidate the very session the app
    // is holding. Only the stop point.
    const ClientStop stop{client};

    const Result<void> login = await<Result<void>>([&](auto done) {
        client->loginWithSession(token.value(), std::move(done));
    });
    if (!login.success)
    {
        std::printf("session: (unusable)\n");
        std::printf("match  : no -- %s\n", login.errorMessage.c_str());
        return 1;
    }

    // fetchNodes before asking who we are: MegaApi::getMyEmail() returns null
    // after a fastLogin until the tree has been fetched, so skipping this reports
    // "Not logged in" for a perfectly good session.
    const Result<void> fetched = await<Result<void>>([&](auto done) {
        client->fetchNodes(kIgnoreProgress, std::move(done));
    });

    const Result<AccountIdentity> identity = client->currentAccountIdentity();
    if (!fetched.success)
    {
        std::printf("session: (unreadable)\n");
        std::printf("match  : no -- fetchNodes: %s\n", fetched.errorMessage.c_str());
        return 1;
    }
    if (!identity.success)
    {
        std::printf("session: (unknown)\n");
        std::printf("match  : no -- %s\n", identity.errorMessage.c_str());
        return 1;
    }

    const std::string actual = identity.value().email;
    std::printf("session: %s\n", actual.c_str());
    const bool matches = !expected.empty() && actual == expected;
    std::printf("match  : %s\n", matches ? "yes" : "no");
    if (!matches)
        std::printf("         sign the app out and back in as %s before running anything that "
                    "touches MEGA\n",
                    expected.empty() ? "the test account" : expected.c_str());
    return matches ? 0 : 1;
}

int cmdLs(IMegaClient& client, const std::string& path)
{
    const Result<Node> node = resolve(client, path);
    if (!node.success)
        return fail("resolve " + path + ": " + node.errorMessage);

    const Result<std::vector<FileEntry>> children = listChildren(client, node.value());
    if (!children.success)
        return fail("list " + path + ": " + children.errorMessage);

    for (const FileEntry& entry : children.value())
    {
        std::printf("%s %10llu %s%s\n",
                    entry.isFolder ? "d" : "-",
                    static_cast<unsigned long long>(entry.sizeBytes),
                    entry.name.c_str(),
                    entry.isFavourite ? " *" : "");
    }
    return 0;
}

// Fetches a node's bytes back over the SDK's local HTTP server -- deliberately
// not over readFileRange, because what has to be proved here is that *that
// server* is up and answers a range: it is the URL the in-app viewers will be
// pointed at.
//
// The one place in this tool that runs an event loop. QNetworkAccessManager has
// no blocking form, and await() cannot stand in: it parks the calling thread,
// which is the very thread this reply is delivered on.
int cmdStream(IMegaClient& client,
              const std::string& path,
              std::uint64_t offset,
              std::uint64_t length)
{
    const Result<Node> node = resolve(client, path);
    if (!node.success)
        return fail("resolve " + path + ": " + node.errorMessage);
    if (node.value().isRoot)
        return fail("stream needs a file, not the Cloud Drive root");

    const Result<std::string> url = client.streamingUrl(node.value().handle);
    if (!url.success)
        return fail("streamingUrl " + path + ": " + url.errorMessage);
    std::printf("url    : %s\n", url.value().c_str());

    QNetworkRequest request{QUrl(QString::fromStdString(url.value()))};
    if (length > 0)
    {
        // Inclusive on both ends, per RFC 9110; length 0 means "no Range header",
        // i.e. the whole node.
        request.setRawHeader("Range",
                             QByteArray("bytes=")
                                 + QByteArray::number(static_cast<qulonglong>(offset)) + '-'
                                 + QByteArray::number(static_cast<qulonglong>(offset + length - 1)));
    }

    QNetworkAccessManager nam;
    const std::unique_ptr<QNetworkReply> reply(nam.get(request));
    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kAwaitBudget, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished())
        return fail("the local HTTP server did not answer within the budget");
    if (reply->error() != QNetworkReply::NoError)
        return fail("GET failed: " + reply->errorString().toStdString());

    const QByteArray body = reply->readAll();
    std::printf("status : %d\n",
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    std::printf("range  : %s\n", reply->rawHeader("Content-Range").constData());
    std::printf("bytes  : %lld\n", static_cast<long long>(body.size()));
    std::printf("head   : %s\n", body.left(16).toHex(' ').constData());
    return 0;
}

int cmdMkdir(IMegaClient& client, const std::string& path)
{
    const Result<Node> node = makeDirs(client, path);
    if (!node.success)
        return fail("mkdir " + path + ": " + node.errorMessage);
    std::printf("created %s\n", path.c_str());
    return 0;
}

int cmdPut(IMegaClient& client, const std::string& localPath, const std::string& remoteDir)
{
    const Result<Node> parent = makeDirs(client, remoteDir);
    if (!parent.success)
        return fail("mkdir " + remoteDir + ": " + parent.errorMessage);

    const QString native =
        QDir::toNativeSeparators(QFileInfo(QString::fromStdString(localPath)).absoluteFilePath());
    const Result<UploadOutcome> uploaded = await<Result<UploadOutcome>>([&](auto done) {
        client.upload(native.toStdString(),
                      parent.value().handle,
                      parent.value().isRoot,
                      /*transferId*/ 1, // one transfer per run, and nothing here cancels
                      kIgnoreProgress,
                      std::move(done));
    });
    if (!uploaded.success)
        return fail("upload " + localPath + ": " + uploaded.errorMessage);
    std::printf("uploaded %s -> %s\n", localPath.c_str(), remoteDir.c_str());
    return 0;
}

int cmdRm(IMegaClient& client, const std::string& path)
{
    const Result<Node> node = resolve(client, path);
    if (!node.success)
        return fail("resolve " + path + ": " + node.errorMessage);
    if (node.value().isRoot)
        return fail("refusing to remove the Cloud Drive root");

    // Moves to the Rubbish bin: IMegaClient deliberately does not expose
    // MegaApi::remove, and this tool is not a reason to add it.
    const Result<void> removed = await<Result<void>>([&](auto done) {
        client.moveToRubbish(node.value().handle, std::move(done));
    });
    if (!removed.success)
        return fail("rm " + path, removed);
    std::printf("moved to rubbish: %s\n", path.c_str());
    return 0;
}

int cmdMv(IMegaClient& client, const std::string& path, const std::string& destDir)
{
    const Result<Node> node = resolve(client, path);
    if (!node.success)
        return fail("resolve " + path + ": " + node.errorMessage);
    if (node.value().isRoot)
        return fail("refusing to move the Cloud Drive root");

    // resolve, not makeDirs: a typo in the destination should fail rather than
    // quietly create that folder and report a move that went somewhere else.
    const Result<Node> parent = resolve(client, destDir);
    if (!parent.success)
        return fail("resolve " + destDir + ": " + parent.errorMessage);

    // No name check of any kind, deliberately. MEGA allows same-name siblings and
    // moveNode sends no overwrite hint, so landing on a name the destination
    // already holds is precisely what this command exists to make reachable --
    // every path in the app diverts to a conflict dialog before that point.
    const Result<void> moved = await<Result<void>>([&](auto done) {
        client.moveNode(node.value().handle,
                        parent.value().handle,
                        parent.value().isRoot,
                        std::string(),
                        std::move(done));
    });
    if (!moved.success)
        return fail("mv " + path, moved);
    std::printf("moved %s -> %s\n", path.c_str(), destDir.c_str());
    return 0;
}

// The known tree every fixture-dependent check starts from. Kept small on
// purpose -- it is rebuilt from scratch each time, and each entry exists to
// cover one listing case.
int cmdFixtureReset(IMegaClient& client)
{
    const std::string root = "MegaExplorerFixture";

    const Result<Node> existing = resolve(client, root);
    // Only kENoEnt means "not there yet". Any other failure is a listing error,
    // and reading it as absence would leave the previous run's tree in place
    // while still reporting a reset -- the one thing this command must not do.
    if (!existing.success && existing.errorCode != MegaErrorCode::kENoEnt)
        return fail("checking for an existing fixture: " + existing.errorMessage);
    if (existing.success && !existing.value().isRoot)
    {
        const Result<void> removed = await<Result<void>>([&](auto done) {
            client.moveToRubbish(existing.value().handle, std::move(done));
        });
        if (!removed.success)
            return fail("clearing the old fixture", removed);
    }

    for (const char* dir : {"MegaExplorerFixture/docs",
                            "MegaExplorerFixture/images",
                            "MegaExplorerFixture/empty",
                            "MegaExplorerFixture/nested/a/b"})
    {
        const Result<Node> made = makeDirs(client, dir);
        if (!made.success)
            return fail(std::string("mkdir ") + dir + ": " + made.errorMessage);
    }

    // Written locally first because upload() is the only way to create a file.
    const QString scratch = QDir::tempPath() + "/megatool-fixture";
    if (!QDir().mkpath(scratch))
        return fail("cannot create " + scratch.toStdString());
    for (const auto& [name, body] : {std::pair<const char*, const char*>{"readme.txt", "fixture\n"},
                                     {"notes.txt", "second file\n"},
                                     {"deep.txt", "in nested/a/b\n"}})
    {
        const QString local = scratch + "/" + name;
        QFile file(local);
        if (!file.open(QIODevice::WriteOnly))
            return fail("cannot write " + local.toStdString());
        // Checked, and flushed before the upload reads it back: a short write
        // would otherwise upload cleanly and hand every later check bad data.
        const qint64 length = static_cast<qint64>(std::strlen(body));
        if (file.write(body, length) != length || !file.flush())
            return fail("short write to " + local.toStdString());
    }

    if (const int rc =
            cmdPut(client, (scratch + "/readme.txt").toStdString(), "MegaExplorerFixture/docs"))
        return rc;
    if (const int rc =
            cmdPut(client, (scratch + "/notes.txt").toStdString(), "MegaExplorerFixture/docs"))
        return rc;
    if (const int rc =
            cmdPut(client, (scratch + "/deep.txt").toStdString(), "MegaExplorerFixture/nested/a/b"))
        return rc;

    // Hard failure, not a skip: the file was uploaded moments ago, so failing to
    // resolve it means something real went wrong -- and this is the one entry
    // that exists to cover the favourite case, so losing it silently would leave
    // a fixture that looks complete and isn't.
    const Result<Node> favourite = resolve(client, "MegaExplorerFixture/docs/readme.txt");
    if (!favourite.success)
        return fail("locating readme.txt to mark it favourite: " + favourite.errorMessage);
    const Result<void> marked = await<Result<void>>([&](auto done) {
        client.setNodeFavourite(favourite.value().handle, true, std::move(done));
    });
    if (!marked.success)
        return fail("marking readme.txt favourite", marked);

    std::printf("fixture reset under /%s\n", root.c_str());
    return 0;
}

void usage()
{
    std::fprintf(stderr,
                 "usage: megatool <command>\n"
                 "  whoami                  which account the *app's saved session* belongs to,\n"
                 "                          compared against MEGAEXPLORER_TEST_ACCOUNT\n"
                 "                          (exit 0 only when they match)\n"
                 "  ls <path>               list a folder ('.' for the Cloud Drive root -- Git\n"
                 "                          Bash rewrites a bare '/' before we see it)\n"
                 "  stream <path> [off [len]]\n"
                 "                          read a file back over the SDK's local HTTP server,\n"
                 "                          without it touching the disk; off/len (bytes) ask\n"
                 "                          for a range, and no len means the whole file\n"
                 "  mkdir <path>            create a folder, parents included\n"
                 "  put <local> <path>      upload one local file into a folder\n"
                 "  mv <path> <folder>      move a node into a folder, taken name or not\n"
                 "  rm <path>               move a node to the Rubbish bin\n"
                 "  fixture reset           rebuild the known test tree under "
                 "/MegaExplorerFixture\n"
                 "\n"
                 "Every command except whoami signs in from MEGAEXPLORER_TEST_ACCOUNT /\n"
                 "MEGAEXPLORER_TEST_PASSWORD, never from the app's session.\n");
}

} // namespace

int main(int argc, char* argv[])
{
    // Unattended: a Windows Error Reporting dialog does not fail, it *waits*, so
    // a crash here would hang an /evolve cycle until someone clicks it away.
    // Turn crashes into an exit code instead.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::set_terminate([] {
        std::fputs("megatool: terminated by an unhandled exception\n", stderr);
        std::_Exit(3);
    });

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("MegaExplorer");
    QCoreApplication::setApplicationName("MegaExplorer");

    // Deliberately *not* installLogging(): it opens MegaExplorer.log WriteOnly,
    // so running this tool would truncate the app's log. Qt's default handler
    // sends what is left to stderr, which is where a console tool wants it.
    // src/app/Logging.cpp is still linked in -- MegaSdkClient and MegaSdkLogger
    // reference its logging categories.
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=false"));

    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
    {
        usage();
        return 2;
    }

    const std::string& command = args[0];
    if (command == "whoami")
        return cmdWhoami();

    auto client = makeClient();
    const ClientStop stop{client};
    // After ClientStop so it runs first -- the logout needs the SDK thread that
    // shutdown() stops. Before signIn so a login that got half-way is cleaned up
    // too; logout on a client that never logged in only drops the local caches.
    const SessionEnd sessionEnd{client};

    if (const int rc = signIn(*client))
        return rc;

    // An empty destination silently means "the root", so `put file ""` would
    // upload into the account root and `mkdir ""` would report success having
    // done nothing. An empty shell variable is exactly how that arises, so the
    // mutating commands reject it. `ls` keeps the root as its default.
    const bool emptyTarget = args.size() > 1 && args[1].empty();

    int rc = 2;
    if (command == "ls")
        rc = cmdLs(*client, args.size() > 1 ? args[1] : std::string());
    else if (command == "stream" && args.size() > 1 && !emptyTarget)
        rc = cmdStream(*client,
                       args[1],
                       args.size() > 2 ? std::strtoull(args[2].c_str(), nullptr, 10) : 0,
                       args.size() > 3 ? std::strtoull(args[3].c_str(), nullptr, 10) : 0);
    else if (command == "mkdir" && args.size() > 1 && !emptyTarget)
        rc = cmdMkdir(*client, args[1]);
    else if (command == "put" && args.size() > 2 && !args[2].empty())
        rc = cmdPut(*client, args[1], args[2]);
    else if (command == "mv" && args.size() > 2 && !emptyTarget && !args[2].empty())
        rc = cmdMv(*client, args[1], args[2]);
    else if (command == "rm" && args.size() > 1 && !emptyTarget)
        rc = cmdRm(*client, args[1]);
    else if (command == "fixture" && args.size() > 1 && args[1] == "reset")
        rc = cmdFixtureReset(*client);
    else
        usage();

    return rc;
}
