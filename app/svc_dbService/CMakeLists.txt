cmake_minimum_required(VERSION 3.12)
project(DatabaseService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件（本服务源码 + 驱动层 + 三份 proto 生成物 + 公共组件）
# 注意：数据库服务不用 ODB（走原始 C API 动态造表），故无 data/ 依赖
add_executable(${PROJECT_NAME}
    main.cc
    dbServer.cc
    dbServiceImpl.cc
    dbBusiness.cc
    dbConnMgr.cc
    dbDriver/databaseSchema.cc
    dbDriver/mysqlDatabase.cc
    dbDriver/sqliteDatabase.cc
    dbDriver/databaseFactory.cc
    ../proto/protoCode/dbService.pb.cc
    ../proto/protoCode/excelParseService.pb.cc
    ../proto/protoCode/fileService.pb.cc
    ../common/utils.cpp
    ../common/errorHandler.cpp
    ../common/sqlValidator.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..            # app/ 根
    ${CMAKE_CURRENT_SOURCE_DIR}/dbDriver      # 驱动头文件
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold   # 传递依赖：brpc/protobuf/etcd/fastdfs/spdlog/gflags 等
    mysqlclient                    # MySQL C API
    sqlite3                        # SQLite C API
    pthread
)
