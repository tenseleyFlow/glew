cmake_minimum_required(VERSION 3.16)
project(glew VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-format-truncation)

# libuv
find_package(PkgConfig REQUIRED)
pkg_check_modules(UV REQUIRED libuv)

# Platform-specific input sources and libraries
if(APPLE)
    set(INPUT_SOURCES
        src/input/capture_macos.c
        src/input/inject_macos.c
        src/input/keymap_macos.c
    )
    set(INPUT_LIBS
        "-framework CoreGraphics"
        "-framework ApplicationServices"
        "-framework CoreFoundation"
    )
else()
    pkg_check_modules(X11 REQUIRED x11)
    pkg_check_modules(XTST REQUIRED xtst)
    set(INPUT_SOURCES
        src/input/capture_x11.c
        src/input/inject_x11.c
    )
    set(INPUT_LIBS ${X11_LIBRARIES} ${XTST_LIBRARIES})
    set(INPUT_INCLUDE_DIRS ${X11_INCLUDE_DIRS} ${XTST_INCLUDE_DIRS})
    set(INPUT_LIBRARY_DIRS ${X11_LIBRARY_DIRS} ${XTST_LIBRARY_DIRS})
endif()

# Embedded deps
add_library(cjson STATIC deps/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_SOURCE_DIR}/deps)

add_library(tomlc99 STATIC deps/toml.c)
target_include_directories(tomlc99 PUBLIC ${CMAKE_SOURCE_DIR}/deps)

# Shared sources (used by both glew and glewd)
set(COMMON_SOURCES
    src/config.c
    src/log.c
    src/layout.c
    src/drivers/driver.c
    src/drivers/i3.c
    src/drivers/tarmac.c
    src/drivers/gar.c
)

# glewd-specific sources
set(DAEMON_SOURCES
    src/net/proto.c
    src/net/peer.c
    ${INPUT_SOURCES}
)

# glew CLI
add_executable(glew
    src/glew.c
    src/net/proto.c
    ${COMMON_SOURCES}
)
target_include_directories(glew PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(glew PRIVATE cjson tomlc99)

# glewd daemon
add_executable(glewd
    src/glewd.c
    ${COMMON_SOURCES}
    ${DAEMON_SOURCES}
)
target_include_directories(glewd PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${UV_INCLUDE_DIRS}
    ${INPUT_INCLUDE_DIRS}
)
target_link_libraries(glewd PRIVATE cjson tomlc99 ${UV_LIBRARIES} ${INPUT_LIBS})
target_link_directories(glewd PRIVATE ${UV_LIBRARY_DIRS} ${INPUT_LIBRARY_DIRS})

# Install
install(TARGETS glew glewd RUNTIME DESTINATION bin)
