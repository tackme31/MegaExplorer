import QtQuick
import QtTest
import MegaExplorer

// R4-5. The point of these tests is the wording: describeReason/describeError/
// describeOperation/describeDownload turn a context enum plus a couple of
// counts into the sentence the user reads, and a wrong branch produces a wrong
// sentence with no error raised anywhere. C++ hands this component enums only
// (R3-4/R3-5), so the mapping lives here and nothing else checks it.
//
// The describe* functions were split out of the show* ones for these tests;
// show* is covered only far enough to prove the two halves are still wired
// together, since the ListModel behind push() is private to the component.
TestCase {
    id: testCase
    name: "ToastStack"

    Component {
        id: toastComponent
        ToastStack {}
    }

    function makeToast() {
        return createTemporaryObject(toastComponent, testCase);
    }

    // ---- describeReason: the reason half of a message ----------------------

    function test_describeReason_data() {
        return [
                    {
                        tag: "notFound",
                        reason: NotificationController.NotFound,
                        raw: "",
                        expected: "Failed to rename — it no longer exists"
                    },
                    {
                        tag: "noPermission",
                        reason: NotificationController.NoPermission,
                        raw: "",
                        expected: "Failed to rename — you don't have permission"
                    },
                    {
                        tag: "offline",
                        reason: NotificationController.Offline,
                        raw: "",
                        expected: "Failed to rename — check your connection"
                    },
                    // Unknown is the only reason that lets a server string through, and
                    // only when there is one to show.
                    {
                        tag: "unknownWithRawMessage",
                        reason: NotificationController.Unknown,
                        raw: "Internal error",
                        expected: "Failed to rename: Internal error"
                    },
                    {
                        tag: "unknownWithoutRawMessage",
                        reason: NotificationController.Unknown,
                        raw: "",
                        expected: "Failed to rename"
                    },
                    // R3-1 added kEInternal = -1, which classifies to nothing. An
                    // unmapped value has to reach the clause-only branch rather than
                    // fall through to a blank message.
                    {
                        tag: "unmappedReasonValue",
                        reason: -1,
                        raw: "",
                        expected: "Failed to rename"
                    }
                ];
    }

    function test_describeReason(data) {
        const toast = makeToast();
        compare(toast.describeReason(qsTr("Failed to rename"), data.reason, data.raw),
                data.expected);
    }

    // ---- describeError: context -> clause ---------------------------------

    // The ten contexts that had an errorCode to classify, so the reason gets
    // appended. One row each is enough: the reason half is covered above.
    function test_describeError_classified_data() {
        return [
                    {
                        tag: "navigation",
                        context: "navigation",
                        expected: "Failed to load folder — check your connection"
                    },
                    {
                        tag: "search",
                        context: "search",
                        expected: "Search failed — check your connection"
                    },
                    {
                        tag: "thumbnail",
                        context: "thumbnail",
                        expected: "Failed to load thumbnail — check your connection"
                    },
                    {
                        tag: "rename",
                        context: "rename",
                        expected: "Failed to rename — check your connection"
                    },
                    {
                        tag: "createFolder",
                        context: "createFolder",
                        expected: "Failed to create folder — check your connection"
                    },
                    {
                        tag: "addFavourite",
                        context: "addFavourite",
                        expected: "Failed to add to Favourites — check your connection"
                    },
                    {
                        tag: "removeFavourite",
                        context: "removeFavourite",
                        expected: "Failed to remove from Favourites — check your connection"
                    },
                    {
                        tag: "paste",
                        context: "paste",
                        expected: "Can't paste here — check your connection"
                    },
                    {
                        tag: "copy",
                        context: "copy",
                        expected: "Can't copy here — check your connection"
                    },
                    // Missing until R3-5, when it fell to default: and put a bare
                    // untranslated "Internal error" on screen.
                    {
                        tag: "refresh",
                        context: "refresh",
                        expected: "Couldn't refresh this folder — check your connection"
                    }
                ];
    }

    function test_describeError_classified(data) {
        const toast = makeToast();
        compare(toast.describeError(data.context, NotificationController.Offline, ""),
                data.expected);
    }

    // The six contexts whose failure never reached the SDK, so there is no code
    // to classify and the sentence is fixed. Each row is checked twice with
    // different reason/rawMessage: ignoring both is the contract, and a
    // regression would show up as a reason suddenly being appended.
    function test_describeError_fixed_data() {
        return [
                    {
                        tag: "openFile",
                        context: "openFile",
                        expected: "Couldn't open this file"
                    },
                    {
                        tag: "renameInvalidName",
                        context: "renameInvalidName",
                        expected: "That name can't be used — names can't be empty or contain \\ or /"
                    },
                    {
                        tag: "quickAccessSave",
                        context: "quickAccessSave",
                        expected: "Couldn't save your pinned folders"
                    },
                    {
                        tag: "quickAccessUnavailable",
                        context: "quickAccessUnavailable",
                        expected: "Couldn't check this folder right now — please try again"
                    },
                    {
                        tag: "uploadNothingToUpload",
                        context: "uploadNothingToUpload",
                        expected: "Nothing to upload — folders and non-file items can't be uploaded"
                    },
                    {
                        tag: "uploadReplaceFailed",
                        context: "uploadReplaceFailed",
                        expected: "Uploaded, but some of the files being replaced could not be removed"
                    }
                ];
    }

    function test_describeError_fixed(data) {
        const toast = makeToast();
        compare(toast.describeError(data.context, NotificationController.Unknown, ""),
                data.expected);
        compare(toast.describeError(data.context, NotificationController.NotFound, "ignored"),
                data.expected);
    }

    // Fixed too, but the sentence carries the cap, which is injected rather than
    // written here -- the number has one home, UploadController::kMaxFilesPerUpload.
    function test_describeError_tooManyFilesNamesTheInjectedCap() {
        const toast = makeToast();
        toast.maxFilesPerUpload = 100;
        compare(toast.describeError("uploadTooManyFiles", NotificationController.Unknown, ""),
                "Too many files — upload at most 100 at a time");
        compare(toast.describeError("uploadTooManyFiles", NotificationController.NotFound,
                                    "ignored"), "Too many files — upload at most 100 at a time");
    }

    // A context C++ emits that nothing here handles. R3-5 turned this from
    // "print the raw English" into "warn and say something generic"; the warning
    // is the half that makes the gap findable, so it is part of the contract.
    function test_describeError_unknownContextWarnsAndFallsBack() {
        const toast = makeToast();
        ignoreWarning("ToastStack: no case for error context bogus");
        compare(toast.describeError("bogus", NotificationController.NotFound, ""),
                "Something went wrong — it no longer exists");
    }

    // ---- describeOperation: counts -> tally --------------------------------

    function test_describeOperation_data() {
        return [
                    {
                        tag: "move/all",
                        context: "move",
                        ok: 3,
                        failed: 0,
                        expected: "Moved 3 item(s)"
                    },
                    {
                        tag: "move/none",
                        context: "move",
                        ok: 0,
                        failed: 3,
                        expected: "Failed to move 3 item(s)"
                    },
                    {
                        tag: "move/partial",
                        context: "move",
                        ok: 2,
                        failed: 1,
                        expected: "Moved 2 item(s), 1 failed"
                    },
                    {
                        tag: "copy/all",
                        context: "copy",
                        ok: 3,
                        failed: 0,
                        expected: "Copied 3 item(s)"
                    },
                    {
                        tag: "copy/none",
                        context: "copy",
                        ok: 0,
                        failed: 3,
                        expected: "Failed to copy 3 item(s)"
                    },
                    {
                        tag: "copy/partial",
                        context: "copy",
                        ok: 2,
                        failed: 1,
                        expected: "Copied 2 item(s), 1 failed"
                    },
                    {
                        tag: "rubbish/all",
                        context: "moveToRubbish",
                        ok: 3,
                        failed: 0,
                        expected: "Moved 3 item(s) to the Rubbish bin"
                    },
                    {
                        tag: "rubbish/none",
                        context: "moveToRubbish",
                        ok: 0,
                        failed: 3,
                        expected: "Failed to move 3 item(s) to the Rubbish bin"
                    },
                    {
                        tag: "rubbish/partial",
                        context: "moveToRubbish",
                        ok: 2,
                        failed: 1,
                        expected: "Moved 2 item(s) to the Rubbish bin, 1 failed"
                    },
                    // "file(s)", not "item(s)": only files are uploadable.
                    {
                        tag: "upload/all",
                        context: "upload",
                        ok: 3,
                        failed: 0,
                        expected: "Uploaded 3 file(s)"
                    },
                    {
                        tag: "upload/none",
                        context: "upload",
                        ok: 0,
                        failed: 3,
                        expected: "Failed to upload 3 file(s)"
                    },
                    {
                        tag: "upload/partial",
                        context: "upload",
                        ok: 2,
                        failed: 1,
                        expected: "Uploaded 2 file(s), 1 failed"
                    },
                    // Counts carry nothing here -- the reason is the whole message.
                    {
                        tag: "uploadDestinationGone",
                        context: "uploadDestinationGone",
                        ok: 0,
                        failed: 5,
                        expected: "The upload destination folder no longer exists"
                    },
                    {
                        tag: "createFolder",
                        context: "createFolder",
                        ok: 1,
                        failed: 0,
                        expected: "Folder created"
                    }
                ];
    }

    function test_describeOperation(data) {
        const toast = makeToast();
        compare(toast.describeOperation(data.context, data.ok, data.failed), data.expected);
    }

    // An empty batch takes the failed === 0 branch and reports a zero tally
    // rather than nothing. Recorded because it reads like an oversight and is
    // not one: C++ does not emit operationFinished for an empty selection.
    function test_describeOperation_emptyBatchStillTallies() {
        const toast = makeToast();
        compare(toast.describeOperation("move", 0, 0), "Moved 0 item(s)");
    }

    // The one branch that deliberately stays silent instead of warning, unlike
    // describeError's default: this list is closed on the C++ side.
    function test_describeOperation_unknownContextIsEmpty() {
        const toast = makeToast();
        compare(toast.describeOperation("bogus", 1, 0), "");
    }

    // ---- describeDownload --------------------------------------------------

    function test_describeDownload_data() {
        return [
                    // Names the file and nothing else -- the SDK's reason is
                    // untranslated English and stays in the log (R5-10).
                    {
                        tag: "failure",
                        success: false,
                        already: false,
                        expected: "Couldn't download a.txt"
                    },
                    // Without this wording a skipped transfer is indistinguishable from
                    // an overwrite.
                    {
                        tag: "alreadyPresent",
                        success: true,
                        already: true,
                        expected: "a.txt is already downloaded"
                    },
                    {
                        tag: "downloaded",
                        success: true,
                        already: false,
                        expected: "a.txt downloaded"
                    }
                ];
    }

    function test_describeDownload(data) {
        const toast = makeToast();
        compare(toast.describeDownload(data.success, "a.txt", data.already), data.expected);
    }

    // alreadyPresent is only consulted on success -- a failed transfer reports
    // the error either way.
    function test_describeDownload_failureIgnoresAlreadyPresent() {
        const toast = makeToast();
        compare(toast.describeDownload(false, "a.txt", true), "Couldn't download a.txt");
    }

    // ---- show*: only that composing and pushing are still connected --------

    function test_showError_pushes() {
        const toast = makeToast();
        const before = toast.nextSeq;
        toast.showError("rename", NotificationController.NotFound, "");
        compare(toast.nextSeq, before + 1);
    }

    function test_showOperation_pushesWhenDescribed() {
        const toast = makeToast();
        const before = toast.nextSeq;
        toast.showOperation("move", 1, 0);
        compare(toast.nextSeq, before + 1);
    }

    // The empty string from describeOperation has to stop the push, not produce
    // a blank toast.
    function test_showOperation_silentForUnknownContext() {
        const toast = makeToast();
        const before = toast.nextSeq;
        toast.showOperation("bogus", 1, 0);
        compare(toast.nextSeq, before);
    }

    function test_showDownload_pushes() {
        const toast = makeToast();
        const before = toast.nextSeq;
        toast.showDownload(true, "a.txt", "C:/tmp/a.txt", false);
        compare(toast.nextSeq, before + 1);
    }
}
