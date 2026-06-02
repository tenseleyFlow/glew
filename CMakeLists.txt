cmake_minimum_required(VERSION 3.16)
project(glew VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

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

# glew CLI
add_executable(glew
    src/glew.c
    ${COMMON_SOURCES}
)
target_include_directories(glew PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(glew PRIVATE cjson tomlc99)

# glewd daemon
add_executable(glewd
    src/glewd.c
    ${COMMON_SOURCES}
)
target_include_directories(glewd PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(glewd PRIVATE cjson tomlc99)

# Install
install(TARGETS glew glewd RUNTIME DESTINATION bin)
