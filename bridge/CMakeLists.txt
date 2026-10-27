cmake_minimum_required(VERSION 3.20)
project(tinoxkde VERSION 1.0.0 LANGUAGES C CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.5.0")

# -DTKDE_SANITIZE=address,undefined (or thread) builds everything instrumented: `make test-asan`
set(TKDE_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined")
if(TKDE_SANITIZE)
    add_compile_options(-fsanitize=${TKDE_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${TKDE_SANITIZE})
endif()

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Widgets Xml)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons I18n Crash DBusAddons TextEditor SyntaxHighlighting Config ConfigWidgets XmlGui WidgetsAddons)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GC REQUIRED IMPORTED_TARGET bdw-gc)
find_package(Threads REQUIRED)

add_library(tinoxkde SHARED
    src/abi.cpp src/bridge.cpp
    src/editor/editor.cpp src/editor/diagnostics.cpp src/editor/completion.cpp src/editor/viewfeatures.cpp
    src/widget/widget.cpp src/action/action.cpp src/model/model.cpp)
target_include_directories(tinoxkde PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>)
target_include_directories(tinoxkde PRIVATE src common)
target_link_libraries(tinoxkde PRIVATE Qt6::Core Qt6::Widgets Qt6::Xml KF6::CoreAddons KF6::I18n KF6::Crash KF6::DBusAddons KF6::TextEditor KF6::SyntaxHighlighting KF6::ConfigCore KF6::ConfigWidgets KF6::XmlGui KF6::WidgetsAddons PkgConfig::GC)
set_target_properties(tinoxkde PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1   # equals TKDE_ABI_VERSION (docs/abi.md, versioning)
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

install(TARGETS tinoxkde LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES include/tinoxkde.h DESTINATION ${KDE_INSTALL_INCLUDEDIR})

# The headless mock of the bridge (no Qt, no KDE): same ABI, scripted by tests.
add_library(tinoxkde-mock SHARED mock/mock.cpp)
target_include_directories(tinoxkde-mock PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mock>)
target_include_directories(tinoxkde-mock PRIVATE common)
target_link_libraries(tinoxkde-mock PRIVATE PkgConfig::GC)
set_target_properties(tinoxkde-mock PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    AUTOMOC OFF)
install(TARGETS tinoxkde-mock LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(version_test tests/version_test.cpp)
    target_link_libraries(version_test PRIVATE tinoxkde)
    add_test(NAME version COMMAND version_test)

    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE tinoxkde Threads::Threads PkgConfig::GC)
    set_target_properties(core_test PROPERTIES AUTOMOC OFF)
    add_test(NAME core COMMAND core_test)
    set_tests_properties(core PROPERTIES TIMEOUT 120)

    add_executable(editor_test tests/editor_test.cpp)
    target_link_libraries(editor_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Threads::Threads PkgConfig::GC)
    set_target_properties(editor_test PROPERTIES AUTOMOC OFF)
    # the test builds JSON with plain string literals
    target_compile_options(editor_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    add_test(NAME editor COMMAND editor_test)
    set_tests_properties(editor PROPERTIES TIMEOUT 180)

    add_executable(widget_test tests/widget_test.cpp)
    target_link_libraries(widget_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Threads::Threads PkgConfig::GC)
    set_target_properties(widget_test PROPERTIES AUTOMOC OFF)
    target_compile_options(widget_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    target_compile_definitions(widget_test PRIVATE TKDE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/widget")
    add_test(NAME widget COMMAND widget_test)
    set_tests_properties(widget PROPERTIES TIMEOUT 180)

    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_executable(action_test tests/action_test.cpp)
    target_link_libraries(action_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Threads::Threads PkgConfig::GC)
    set_target_properties(action_test PROPERTIES AUTOMOC OFF)
    target_compile_options(action_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    add_test(NAME action COMMAND action_test)
    set_tests_properties(action PROPERTIES TIMEOUT 180)

    add_executable(model_test tests/model_test.cpp)
    target_link_libraries(model_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Threads::Threads PkgConfig::GC)
    set_target_properties(model_test PROPERTIES AUTOMOC OFF)
    target_compile_options(model_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    add_test(NAME model COMMAND model_test)
    set_tests_properties(model PROPERTIES TIMEOUT 300)

    add_executable(bench_queue tests/bench_queue.cpp)   # not a test: `make bench`
    target_link_libraries(bench_queue PRIVATE tinoxkde Threads::Threads PkgConfig::GC)
    set_target_properties(bench_queue PROPERTIES AUTOMOC OFF)

    add_executable(mock_test mock/tests/mock_test.cpp)
    target_link_libraries(mock_test PRIVATE tinoxkde-mock Threads::Threads PkgConfig::GC)
    set_target_properties(mock_test PROPERTIES AUTOMOC OFF)
    add_test(NAME mock COMMAND mock_test)
    add_dependencies(version_test header_c header_cxx)

    # The header must compile as strict C11 and as C++, warnings are errors.
    add_library(header_c OBJECT tests/header_c.c)
    target_include_directories(header_c PRIVATE include)
    set_target_properties(header_c PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(header_c PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_library(header_cxx OBJECT tests/header_cxx.cpp)
    target_include_directories(header_cxx PRIVATE include)
    set_target_properties(header_cxx PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(header_cxx PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
