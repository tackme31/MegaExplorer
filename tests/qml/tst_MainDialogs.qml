import QtQuick
import QtTest
import MegaExplorer

// R6-4. The dialogs Main.qml used to declare inline and now instantiates
// from qml/components/: NameConflictDialog, MissingPinDialog. Both
// wire themselves to their controller's signal, so nothing outside
// them opens them -- which means every failure mode here is silent. A mistyped
// handler name makes the dialog simply never appear; Replace and Skip swapped
// makes the app overwrite the files the user asked to keep. Screenshots cannot
// see a closed dialog, so this file is the only net.
//
// The controllers arrive as required properties rather than being read as
// context properties, which is the whole reason these are testable: the runner
// is QUICK_TEST_MAIN with no engine hook, so main.cpp's context properties do
// not exist here (see QmlTestMain.cpp).
//
// Not covered: SignOutDialog. It holds no state and its only logic is a title
// that flips on downloadActive/uploadActive -- a binding a screenshot can
// adjudicate, unlike anything below.
TestCase {
    id: testCase
    name: "MainDialogs"

    Component {
        id: nameConflictComponent
        NameConflictDialog {}
    }

    Component {
        id: confirmUploadComponent
        ConfirmUploadDialog {}
    }

    Component {
        id: missingPinComponent
        MissingPinDialog {}
    }

    // A QtObject, not a JS object literal: Connections.target is typed QObject*,
    // so a plain JS object lands there as null, the dialog never opens and the
    // tests that assert on what it called would pass on their own emptiness.
    // The signal signatures must match src/qml/UploadController.h's, since a
    // mismatch is exactly what this file exists to catch.
    Component {
        id: uploadsComponent

        QtObject {
            id: fakeUploads

            signal nameConflictRequiresConfirmation(var filePaths, var conflictNames,
                                                    var destinationHandle, bool destinationIsRoot)
            signal uploadRequiresConfirmation(var filePaths, int fileCount,
                                              var destinationHandle, bool destinationIsRoot)

            property int confirmedCount: 0
            property int uploadFilesCount: 0
            property int replacingCount: 0
            property int skippingCount: 0
            property var lastCall: null

            function uploadFiles(paths, handle, isRoot) {
                fakeUploads.uploadFilesCount += 1;
                fakeUploads.lastCall = {
                    "paths": paths,
                    "handle": handle,
                    "isRoot": isRoot
                };
            }

            function uploadConfirmed(paths, handle, isRoot) {
                fakeUploads.confirmedCount += 1;
                fakeUploads.lastCall = {
                    "paths": paths,
                    "handle": handle,
                    "isRoot": isRoot
                };
            }

            function uploadReplacingExisting(paths, handle, isRoot) {
                fakeUploads.replacingCount += 1;
                fakeUploads.lastCall = {
                    "paths": paths,
                    "handle": handle,
                    "isRoot": isRoot
                };
            }

            function uploadSkippingExisting(paths, handle, isRoot) {
                fakeUploads.skippingCount += 1;
                fakeUploads.lastCall = {
                    "paths": paths,
                    "handle": handle,
                    "isRoot": isRoot
                };
            }
        }
    }

    Component {
        id: quickAccessComponent

        QtObject {
            id: fakeQuickAccess

            signal missing(var handle, string name)

            property int unpinCount: 0
            property var lastUnpin: null

            function unpin(handle) {
                fakeQuickAccess.unpinCount += 1;
                fakeQuickAccess.lastUnpin = handle;
            }
        }
    }

    function makeUploads() {
        const uploads = createTemporaryObject(uploadsComponent, testCase);
        verify(uploads !== null);
        return uploads;
    }

    function makeQuickAccess() {
        const quickAccess = createTemporaryObject(quickAccessComponent, testCase);
        verify(quickAccess !== null);
        return quickAccess;
    }

    function makeDialog(component, props) {
        const dialog = createTemporaryObject(component, testCase, props);
        // A required property left out returns null here, and every later
        // failure would then be an opaque null dereference instead of this line.
        verify(dialog !== null);
        return dialog;
    }

    // DialogButtonBox reorders its buttons by role to match platform
    // convention, so an index would be asserting the platform's preference
    // rather than ours. Text is what the user actually clicks.
    function buttonNamed(dialog, label) {
        const box = dialog.footer;
        verify(box !== null);
        for (var i = 0; i < box.count; ++i) {
            if (box.itemAt(i).text === label)
                return box.itemAt(i);
        }
        fail("no button labelled " + label);
    }

    // ---- NameConflictDialog --------------------------------------------

    function test_nameConflict_signalOpensAndCarriesTheDestination() {
        failOnWarning(/Connections/);
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, true);

        tryCompare(dialog, "opened", true);
        compare(dialog.filePaths, ["a.txt"]);
        compare(dialog.conflictNames, ["a.txt"]);
        compare(dialog.destinationHandle, 0);
        compare(dialog.destinationIsRoot, true);
    }

    // ---- ConfirmUploadDialog -------------------------------------------

    function test_confirmUpload_signalOpensAndCarriesTheDestination() {
        failOnWarning(/Connections/);
        const uploads = makeUploads();
        const dialog = makeDialog(confirmUploadComponent, {
                                      "uploads": uploads
                                  });

        uploads.uploadRequiresConfirmation(["a.txt", "b.txt"], 7, 42, false);

        tryCompare(dialog, "opened", true);
        // The count is the recursive one, so it is carried rather than derived
        // from filePaths -- a dropped folder makes the two differ.
        compare(dialog.fileCount, 7);
        compare(dialog.filePaths, ["a.txt", "b.txt"]);
        compare(dialog.destinationHandle, 42);
        compare(dialog.destinationIsRoot, false);
    }

    function test_confirmUpload_okConfirmsWithTheSamePathsAndDestination() {
        const uploads = makeUploads();
        const dialog = makeDialog(confirmUploadComponent, {
                                      "uploads": uploads
                                  });

        uploads.uploadRequiresConfirmation(["a.txt", "b.txt"], 2, 42, false);
        dialog.accept();

        compare(uploads.confirmedCount, 1);
        compare(uploads.lastCall.paths, ["a.txt", "b.txt"]);
        compare(uploads.lastCall.handle, 42);
        compare(uploads.lastCall.isRoot, false);
        tryCompare(dialog, "visible", false);
    }

    function test_confirmUpload_cancelUploadsNothing() {
        // Cancel is the whole point of the dialog: it must not fall through to
        // the upload the way a mis-wired reject role would.
        const uploads = makeUploads();
        const dialog = makeDialog(confirmUploadComponent, {
                                      "uploads": uploads
                                  });

        uploads.uploadRequiresConfirmation(["a.txt", "b.txt"], 2, 42, false);
        dialog.reject();

        compare(uploads.confirmedCount, 0);
        tryCompare(dialog, "visible", false);
    }

    function test_confirmUpload_aSecondDropWaitsInsteadOfReplacingTheFirst() {
        // A drop still reaches the app while this dialog is up, and open() does
        // nothing on a visible Popup -- so without a queue the first question's
        // files would be dropped on the floor with no error at all.
        const uploads = makeUploads();
        const dialog = makeDialog(confirmUploadComponent, {
                                      "uploads": uploads
                                  });

        uploads.uploadRequiresConfirmation(["a.txt"], 5, 42, false);
        uploads.uploadRequiresConfirmation(["b.txt"], 2, 7, false);

        // Still the first question, with its own destination.
        tryCompare(dialog, "opened", true);
        compare(dialog.fileCount, 5);
        compare(dialog.destinationHandle, 42);

        dialog.accept();
        compare(uploads.confirmedCount, 1);
        compare(uploads.lastCall.handle, 42);

        // ...and only now does the second one get asked.
        tryVerify(() => dialog.fileCount === 2);
        compare(dialog.destinationHandle, 7);
        dialog.accept();
        compare(uploads.confirmedCount, 2);
        compare(uploads.lastCall.handle, 7);
    }

    function test_confirmUpload_cancellingTheFirstStillAsksTheSecond() {
        // Cancel answers one question, not the queue behind it.
        const uploads = makeUploads();
        const dialog = makeDialog(confirmUploadComponent, {
                                      "uploads": uploads
                                  });

        uploads.uploadRequiresConfirmation(["a.txt"], 5, 42, false);
        uploads.uploadRequiresConfirmation(["b.txt"], 2, 7, false);

        tryCompare(dialog, "opened", true);
        dialog.reject();

        tryVerify(() => dialog.fileCount === 2);
        compare(uploads.confirmedCount, 0);
        dialog.accept();
        compare(uploads.confirmedCount, 1);
        compare(uploads.lastCall.handle, 7);
    }

    // This is the destructive one: Replace overwrites what the user has, Skip
    // keeps it. Swapping them loses data with no error and no visible symptom.
    function test_nameConflict_replaceReplacesAndOnlyReplaces() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, true);
        buttonNamed(dialog, "Replace").clicked();

        compare(uploads.replacingCount, 1);
        compare(uploads.skippingCount, 0);
        compare(uploads.lastCall.paths, ["a.txt"]);
        compare(uploads.lastCall.handle, 0);
        compare(uploads.lastCall.isRoot, true);
        tryCompare(dialog, "visible", false);
    }

    function test_nameConflict_skipSkipsAndOnlySkips() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 42, false);
        buttonNamed(dialog, "Skip").clicked();

        compare(uploads.skippingCount, 1);
        compare(uploads.replacingCount, 0);
        compare(uploads.lastCall.handle, 42);
        compare(uploads.lastCall.isRoot, false);
        tryCompare(dialog, "visible", false);
    }

    function test_nameConflict_cancelUploadsNothing() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 42, false);
        buttonNamed(dialog, "Cancel").clicked();

        compare(uploads.replacingCount, 0);
        compare(uploads.skippingCount, 0);
        compare(uploads.uploadFilesCount, 0);
        tryCompare(dialog, "visible", false);
    }

    function test_nameConflict_listsAtMostFiveNames_data() {
        return [
                    {
                        tag: "five",
                        names: ["a", "b", "c", "d", "e"],
                        shown: "a, b, c, d, e",
                        ellipsis: false
                    },
                    {
                        tag: "six",
                        names: ["a", "b", "c", "d", "e", "f"],
                        shown: "a, b, c, d, e",
                        ellipsis: true
                    }
                ];
    }

    function test_nameConflict_listsAtMostFiveNames(data) {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(data.names, data.names, 0, true);

        const text = dialog.contentChildren[0].text;
        compare(text.indexOf(data.shown) !== -1, true);
        compare(text.indexOf("…") !== -1, data.ellipsis);
    }

    function test_nameConflict_aSecondDropWaitsInsteadOfReplacingTheFirst() {
        // Same queue as ConfirmUploadDialog, and it matters more here: the
        // question being replaced is the one guarding an overwrite.
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 42, false);
        uploads.nameConflictRequiresConfirmation(["b.txt"], ["b.txt"], 7, false);

        tryCompare(dialog, "opened", true);
        compare(dialog.destinationHandle, 42);
        buttonNamed(dialog, "Skip").clicked();
        compare(uploads.skippingCount, 1);
        compare(uploads.lastCall.handle, 42);

        tryVerify(() => dialog.destinationHandle === 7);
        buttonNamed(dialog, "Replace").clicked();
        compare(uploads.replacingCount, 1);
        compare(uploads.lastCall.handle, 7);
    }

    // ---- MissingPinDialog ----------------------------------------------

    function test_missingPin_signalOpensWithTheName() {
        failOnWarning(/Connections/);
        const quickAccess = makeQuickAccess();
        const dialog = makeDialog(missingPinComponent, {
                                      "quickAccess": quickAccess
                                  });

        quickAccess.missing(77, "Photos");

        tryCompare(dialog, "opened", true);
        compare(dialog.pinHandle, 77);
        compare(dialog.contentChildren[0].text,
                "\"Photos\" could not be found. Remove it from Quick access?");
    }

    function test_missingPin_acceptUnpinsThatHandle() {
        const quickAccess = makeQuickAccess();
        const dialog = makeDialog(missingPinComponent, {
                                      "quickAccess": quickAccess
                                  });

        quickAccess.missing(77, "Photos");
        dialog.accept();

        compare(quickAccess.unpinCount, 1);
        compare(quickAccess.lastUnpin, 77);
    }

    // Declining has to leave the pin in place: the folder may come back, and a
    // pin removed here cannot be recovered.
    function test_missingPin_rejectKeepsThePin() {
        const quickAccess = makeQuickAccess();
        const dialog = makeDialog(missingPinComponent, {
                                      "quickAccess": quickAccess
                                  });

        quickAccess.missing(77, "Photos");
        dialog.reject();

        compare(quickAccess.unpinCount, 0);
    }
}
