cmake_minimum_required(VERSION 3.19)
project(glassfx CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fPIC -fvisibility=default)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SCENEFX REQUIRED scenefx-0.4)
pkg_check_modules(GLESV2 REQUIRED glesv2)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)

add_library(glassfx SHARED
    src/main.cpp
    src/ShaderSystem.cpp
    src/WindowTransformer.cpp
    src/FullscreenPass.cpp
    src/ShaderManager.cpp
    src/Config.cpp
    src/Dispatchers.cpp
    src/TransformerCompat.cpp
)

target_include_directories(glassfx PRIVATE
    /usr/include/hyprland
    /usr/include/hyprland/src
    /usr/include/hyprland/protocols
    /usr/include/scenefx-0.4
    ${SCENEFX_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS}
)

target_link_libraries(glassfx PRIVATE
    ${SCENEFX_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${EGL_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES}
)

target_compile_options(glassfx PRIVATE
    ${SCENEFX_CFLAGS_OTHER}
    ${GLESV2_CFLAGS_OTHER}
    ${EGL_CFLAGS_OTHER}
)

install(TARGETS glassfx
    DESTINATION $ENV{HOME}/.config/hypr/plugins/)
install(DIRECTORY shaders/
    DESTINATION $ENV{HOME}/.config/hypr/shaders/glassfx/)
