import QtQuick
import QtTest
import MegaExplorer

// R6-4. The dialogs Main.qml used to declare inline and now instantiates
// from qml/components/: NameConflictDialog, MissingPinDialog, since the
// copy/move conflict rework CopyConflictDialog, and PropertiesDialog. All of them
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

    // Declared per file view rather than by Main.qml, unlike the three above,
    // but silent in the same way: nothing outside them opens them.
    Component {
        id: confirmRubbishComponent
        ConfirmRubbishDialog {}
    }

    Component {
        id: confirmPermanentDeleteComponent
        ConfirmPermanentDeleteDialog {}
    }

    Component {
        id: confirmRemoveLinkComponent
        ConfirmRemoveLinkDialog {}
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
                                                    int unaffectedCount, string conflictingSize,
                                                    string unaffectedSize, var destinationHandle,
                                                    bool destinationIsRoot)
            signal uploadRequiresConfirmation(var filePaths, int fileCount, var destinationHandle,
                                              bool destinationIsRoot)

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

    // confirm() reaches through navController.fileListModel.selectedEntries(),
    // so the fake has to be nested the same way.
    Component {
        id: navControllerComponent

        QtObject {
            id: fakeNav

            property var entries: []
            property QtObject fileListModel: QtObject {
                function selectedEntries() {
                    return fakeNav.entries;
                }
            }
        }
    }

    Component {
        id: mutControllerComponent

        QtObject {
            property var lastRubbishHandles: null
            property var lastDeleteHandles: null
            property var lastRemoveLinkHandle: null

            function moveHandlesToRubbish(handles) {
                lastRubbishHandles = handles;
            }

            function deleteHandlesPermanently(handles) {
                lastDeleteHandles = handles;
            }

            function removeLink(handle) {
                lastRemoveLinkHandle = handle;
            }
        }
    }

    // CopyConflictDialog's own controller stub: it answers two signals rather than
    // reading state, so the signatures here have to track
    // src/qml/FileMutationController.h's exactly.
    Component {
        id: copyConflictComponent
        CopyConflictDialog {}
    }

    Component {
        id: settingsComponent
        SettingsDialog {}
    }

    Component {
        id: propertiesComponent
        PropertiesDialog {}
    }

    // Mirrors PropertiesController's QML-facing surface. Every value is a plain
    // property because the real one publishes them all through one changed()
    // signal, which QML re-reads on assignment here just the same.
    Component {
        id: propertiesControllerComponent

        QtObject {
            signal showRequested

            property string name: "a.jpg"
            property bool isFolder: false
            property var sizeBytes: 1024
            property string formattedSize: "1.0 KiB"
            property var modificationTime: 0
            property string parentPath: ""
            property int rootKind: 0
            property int fileCount: -1
            property int folderCount: -1
            property bool loading: false
            property bool failed: false
        }
    }

    Component {
        id: conflictControllerComponent

        QtObject {
            signal copyNameConflict(var entries, var conflictingFiles, var conflictingFolders,
                                    var renamedTo, string conflictingSize, string unaffectedSize,
                                    var destination, bool destinationIsRoot)
            signal moveNameConflict(var entries, var conflictingFiles, var conflictingFolders,
                                    var renamedTo, var destination, bool destinationIsRoot,
                                    var source, bool sourceIsRoot)

            function copyIgnoringExisting(entries, target, targetIsRoot) {
            }
            function copyRenamingExisting(entries, target, targetIsRoot) {
            }
            function copySkippingExisting(entries, target, targetIsRoot) {
            }
            function moveIgnoringExisting(entries, target, targetIsRoot, source, sourceIsRoot) {
            }
            function moveRenamingExisting(entries, target, targetIsRoot, source, sourceIsRoot) {
            }
            function moveSkippingExisting(entries, target, targetIsRoot, source, sourceIsRoot) {
            }
        }
    }

    function makeSelection(names) {
        const nav = createTemporaryObject(navControllerComponent, testCase);
        verify(nav !== null);
        nav.entries = names.map((n, i) => ({
            "name": n,
            "handle": i + 1
        }));
        return nav;
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

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 0, true);

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

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 0, true);
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

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 42, false);
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

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 42, false);
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

        uploads.nameConflictRequiresConfirmation(data.names, data.names, 0, "", "", 0, true);

        const text = dialog.contentChildren[0].text;
        compare(text.indexOf(data.shown) !== -1, true);
        compare(text.indexOf("…") !== -1, data.ellipsis);
    }

    function test_nameConflict_wordsReplaceFromVersioningAndSaysWhatSkipKeeps() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads,
                                      "fileVersioningEnabled": false
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 4, "", "", 0, true);

        tryCompare(dialog, "opened", true);
        const text = dialog.contentChildren[0].text;
        verify(text.indexOf("cannot be recovered") !== -1);
        // Without this line Skip and Cancel read as the same answer.
        verify(text.indexOf("other 4") !== -1);
    }

    // With versioning on, Replace is recoverable, so its sentence rides on the
    // button's tooltip instead of lengthening the body.
    function test_nameConflict_replaceSentenceLivesOnTheButton() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 0, true);

        tryCompare(dialog, "opened", true);
        verify(dialog.replaceLine().indexOf("earlier versions") !== -1);
        verify(dialog.contentChildren[0].text.indexOf("\"Replace\"") === -1);
    }

    // Sizes are formatted in C++ and only quoted here, so what this checks is that
    // each one reaches the half of the message it belongs to.
    function test_nameConflict_quotesTheSizeOnBothSidesOfTheQuestion() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 4, "2.0 KB", "9.0 MB", 0,
                                                 true);

        tryCompare(dialog, "opened", true);
        const text = dialog.contentChildren[0].text;
        verify(text.indexOf("(2.0 KB)") !== -1);
        verify(text.indexOf("other 4 file(s) are uploaded either way (9.0 MB)") !== -1);
    }

    // Zero bytes arrives as an empty string, and "(0 B)" is worse than nothing.
    function test_nameConflict_leavesTheSizeOutWhenItIsUnknown() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 4, "", "", 0, true);

        tryCompare(dialog, "opened", true);
        const text = dialog.contentChildren[0].text;
        verify(text.indexOf("()") === -1);
        verify(text.indexOf("other 4 file(s) are uploaded either way.") !== -1);
    }

    // A Popup sizes itself from its content's *implicit* width, so before the cap
    // the frame followed the name list and took its own buttons off-screen.
    // Asserting on the frame is not enough -- a Popup clamps its own width to the
    // overlay anyway, so dialog.width alone passes with the bug present. Staged
    // here rather than in a screenshot because the dialog opens only from a drop.
    function test_nameConflict_staysInsideTheWindowOnALongNameList() {
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });
        const long = "a-name-long-enough-to-outgrow-any-sane-window-on-its-own.txt";

        uploads.nameConflictRequiresConfirmation([long], [long, long, long], 0, "", "", 0, true);

        tryCompare(dialog, "opened", true);
        const label = dialog.contentChildren[0];
        // Guard: if the list ever stops being wider than the window, everything
        // below would pass on its own emptiness.
        verify(dialog.parent.width > 0);
        verify(label.implicitWidth > dialog.parent.width);

        verify(dialog.width <= dialog.parent.width);
        verify(label.width <= dialog.availableWidth);
        // Counted against the message's own hard breaks rather than against 1:
        // this text already carries a "\n" ahead of the name list, so lineCount > 1
        // holds even completely unwrapped.
        verify(label.lineCount > label.text.split("\n").length);
    }

    function test_nameConflict_aSecondDropWaitsInsteadOfReplacingTheFirst() {
        // Same queue as ConfirmUploadDialog, and it matters more here: the
        // question being replaced is the one guarding an overwrite.
        const uploads = makeUploads();
        const dialog = makeDialog(nameConflictComponent, {
                                      "uploads": uploads
                                  });

        uploads.nameConflictRequiresConfirmation(["a.txt"], ["a.txt"], 0, "", "", 42, false);
        uploads.nameConflictRequiresConfirmation(["b.txt"], ["b.txt"], 0, "", "", 7, false);

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

    // ---- PropertiesDialog ----------------------------------------------

    function makeProperties() {
        const controller = createTemporaryObject(propertiesControllerComponent, testCase);
        verify(controller !== null);
        return controller;
    }

    function test_properties_signalOpensTheDialog() {
        failOnWarning(/Connections/);
        const controller = makeProperties();
        const dialog = makeDialog(propertiesComponent, {
                                      "properties": controller
                                  });

        controller.showRequested();

        tryCompare(dialog, "opened", true);
    }

    // The root is named here, not in C++: ViewLabels owns every screen the app
    // invented, and a binned node would otherwise read as a Cloud Drive one.
    function test_properties_locationNamesTheRootItEndedAt_data() {
        return [
                    {
                        tag: "cloudDrive",
                        rootKind: ViewKind.CloudDrive,
                        parentPath: "/photos",
                        expected: "Cloud Drive/photos"
                    },
                    {
                        tag: "rubbish",
                        rootKind: ViewKind.Rubbish,
                        parentPath: "",
                        expected: "Rubbish bin"
                    }
                ];
    }

    function test_properties_locationNamesTheRootItEndedAt(data) {
        const controller = makeProperties();
        const dialog = makeDialog(propertiesComponent, {
                                      "properties": controller
                                  });

        controller.rootKind = data.rootKind;
        controller.parentPath = data.parentPath;

        compare(dialog.locationText, data.expected);
    }

    function test_properties_sizeDropsTheExactCountWhenItIsAlreadyShown_data() {
        return [
                    {
                        tag: "underOneKiB",
                        sizeBytes: 12,
                        formattedSize: "12 bytes",
                        expected: "12 bytes"
                    },
                    {
                        tag: "quantified",
                        sizeBytes: 1234,
                        formattedSize: "1.2 kB",
                        expected: "1.2 kB (1,234 bytes)"
                    }
                ];
    }

    function test_properties_sizeDropsTheExactCountWhenItIsAlreadyShown(data) {
        const controller = makeProperties();
        const dialog = makeDialog(propertiesComponent, {
                                      "properties": controller
                                  });

        controller.sizeBytes = data.sizeBytes;
        controller.formattedSize = data.formattedSize;

        compare(dialog.sizeText(), data.expected);
    }

    // The dialog used to print the timestamp with toLocaleString(), which on a
    // Japanese system read "2019年2月13日 23:10:23" while the Date modified column
    // beside it read "2/13/2019 11:10 PM". Asserting the shape rather than an exact
    // string keeps the test off the runner's timezone.
    function test_properties_modifiedMatchesTheListingFormat() {
        const controller = makeProperties();
        const dialog = makeDialog(propertiesComponent, {
                                      "properties": controller
                                  });

        controller.modificationTime = 1550099423;

        verify(/^\d{1,2}\/\d{1,2}\/\d{4} \d{1,2}:\d{2} [AP]M$/.test(dialog.modifiedText()));
    }

    function test_properties_modifiedIsUnknownWithoutAStamp() {
        const controller = makeProperties();
        const dialog = makeDialog(propertiesComponent, {
                                      "properties": controller
                                  });

        controller.modificationTime = 0;

        compare(dialog.modifiedText(), "Unknown");
    }

    // ---- ConfirmRubbishDialog / ConfirmPermanentDeleteDialog -----------

    function test_confirmDelete_staysInsideTheWindowOnALongName_data() {
        return [
                    {
                        tag: "rubbish",
                        component: "confirmRubbish"
                    },
                    {
                        tag: "permanent",
                        component: "confirmPermanentDelete"
                    }
                ];
    }

    // Both embed one file name in their message, so before the cap the message
    // laid out on one unwrapped line far wider than the frame around it.
    // Asserting on the frame is not enough -- a Popup clamps its own width to
    // the overlay anyway, so dialog.width alone passes with the bug present.
    // Staged here rather than in a screenshot because the dialog is reachable
    // only through a live selection.
    function test_confirmDelete_staysInsideTheWindowOnALongName(data) {
        const nav = makeSelection(["a-name-long-enough-to-outgrow-any-sane-window-on-its-own.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const component = data.component === "confirmRubbish" ? confirmRubbishComponent :
                                                                confirmPermanentDeleteComponent;
        const dialog = makeDialog(component, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();

        tryCompare(dialog, "opened", true);
        const label = dialog.contentChildren[0];
        // Guard: if the name ever stops being wider than the window, everything
        // below would pass on its own emptiness.
        verify(dialog.parent.width > 0);
        verify(label.implicitWidth > dialog.parent.width);

        verify(dialog.width <= dialog.parent.width);
        verify(label.width <= dialog.availableWidth);
        verify(label.lineCount > 1);
    }

    // The handles are sampled at confirm() time, and the wrap must not have
    // disturbed that: what gets deleted has to be what the prompt named.
    function test_confirmRubbish_acceptBinsTheSampledHandles() {
        const nav = makeSelection(["a.txt", "b.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmRubbishComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();
        nav.entries = [];
        dialog.accept();

        compare(mut.lastRubbishHandles, [1, 2]);
    }

    function test_confirmPermanentDelete_acceptDeletesTheSampledHandles() {
        const nav = makeSelection(["a.txt", "b.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmPermanentDeleteComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();
        nav.entries = [];
        dialog.accept();

        compare(mut.lastDeleteHandles, [1, 2]);
    }

    // ---- ConfirmRemoveLinkDialog ----------------------------------------

    // Single-target, unlike the two above: the action is offered on one node
    // only, so the dialog samples the first entry and nothing else.
    function test_confirmRemoveLink_acceptRemovesTheSampledHandle() {
        const nav = makeSelection(["a.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmRemoveLinkComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();
        nav.entries = [];
        dialog.accept();

        compare(mut.lastRemoveLinkHandle, 1);
    }

    // Cancel is the reason this dialog exists at all: the link has to survive it.
    function test_confirmRemoveLink_rejectKeepsTheLink() {
        const nav = makeSelection(["a.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmRemoveLinkComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();
        dialog.reject();

        compare(mut.lastRemoveLinkHandle, null);
    }

    // An empty selection must not open a prompt naming nothing.
    function test_confirmRemoveLink_confirmWithNoSelectionDoesNotOpen() {
        const nav = makeSelection([]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmRemoveLinkComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });

        dialog.confirm();

        compare(dialog.opened, false);
    }

    // ---- CopyConflictDialog --------------------------------------------
    //
    // Which button is highlighted is the whole point of these: "Continue" is the
    // default in three of the four cells, but a copy onto a file's name with
    // versioning off destroys the existing file beyond recovery, so there it must
    // not be (SPEC_NAME_CONFLICT_COPY_MOVE 3-1). Nothing on screen distinguishes a
    // wrongly-defaulted dialog from a right one until someone presses Enter.

    function makeCopyConflict(versioningEnabled) {
        const mut = createTemporaryObject(conflictControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(copyConflictComponent, {
                                      "mutController": mut,
                                      "fileVersioningEnabled": versioningEnabled
                                  });
        return {
            "mut": mut,
            "dialog": dialog
        };
    }

    function test_copyConflict_defaultIsContinueWhenVersioningKeepsTheOldFile() {
        const c = makeCopyConflict(true);

        c.mut.copyNameConflict([
                                   {}
                               ], ["a.txt"], [], ["a - Copy.txt"], "", "", 42, false);

        tryCompare(c.dialog, "opened", true);
        compare(c.dialog.continueLosesData, false);
        tryCompare(buttonNamed(c.dialog, "Continue"), "highlighted", true);
        compare(buttonNamed(c.dialog, "Rename").highlighted, false);
        verify(buttonNamed(c.dialog, "Continue").activeFocus);
        // Recoverable, so the sentence rides on Continue's tooltip rather than
        // the body.
        verify(c.dialog.continueLine().indexOf("earlier versions") !== -1);
        verify(c.dialog.buildMessage().indexOf("earlier versions") === -1);
    }

    function test_copyConflict_defaultIsRenameWhenVersioningIsOff() {
        const c = makeCopyConflict(false);

        c.mut.copyNameConflict([
                                   {}
                               ], ["a.txt"], [], ["a - Copy.txt"], "", "", 42, false);

        tryCompare(c.dialog, "opened", true);
        compare(c.dialog.continueLosesData, true);
        tryCompare(buttonNamed(c.dialog, "Rename"), "highlighted", true);
        compare(buttonNamed(c.dialog, "Continue").highlighted, false);
        verify(buttonNamed(c.dialog, "Rename").activeFocus);
        verify(c.dialog.buildMessage().indexOf("cannot be recovered") !== -1);
    }

    // Folders never version, so the setting must not reach them.
    function test_copyConflict_foldersKeepContinueEvenWithVersioningOff() {
        const c = makeCopyConflict(false);

        c.mut.copyNameConflict([
                                   {}
                               ], [], ["photos"], ["photos - Copy"], "", "", 42, false);

        tryCompare(c.dialog, "opened", true);
        compare(c.dialog.continueLosesData, false);
        tryCompare(buttonNamed(c.dialog, "Continue"), "highlighted", true);
    }

    // The versioning answer is an SDK round-trip issued at login, so it can land
    // after this dialog is already open. The wording follows it through a binding;
    // the accent and the focus have to be re-applied by hand, and a Continue left
    // highlighted under "cannot be recovered" points Enter at the destructive answer.
    function test_copyConflict_versioningLandingWhileOpenMovesTheDefault() {
        const c = makeCopyConflict(true);

        c.mut.copyNameConflict([
                                   {}
                               ], ["a.txt"], [], ["a - Copy.txt"], "", "", 42, false);
        tryCompare(c.dialog, "opened", true);
        tryCompare(buttonNamed(c.dialog, "Continue"), "highlighted", true);

        c.dialog.fileVersioningEnabled = false;

        compare(c.dialog.continueLosesData, true);
        compare(buttonNamed(c.dialog, "Rename").highlighted, true);
        compare(buttonNamed(c.dialog, "Continue").highlighted, false);
        verify(buttonNamed(c.dialog, "Rename").activeFocus);
        verify(c.dialog.buildMessage().indexOf("cannot be recovered") !== -1);
    }

    function test_copyConflict_quotesTheSizeOnBothSidesOfTheQuestion() {
        const c = makeCopyConflict(true);

        c.mut.copyNameConflict([
                                   {},
                                   {}
                               ], ["a.txt"], [], ["a - Copy.txt"], "2.0 KB", "9.0 MB", 42, false);

        tryCompare(c.dialog, "opened", true);
        const text = c.dialog.buildMessage();
        verify(text.indexOf("(2.0 KB)") !== -1);
        verify(text.indexOf("other 1 item(s) are copied either way (9.0 MB)") !== -1);
    }

    // A move rearranges nodes inside one account, so the signal carries no size at
    // all and quoting one would suggest data is being sent.
    function test_copyConflict_movesQuoteNoSize() {
        const c = makeCopyConflict(true);

        c.mut.moveNameConflict([
                                   {},
                                   {}
                               ], ["a.txt"], [], ["a (2).txt"], 42, false, 7, false);

        tryCompare(c.dialog, "opened", true);
        const text = c.dialog.buildMessage();
        verify(text.indexOf("KB") === -1);
        verify(text.indexOf("other 1 item(s) are moved either way.") !== -1);
    }

    // A folder is listed by name alone, so nothing else says Skip drops its contents.
    function test_copyConflict_saysAFolderIsSkippedWithItsContents() {
        const c = makeCopyConflict(true);

        c.mut.copyNameConflict([
                                   {}
                               ], [], ["photos"], ["photos - Copy"], "", "", 42, false);

        tryCompare(c.dialog, "opened", true);
        verify(c.dialog.skipLine().indexOf("with everything inside them") !== -1);
        verify(c.dialog.buildMessage().indexOf("with everything inside them") === -1);
    }

    // The per-answer sentences moved onto the buttons they describe; what stays in
    // the body is the question, plus the one wording that announces unrecoverable
    // loss (covered by the versioning-off cases above).
    function test_copyConflict_answerSentencesLiveOnTheButtons() {
        const c = makeCopyConflict(true);

        c.mut.copyNameConflict([
                                   {}
                               ], ["a.txt"], [], ["a - Copy.txt"], "", "", 42, false);

        tryCompare(c.dialog, "opened", true);
        const body = c.dialog.buildMessage();
        verify(c.dialog.renameLine().indexOf("a - Copy.txt") !== -1);
        verify(body.indexOf("\"Rename\"") === -1);
        verify(body.indexOf("\"Continue\"") === -1);
        // Still says what collided, or the dialog would ask nothing at all.
        verify(body.indexOf("a.txt") !== -1);
        // Files only, so Skip has nothing extra to explain.
        compare(c.dialog.skipLine(), "");
    }

    // Neither does a move: moveNode overwrites nothing, whatever the setting says.
    function test_copyConflict_moveKeepsContinueEvenWithVersioningOff() {
        const c = makeCopyConflict(false);

        c.mut.moveNameConflict([
                                   {}
                               ], ["a.txt"], [], ["a (2).txt"], 42, false, 7, false);

        tryCompare(c.dialog, "opened", true);
        compare(c.dialog.continueLosesData, false);
        tryCompare(buttonNamed(c.dialog, "Continue"), "highlighted", true);
    }

    // ---- SettingsDialog -------------------------------------------------

    // Main.qml owns the persisted value and the one styleHints write, so the only
    // logic here is the index <-> Qt::ColorScheme mapping -- and a screenshot
    // cannot see which row of a closed combo box is current.
    function test_settings_opensOnTheStoredTheme_data() {
        return [
                    {
                        tag: "system",
                        scheme: Qt.Unknown,
                        index: 0
                    },
                    {
                        tag: "light",
                        scheme: Qt.Light,
                        index: 1
                    },
                    {
                        tag: "dark",
                        scheme: Qt.Dark,
                        index: 2
                    },
                    // A value from a hand-edited registry must still leave a row
                    // selected rather than none.
                    {
                        tag: "unrecognized",
                        scheme: 99,
                        index: 0
                    }
                ];
    }

    function test_settings_opensOnTheStoredTheme(data) {
        const dialog = makeDialog(settingsComponent, {
                                      "colorScheme": data.scheme
                                  });
        let picked = [];
        dialog.colorSchemeSelected.connect(scheme => picked.push(scheme));

        dialog.open();

        tryCompare(dialog, "opened", true);
        compare(dialog.themeSelector.currentIndex, data.index);
        // Showing the stored value is not choosing it: an echo here would write
        // the preference back on every open.
        compare(picked.length, 0);
    }

    function test_settings_pickingARowReportsThatScheme() {
        const dialog = makeDialog(settingsComponent, {});
        let picked = [];
        dialog.colorSchemeSelected.connect(scheme => picked.push(scheme));

        dialog.open();
        tryCompare(dialog, "opened", true);
        dialog.themeSelector.activated(2);

        compare(picked.length, 1);
        compare(picked[0], Qt.Dark);
    }

    // ---- StandardButtonLabels ------------------------------------------

    // Dialog.standardButtons words its buttons from Qt's own catalogue, which
    // follows the OS language, so in an English-locale test run the wording is
    // already right and asserting it proves nothing. Overwriting the text first
    // is what makes this a test of pin() rather than of the runner's locale.
    function test_standardButtons_arePinnedToEnglish() {
        const nav = makeSelection(["a.txt"]);
        const mut = createTemporaryObject(mutControllerComponent, testCase);
        verify(mut !== null);
        const dialog = makeDialog(confirmRubbishComponent, {
                                      "navController": nav,
                                      "mutController": mut
                                  });
        const box = dialog.footer;
        verify(box !== null);
        compare(box.count, 2);
        for (var i = 0; i < box.count; ++i)
            box.itemAt(i).text = "translated elsewhere";

        StandardButtonLabels.pin(box);

        verify(buttonNamed(dialog, "Yes") !== null);
        verify(buttonNamed(dialog, "Cancel") !== null);
    }
}
