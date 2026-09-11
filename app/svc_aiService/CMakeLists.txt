cmake_minimum_required(VERSION 3.12)
project(AIService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ChatSDK 以项目内静态库接入（add_subdirectory；替代课件黑盒 ${CHAT_SDK_SOURCES}——AI7）
add_subdirectory(../third_party/ai_chat_sdk ${CMAKE_BINARY_DIR}/ai_chat_sdk)

# 可执行文件（服务源码 + 6 份 proto 生成物 + 数据层/ODB + 公共组件）
# proto 说明：aiService(自身) + dbService(SQL执行/表结构/采样) + fileService(worksheet表名)
#           + notifyService(发邮件) + userService(取邮箱) —— 19 章四条依赖线
add_executable(${PROJECT_NAME}
    main.cc
    aiServer.cc
    aiServiceImpl.cc
    aiBusiness.cc
    chatSessionMgr.cc
    ../proto/protoCode/aiService.pb.cc
    ../proto/protoCode/dbService.pb.cc
    ../proto/protoCode/excelParseService.pb.cc   # dbService import 的 WorksheetData 符号
    ../proto/protoCode/fileService.pb.cc
    ../proto/protoCode/notifyService.pb.cc
    ../proto/protoCode/userService.pb.cc
    ../data/chatSessionData.cc
    ../data/odb/chatSessionEntity-odb.cxx
    ../common/errorHandler.cpp
    ../common/utils.cpp
    ../common/envLoader.cpp
    ../common/sqlValidator.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # app/ 根（common/utils 等）
    ${CMAKE_CURRENT_SOURCE_DIR}/../data
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/odb
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ai_chat_sdk                    # 含 httplib(OpenSSL)/jsoncpp/spdlog/sqlite3
    bite_scaffold::bite_scaffold   # brpc/odb-mysql/redis++/etcd/gflags 等
    pthread
)
