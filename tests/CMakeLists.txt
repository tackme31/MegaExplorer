find_package(GTest CONFIG REQUIRED)

add_executable(MegaExplorerTests
    FileListingServiceTest.cpp
)

target_link_libraries(MegaExplorerTests PRIVATE
    MegaExplorerCore
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(MegaExplorerTests)
