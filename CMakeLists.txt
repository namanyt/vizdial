cmake_minimum_required(VERSION 3.20)
project(vizdial VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(KF6WindowSystem QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)

add_executable(vizdial
    src/main.cpp
    src/config.cpp
    src/spectrum_feed.cpp
    src/radial_widget.cpp
    resources.qrc
)

target_link_libraries(vizdial PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    PkgConfig::FFTW3F
    PkgConfig::LIBPULSE
)

if(KF6WindowSystem_FOUND)
    target_compile_definitions(vizdial PRIVATE HAVE_KF6_WINDOW_SYSTEM=1)
    target_link_libraries(vizdial PRIVATE KF6::WindowSystem)
endif()

target_compile_options(vizdial PRIVATE -Wall -Wextra)
if(NOT CMAKE_BUILD_TYPE)
    target_compile_options(vizdial PRIVATE -O2)
endif()
