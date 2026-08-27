import QtQuick
import QtTest
import MegaExplorer

// The views hand this component to delegates they reuse, so isFolder flips on a
// live instance rather than only at creation. That is what broke: typeIcon and
// the Label's ternaries share isFolder, and on a folder -> file flip the Label
// could re-evaluate first and read .family off a typeIcon that was still null.
// The invariant the fix rests on is the one pinned here -- typeIcon is never
// null, whatever isFolder says.
TestCase {
    id: testCase
    name: "FileIcon"

    Component {
        id: iconComponent
        FileIcon {
            isFolder: true
        }
    }

    function test_typeIconIsNeverNullForAFolder() {
        const icon = createTemporaryObject(iconComponent, testCase);
        verify(icon);
        verify(icon.typeIcon !== null);
        verify(icon.typeIcon !== undefined);
    }

    function test_aReusedIconSurvivesTheFolderToFileFlip() {
        failOnWarning(/TypeError/);
        const icon = createTemporaryObject(iconComponent, testCase, {
                                               fileName: "holiday.jpg"
                                           });
        verify(icon);
        // One assignment, so both bindings re-run off the same change; which of
        // them runs first is what the icon must not depend on.
        icon.isFolder = false;
        compare(icon.typeIcon.glyph, FileTypeIcons.forFileName("holiday.jpg").glyph);
    }
}
