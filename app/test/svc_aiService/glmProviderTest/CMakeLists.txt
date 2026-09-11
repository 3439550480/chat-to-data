cmake_minimum_required(VERSION 3.12)
project(glmProviderTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# H1.5 冒烟：ChatSDK 补丁（GLM 接入）+ .env 读取
add_subdirectory(../../../third_party/ai_chat_sdk ${CMAKE_BINARY_DIR}/ai_chat_sdk)

add_executable(${PROJECT_NAME}
    main.cc
    ../../../common/envLoader.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ai_chat_sdk
    bite_scaffold::bite_scaffold
    pthread
)
