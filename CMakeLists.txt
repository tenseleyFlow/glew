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
target_include_directories(glewd PRIVATE ${CMAKE_SOURCE_DIR}/src ${UV_INCLUDE_DIRS})
target_link_libraries(glewd PRIVATE cjson tomlc99 ${UV_LIBRARIES})
target_link_directories(glewd PRIVATE ${UV_LIBRARY_DIRS})

# Install
install(TARGETS glew glewd RUNTIME DESTINATION bin)
