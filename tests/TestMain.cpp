#include "qml/GuiThread.h"
#include "TestApp.h"

#include <QCoreApplication>
#include <QObject>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Why the QCoreApplication lives here and not in a fixture: tests/CMakeLists.txt
// registers one ctest test per gtest case, so ctest starts this binary hundreds
// of times with a --gtest_filter. If each fixture brought the application up
// itself, whether one exists at all would depend on which cases the filter let
// through -- a queued invokeOnGuiThread (src/qml/GuiThread.h) would be
// delivered in a whole-binary run and silently dropped in ~QObject in a
// single-case run. Owning it here makes it unconditional and unforgettable.
//
// Qt permits exactly one QCoreApplication per process, and it keeps a reference
// to argc, hence a local of main rather than a static elsewhere: argc outlives
// it by construction and it is destroyed before any static destructor runs.
//
// InitGoogleMock rather than InitGoogleTest so the --gmock_* flags are parsed
// too; it initializes gtest as well, which is why GTest::gtest_main is not
// linked.
int main(int argc, char** argv)
{
    ::testing::InitGoogleMock(&argc, argv);
    QCoreApplication app(argc, argv);
    return RUN_ALL_TESTS();
}

namespace
{

// Guards the arrangement above: run this case alone (as ctest does) and a
// queued call must still arrive.
TEST(TestProcessEnvironment, QueuedGuiThreadCallIsDelivered)
{
    ASSERT_NE(QCoreApplication::instance(), nullptr);

    QObject target;
    bool called = false;
    invokeOnGuiThread(&target, [&called]() {
        called = true;
    });
    flushQueuedEvents();

    EXPECT_TRUE(called);
}

} // namespace
