cmake_minimum_required(VERSION 3.12)
project(excelChatE2e)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 智能 Excel 全链路 E2E：brpc 客户端（文件/DB/AI 三服务）+ httplib（AI 的 HTTP+SSE）+ OpenXLSX 造文件
add_executable(${PROJECT_NAME}
    main.cc
    ../../../proto/protoCode/fileService.pb.cc
    ../../../proto/protoCode/dbService.pb.cc
    ../../../proto/protoCode/excelParseService.pb.cc   # dbService import 的 WorksheetData 符号
    ../../../proto/protoCode/aiService.pb.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)

find_package(bite_scaffold REQUIRED)
find_package(OpenXLSX REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    OpenXLSX::OpenXLSX
    pthread
    ssl crypto    # httplib 的 CPPHTTPLIB_OPENSSL_SUPPORT 需要
)
