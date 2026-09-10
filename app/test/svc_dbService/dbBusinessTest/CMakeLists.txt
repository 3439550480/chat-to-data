cmake_minimum_required(VERSION 3.12)
project(dbBusinessTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DBBusiness 业务层单测（真 MySQL；不依赖其他微服务）
add_executable(${PROJECT_NAME}
    dbBusinessTest.cc
    ../../../svc_dbService/dbConnMgr.cc
    ../../../svc_dbService/dbBusiness.cc
    ../../../svc_dbService/dbDriver/databaseSchema.cc
    ../../../svc_dbService/dbDriver/mysqlDatabase.cc
    ../../../svc_dbService/dbDriver/sqliteDatabase.cc
    ../../../svc_dbService/dbDriver/databaseFactory.cc
    ../../../proto/protoCode/dbService.pb.cc
    ../../../proto/protoCode/excelParseService.pb.cc
    ../../../proto/protoCode/fileService.pb.cc
    ../../../common/utils.cpp
    ../../../common/errorHandler.cpp
    ../../../common/sqlValidator.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../svc_dbService
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../svc_dbService/dbDriver
)

find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    mysqlclient
    sqlite3
    gtest
    pthread
)
