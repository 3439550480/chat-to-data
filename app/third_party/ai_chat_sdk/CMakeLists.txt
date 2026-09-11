# ===== 本项目补丁（PATCH 0）：SDK 构建方式调整 =====
# 原 CMakeLists（见 CMakeLists.orig.txt）会 install 到 /usr/local，依赖镜像环境；
# 这里改为「项目内静态库」：任何 target add_subdirectory 后链接 ai_chat_sdk 即可，
# 无需安装、不改系统目录。其余（源文件收集/头文件目录/依赖库）与原版保持一致。
cmake_minimum_required(VERSION 3.10)

set(SDK_NAME "ai_chat_sdk")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 收集源码/头文件
file(GLOB_RECURSE SDK_SOURCES "src/*.cpp")
file(GLOB_RECURSE SDK_HEADERS "include/*.h")

add_library(${SDK_NAME} STATIC ${SDK_SOURCES} ${SDK_HEADERS})

# cpp-httplib 需要 OpenSSL 支持（与 SDK 原版一致）
target_compile_definitions(${SDK_NAME} PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_include_directories(${SDK_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(OpenSSL REQUIRED)
target_link_libraries(${SDK_NAME} PUBLIC
    jsoncpp fmt spdlog sqlite3 OpenSSL::SSL OpenSSL::Crypto)
