cmake_minimum_required(VERSION 3.20)
project(tinoxkde VERSION 1.0.0 LANGUAGES C CXX)

# Application version is independent of the additive bridge ABI / SONAME.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../tinox.toml" _version_line REGEX "^version = ")
list(GET _version_line 0 _version_line)
string(REGEX REPLACE "^version = \"([^\"]+)\".*" "\\1" TINOX_IDE_VERSION "${_version_line}")
configure_file(src/version.h.in generated/version.h @ONLY)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.5.0")

# -DTKDE_SANITIZE=address,undefined (or thread) builds everything instrumented: `make test-asan`
set(TKDE_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined")
# -DTKDE_FUZZ=ON (clang): `make fuzz` builds the command fuzzer; the library is instrumented for coverage
option(TKDE_FUZZ "Build the libFuzzer command fuzzer" OFF)
if(TKDE_FUZZ)
    add_compile_options(-fsanitize=fuzzer-no-link)
    # clang does not link its runtimes into shared libraries; the fuzzer executable provides them
    add_link_options(-Wl,-z,undefs)
endif()

if(TKDE_SANITIZE)
    add_compile_options(-fsanitize=${TKDE_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${TKDE_SANITIZE})
endif()

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Widgets Xml DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Parts CoreAddons I18n Crash DBusAddons TextEditor SyntaxHighlighting Config ConfigWidgets XmlGui WidgetsAddons Notifications)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GC REQUIRED IMPORTED_TARGET bdw-gc)
find_package(Threads REQUIRED)

add_library(tinoxkde SHARED
    src/abi.cpp src/bridge.cpp src/terminal/terminal.cpp
    src/editor/editor.cpp src/editor/diagnostics.cpp src/editor/semantics.cpp src/editor/completion.cpp src/editor/viewfeatures.cpp
    src/widget/widget.cpp src/action/action.cpp src/model/model.cpp
    src/dialog/dialog.cpp src/dialog/config.cpp src/dialog/notify.cpp)
target_include_directories(tinoxkde PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>)
target_include_directories(tinoxkde PRIVATE src common "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(tinoxkde PRIVATE Qt6::Core Qt6::Widgets Qt6::Xml KF6::Parts KF6::CoreAddons KF6::I18n KF6::Crash KF6::DBusAddons KF6::TextEditor KF6::SyntaxHighlighting KF6::ConfigCore KF6::ConfigWidgets KF6::XmlGui KF6::WidgetsAddons KF6::Notifications PkgConfig::GC)
set_target_properties(tinoxkde PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1   # equals TKDE_ABI_VERSION (docs/abi.md, versioning)
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

install(TARGETS tinoxkde LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES include/tinoxkde.h DESTINATION ${KDE_INSTALL_INCLUDEDIR})

# Install the separately compiled Tinox executable and its freedesktop/KDE resources.
# Plain bridge builds remain possible without a Tinox binary.
set(TKDE_IDE_BINARY "" CACHE FILEPATH "Built Tinox IDE executable to install")
if(TKDE_IDE_BINARY)
    find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)
    install(PROGRAMS "${TKDE_IDE_BINARY}" DESTINATION ${KDE_INSTALL_BINDIR} RENAME tinox-ide)
    file(RELATIVE_PATH TKDE_RELATIVE_LIBDIR "${KDE_INSTALL_FULL_BINDIR}" "${KDE_INSTALL_FULL_LIBDIR}")
    configure_file(install-rpath.cmake.in install-rpath.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-rpath.cmake")
endif()
set(_resources "${CMAKE_CURRENT_SOURCE_DIR}/../resources")
install(FILES "${_resources}/org.subnix.tinoxide.desktop" DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES "${_resources}/org.subnix.tinoxide.metainfo.xml" DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES "${_resources}/icons/org.subnix.tinoxide.svg" DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(FILES "${_resources}/syntax/tinox.xml" DESTINATION ${KDE_INSTALL_DATADIR}/org.kde.syntax-highlighting/syntax)
install(FILES "${_resources}/mime/tinox.xml" DESTINATION ${KDE_INSTALL_MIMEDIR} RENAME org.subnix.tinoxide.xml)
install(FILES "${_resources}/notifications/tinoxide.notifyrc" DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES ../LICENSE-MIT ../LICENSE-APACHE DESTINATION ${KDE_INSTALL_DATADIR}/licenses/tinox-ide)

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
# The mock is a development/testing tool and is not installed with the application.

# `make fuzz`: libFuzzer target for the command parser and all commands (clang, docs/testing.md)
if(TKDE_FUZZ)
    add_executable(command_fuzzer fuzz/command_fuzzer.cpp)
    target_compile_options(command_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
    target_link_options(command_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_libraries(command_fuzzer PRIVATE tinoxkde Qt6::Core Qt6::Widgets PkgConfig::GC)
    set_target_properties(command_fuzzer PROPERTIES AUTOMOC OFF)
endif()

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

    add_executable(terminal_test tests/terminal_test.cpp)
    target_link_libraries(terminal_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Threads::Threads PkgConfig::GC)
    set_target_properties(terminal_test PROPERTIES AUTOMOC OFF)
    target_compile_options(terminal_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    add_test(NAME terminal COMMAND terminal_test)
    add_test(NAME terminal_no_konsole COMMAND terminal_test --without-konsole)
    set_tests_properties(terminal terminal_no_konsole PROPERTIES TIMEOUT 60)

    add_executable(dialog_test tests/dialog_test.cpp)
    target_link_libraries(dialog_test PRIVATE tinoxkde Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::DBus KF6::ConfigCore Threads::Threads PkgConfig::GC)
    set_target_properties(dialog_test PROPERTIES AUTOMOC OFF)
    target_compile_options(dialog_test PRIVATE -UQT_NO_CAST_FROM_ASCII -UQT_NO_CAST_TO_ASCII)
    target_compile_definitions(dialog_test PRIVATE TKDE_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources")
    add_test(NAME dialog COMMAND dialog_test)
    set_tests_properties(dialog PROPERTIES TIMEOUT 300)

    # The ABI contract, as plain C against the header: once with the real bridge, once with the
    # mock. Both must pass the same program (docs/testing.md).
    foreach(impl real mock)
        if(impl STREQUAL "real")
            set(lib tinoxkde)
        else()
            set(lib tinoxkde-mock)
        endif()
        add_executable(contract_test_${impl} tests/contract_test.c)
        target_link_libraries(contract_test_${impl} PRIVATE ${lib} Threads::Threads PkgConfig::GC)
        set_target_properties(contract_test_${impl} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF LINKER_LANGUAGE C C_STANDARD 11 C_STANDARD_REQUIRED ON)
        add_test(NAME contract_${impl} COMMAND contract_test_${impl})
        set_tests_properties(contract_${impl} PROPERTIES TIMEOUT 120)
    endforeach()

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
