cmake_minimum_required(VERSION 3.10)
project(DS5Server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 PipeWire 和 SPA 依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(SPA REQUIRED libspa-0.2)

# 添加可执行文件
add_executable(ds5server
    src/main.cpp
    src/audio.cpp
    src/ds5.cpp
)

# 包含目录
target_include_directories(ds5server PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SPA_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

# 链接库
target_link_libraries(ds5server PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${SPA_LIBRARIES}
    pthread
)

# 编译选项
target_compile_options(ds5server PRIVATE -Wall -Wextra)
