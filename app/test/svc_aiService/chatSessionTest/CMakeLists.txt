cmake_minimum_required(VERSION 3.12)
project(chatSessionTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 聊天会话数据层 + 管理器单测（真 MySQL + Redis；不依赖 ChatSDK/其他微服务）
add_executable(${PROJECT_NAME}
    main.cc
    ../../../data/chatSessionData.cc
    ../../../data/odb/chatSessionEntity-odb.cxx
    ../../../svc_aiService/chatSessionMgr.cc
    ../../../common/utils.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../data
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../data/odb
)

find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
)
