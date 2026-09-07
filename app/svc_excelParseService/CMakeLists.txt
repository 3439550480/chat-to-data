cmake_minimum_required(VERSION 3.12)
project(ExcelParserService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件（全部源码 + proto 生成物）
add_executable(${PROJECT_NAME}
    main.cc
    excelParserServer.cc
    excelParserServiceImpl.cc
    excelParserBusiness.cc
    excelParser.cc
    ../proto/protoCode/excelParseService.pb.cc
    ../common/utils.cpp           # generateUuid（㉘：临时文件 UUID 命名）
    ../common/errorHandler.cpp    # 课件笔误 errorHandler.cc → 实际为 .cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..      # app/ 根（供 ../proto/... 形式的 include 解析）
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)
find_package(OpenXLSX REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold   # 传递依赖：brpc/protobuf/etcd/fastdfs/spdlog/gflags 等
    OpenXLSX::OpenXLSX             # Excel解析库（显式链接）
    pthread
)
