cmake_minimum_required(VERSION 3.12)
project(e2eUploadTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 跨服务端到端测试：纯 brpc 客户端 + OpenXLSX 造文件（不链接任何服务侧代码）
add_executable(${PROJECT_NAME}
    main.cc
    ../../../proto/protoCode/fileService.pb.cc
    ../../../proto/protoCode/dbService.pb.cc
    ../../../proto/protoCode/excelParseService.pb.cc   # dbService import 的 WorksheetData 符号
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
)
