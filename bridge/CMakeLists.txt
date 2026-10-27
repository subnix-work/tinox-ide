cmake_minimum_required(VERSION 3.20)
project(tinoxkde VERSION 0.1.0 LANGUAGES CXX)

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

add_library(tinoxkde SHARED src/version.cpp src/spike.cpp)
target_include_directories(tinoxkde PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${KDE_INSTALL_INCLUDEDIR}>)
target_link_libraries(tinoxkde PRIVATE Qt6::Core Qt6::Widgets KF6::TextEditor PkgConfig::GC)
set_target_properties(tinoxkde PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

install(TARGETS tinoxkde LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
install(FILES include/tinoxkde.h DESTINATION ${KDE_INSTALL_INCLUDEDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(version_test tests/version_test.cpp)
    target_link_libraries(version_test PRIVATE tinoxkde)
    add_test(NAME version COMMAND version_test)
endif()
