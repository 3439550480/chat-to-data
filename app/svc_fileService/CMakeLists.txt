cmake_minimum_required(VERSION 3.12)
project(FileService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件（全部源码 + 双 proto 生成物 + ODB 生成物 + 数据层）
add_executable(${PROJECT_NAME}
    main.cc
    fileServer.cc
    fileServiceImpl.cc
    fileBusiness.cc
    ../proto/protoCode/fileService.pb.cc
    ../proto/protoCode/excelParseService.pb.cc   # 调用Excel解析子服务所需
    ../data/fileData.cc
    ../data/worksheetData.cc
    ../data/odb/fileEntity-odb.cxx
    ../data/odb/worksheetEntity-odb.cxx
    ../common/utils.cpp
    ../common/errorHandler.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..      # app/ 根（供 ../proto/... 形式的 include 解析）
    ${CMAKE_CURRENT_SOURCE_DIR}/../data # data/：odb 生成物中的 #include "fileEntity.h" 可解析
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold   # 传递依赖：brpc/protobuf/etcd/odb/redis++/fastdfs 等
    pthread
)
