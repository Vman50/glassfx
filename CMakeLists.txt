cmake_minimum_required(VERSION 3.19)
project(glassfx C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fPIC -fvisibility=default)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLESV2  REQUIRED glesv2)
pkg_check_modules(EGL     REQUIRED egl)

# ─── Core static library (shader compilation + GLES rendering, no Hyprland/Qt) ───

add_library(glassfx-core STATIC
    src/ShaderSystem.cpp)

target_include_directories(glassfx-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLESV2_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS})

target_link_libraries(glassfx-core PUBLIC
    ${GLESV2_LIBRARIES}
    ${EGL_LIBRARIES})

target_compile_options(glassfx-core PRIVATE
    ${GLESV2_CFLAGS_OTHER}
    ${EGL_CFLAGS_OTHER})

# ─── Hyprland plugin ─────────────────────────────────────────────────────────

pkg_check_modules(SCENEFX       REQUIRED scenefx-0.4)
pkg_check_modules(WAYLAND_SERVER REQUIRED wayland-server)

add_library(glassfx SHARED
    src/main.cpp
    src/WindowTransformer.cpp
    src/FullscreenPass.cpp
    src/ShaderManager.cpp
    src/Config.cpp
    src/Dispatchers.cpp
    src/TransformerCompat.cpp)

target_include_directories(glassfx PRIVATE
    /usr/include/hyprland
    /usr/include/hyprland/src
    /usr/include/hyprland/protocols
    /usr/include/scenefx-0.4
    ${SCENEFX_INCLUDE_DIRS}
    ${WAYLAND_SERVER_INCLUDE_DIRS})

target_link_libraries(glassfx PRIVATE
    glassfx-core
    ${SCENEFX_LIBRARIES}
    ${WAYLAND_SERVER_LIBRARIES})

target_compile_options(glassfx PRIVATE
    ${SCENEFX_CFLAGS_OTHER}
    ${WAYLAND_SERVER_CFLAGS_OTHER})

install(TARGETS glassfx
    DESTINATION $ENV{HOME}/.config/hypr/plugins/)
install(DIRECTORY shaders/
    DESTINATION $ENV{HOME}/.config/hypr/shaders/glassfx/)

# ─── Quickshell QML extension ────────────────────────────────────────────────

find_package(Qt6 QUIET COMPONENTS Quick Gui Core OpenGL WaylandClient)

if(Qt6_FOUND AND Qt6Quick_FOUND AND Qt6WaylandClient_FOUND)
    find_package(Qt6WaylandScannerTools REQUIRED)

    list(APPEND CMAKE_MODULE_PATH
        "${Qt6_DIR}/../../../lib/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules")
    find_package(WaylandScanner REQUIRED)

    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

    set(QS_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)

    add_library(glassfx-qs SHARED
        src/qs/ScreenCapture.cpp
        src/qs/GlassFXItem.cpp
        src/qs/GlassFXRenderer.cpp
        src/qs/plugin.cpp)

    qt6_generate_wayland_protocol_client_sources(glassfx-qs
        FILES
            ${QS_PROTO_DIR}/ext-foreign-toplevel-list-v1.xml
            ${QS_PROTO_DIR}/ext-image-capture-source-v1.xml
            ${QS_PROTO_DIR}/ext-image-copy-capture-v1.xml)

    # qt6_generate_wayland_protocol_client_sources only adds the Qt C++ wrappers;
    # explicitly compile the raw Wayland C protocol data files for the *_interface symbols.
    set(PROTO_C_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-ext-foreign-toplevel-list-v1-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-ext-image-capture-source-v1-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-ext-image-copy-capture-v1-protocol.c)
    set_source_files_properties(${PROTO_C_FILES} PROPERTIES GENERATED TRUE LANGUAGE C)
    target_sources(glassfx-qs PRIVATE ${PROTO_C_FILES})

    set_target_properties(glassfx-qs PROPERTIES
        AUTOMOC ON
        OUTPUT_NAME glassfxqs)

    target_include_directories(glassfx-qs PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Qt6WaylandClient_PRIVATE_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS})

    target_link_libraries(glassfx-qs PRIVATE
        glassfx-core
        Qt6::Quick
        Qt6::Gui
        Qt6::OpenGL
        Qt6::WaylandClient
        ${WAYLAND_CLIENT_LIBRARIES})

    install(TARGETS glassfx-qs
        DESTINATION $ENV{HOME}/.local/lib/qt6/qml/GlassFX/)

    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/qmldir
        "module GlassFX\nplugin glassfxqs\n")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qmldir
        DESTINATION $ENV{HOME}/.local/lib/qt6/qml/GlassFX/)

    message(STATUS "GlassFX: Quickshell QML extension will be built")
else()
    message(STATUS "GlassFX: Qt6/WaylandClient not found — skipping Quickshell extension")
endif()
