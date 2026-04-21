cmake_minimum_required(VERSION 3.21)
project(qreolink LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

qt_standard_project_setup()

qt_add_executable(qreolink
    include/MainWindow.h
    include/MpvWidget.h
    include/CameraConfig.h
    src/main.cpp
    src/MainWindow.cpp
    src/MpvWidget.cpp
)

target_include_directories(qreolink PRIVATE include)

target_link_libraries(qreolink PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    PkgConfig::MPV
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
    "libmpv2, libqt6core6t64 | libqt6core6, libqt6gui6, libqt6widgets6, libqt6opengl6, libqt6openglwidgets6, libgl1, libegl1"
)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "qreolink")

include(CPack)
