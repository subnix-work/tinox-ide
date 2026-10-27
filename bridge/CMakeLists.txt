cmake_minimum_required(VERSION 3.20)
project(tinoxkde VERSION 1.0.0 LANGUAGES C CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.5.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Widgets)
find_package(KF6TextEditor ${KF_MIN_VERSION} REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GC REQUIRED IMPORTED_TARGET bdw-gc)
find_package(Threads REQUIRED)

add_library(tinoxkde SHARED src/version.cpp src/spike.cpp)
target_include_directories(tinoxkde PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>)
target_link_libraries(tinoxkde PRIVATE Qt6::Core Qt6::Widgets KF6::TextEditor PkgConfig::GC)
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

    add_executable(mock_test mock/tests/mock_test.cpp)
    target_link_libraries(mock_test PRIVATE tinoxkde-mock Threads::Threads)
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
