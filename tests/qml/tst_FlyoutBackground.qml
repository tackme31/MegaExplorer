import QtQuick
import QtTest
import MegaExplorer

// The flat fill must stay inside the sprite's shadow and 1px rim in both of the
// style's shadow placements: off by the shadow and it paints over the shadow, off
// by the rim and the menu loses its edge. Neither shows at a glance in a screenshot.
TestCase {
    id: testCase
    name: "FlyoutBackground"

    Component {
        id: backgroundComponent
        FlyoutBackground {
            width: 300
            height: 200
        }
    }

    Component {
        id: menuComponent
        MeasuredMenu {}
    }

    // StyleImage's own BorderImage is child 0; the fill is the Rectangle after it.
    function fillOf(background) {
        return background.children[0].children[1];
    }

    function test_shadowWithinBounds_data() {
        return [
                    {
                        tag: "menu",
                        within: true
                    },
                    {
                        tag: "combobox",
                        within: false
                    }
                ];
    }

    function test_shadowWithinBounds(data) {
        const bg = createTemporaryObject(backgroundComponent, testCase, {
                                             shadowWithinBounds: data.within
                                         });
        verify(bg);
        const cfg = bg.imageConfig;
        const left = data.within ? cfg.leftShadow : 0;
        const top = data.within ? cfg.topShadow : 0;
        const right = data.within ? cfg.rightShadow : 0;
        const bottom = data.within ? cfg.bottomShadow : 0;

        const fill = fillOf(bg);
        compare(fill.x, left + 1);
        compare(fill.y, top + 1);
        compare(fill.width, 300 - left - right - 2);
        compare(fill.height, 200 - top - bottom - 2);
        compare(fill.color, Theme.color.flyoutFill);
    }

    function test_menuUsesFlyoutBackground() {
        const menu = createTemporaryObject(menuComponent, testCase);
        verify(menu);
        compare(menu.background.shadowWithinBounds, true);
    }
}
