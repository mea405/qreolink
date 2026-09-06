cmake_minimum_required(VERSION 3.21)
project(qreolink LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)

# Prefer a direct libmpv lookup. pkg-config's mpv module often lists transitive
# include dirs (e.g. freetype2) that are missing when only libmpv2 is installed.
find_path(MPV_INCLUDE_DIR
    NAMES mpv/client.h
    HINTS
        "$ENV{MPV_DIR}/include"
        "${CMAKE_SOURCE_DIR}/.deps/usr/include"
        /usr/local/include
        /usr/include
)
find_library(MPV_LIBRARY
    NAMES mpv libmpv.so.2
    HINTS
        "$ENV{MPV_DIR}/lib"
        "${CMAKE_SOURCE_DIR}/.deps/usr/lib/x86_64-linux-gnu"
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib
)
# Some systems ship only the SONAME (libmpv.so.2) without the -dev linker name.
if(NOT MPV_LIBRARY)
    find_file(MPV_LIBRARY
        NAMES libmpv.so.2
        HINTS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
    )
endif()


if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED mpv)
    if(NOT MPV_INCLUDE_DIR AND MPV_INCLUDE_DIRS)
        list(GET MPV_INCLUDE_DIRS 0 MPV_INCLUDE_DIR)
    endif()
    if(NOT MPV_LIBRARY AND MPV_LIBRARIES)
        find_library(MPV_LIBRARY NAMES ${MPV_LIBRARIES})
    endif()
endif()

if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
    message(FATAL_ERROR
        "libmpv not found (mpv/client.h and -lmpv). Install libmpv-dev, "
        "or extract headers into .deps/usr/include (see README).")
endif()

message(STATUS "libmpv include: ${MPV_INCLUDE_DIR}")
message(STATUS "libmpv library: ${MPV_LIBRARY}")

qt_standard_project_setup()

qt_add_executable(qreolink
    include/MainWindow.h
    include/MpvWidget.h
    include/CameraConfig.h
    include/AesCfb128.h
    include/ReolinkCgiClient.h
    src/main.cpp
    src/MainWindow.cpp
    src/MpvWidget.cpp
    src/AesCfb128.cpp
    src/ReolinkCgiClient.cpp
)

target_include_directories(qreolink PRIVATE
    include
    "${MPV_INCLUDE_DIR}"
)

target_link_libraries(qreolink PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
    "${MPV_LIBRARY}"
)

install(TARGETS qreolink
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/qreolink.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/qreolink.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

set(CPACK_PACKAGE_NAME "qreolink")
set(CPACK_PACKAGE_VENDOR "qreolink")
set(CPACK_PACKAGE_CONTACT "qreolink@localhost")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt + libmpv viewer for Reolink RTSP cameras")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://example.com/qreolink")

set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libmpv2, libqt6core6t64 | libqt6core6, libqt6gui6, libqt6widgets6, libqt6opengl6, libqt6openglwidgets6, libqt6network6t64 | libqt6network6, libgl1, libegl1"
)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "qreolink")

include(CPack)
