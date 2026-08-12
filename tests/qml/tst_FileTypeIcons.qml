import QtQuick
import QtTest
import MegaExplorer

// FileTypeIcons is a pragma Singleton holding one pure function, so the test
// needs no fixture. What it pins down is the whitelist's contract: a claimed
// extension gets that type's pair, and everything else lands on the generic
// page rather than on nothing at all.
TestCase {
    name: "FileTypeIcons"

    function test_aClaimedExtensionGetsItsOwnGlyph() {
        const icon = FileTypeIcons.forFileName("holiday.JPG");
        verify(icon.glyph !== FileTypeIcons.fallback.glyph);
        // Case folding happens on the extension, not on the whole name.
        compare(icon.glyph, FileTypeIcons.forFileName("holiday.jpg").glyph);
    }

    function test_everyTypeCarriesAFamilyWithItsGlyph() {
        // The pair is the point: a type may name a font other than Theme's, so
        // no caller may assume the icon family.
        for (let i = 0; i < FileTypeIcons.types.length; ++i) {
            verify(FileTypeIcons.types[i].family.length > 0);
            verify(FileTypeIcons.types[i].glyph.length > 0);
        }
    }

    function test_unknownExtensionsFallBackToThePage() {
        compare(FileTypeIcons.forFileName("archive.qzx"), FileTypeIcons.fallback);
        compare(FileTypeIcons.forFileName("README"), FileTypeIcons.fallback);
        compare(FileTypeIcons.forFileName("trailing."), FileTypeIcons.fallback);
    }

    function test_aLeadingDotIsNotAnExtension() {
        // ".png" is a file named .png, not a picture.
        compare(FileTypeIcons.forFileName(".png"), FileTypeIcons.fallback);
    }

    function test_prototypeKeysAreNotIcons() {
        // A plain map lookup would answer with a function here.
        compare(FileTypeIcons.forFileName("notes.constructor"), FileTypeIcons.fallback);
    }
}
