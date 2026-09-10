cmake_minimum_required(VERSION 3.12)
project(dbDriverTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# dbDriver + SQLValidator 单测（真 MySQL / 真 SQLite）
add_executable(${PROJECT_NAME}
    dbDriverTest.cc
    ../../../svc_dbService/dbDriver/databaseSchema.cc
    ../../../svc_dbService/dbDriver/mysqlDatabase.cc
    ../../../svc_dbService/dbDriver/sqliteDatabase.cc
    ../../../svc_dbService/dbDriver/databaseFactory.cc
    ../../../common/utils.cpp
    ../../../common/sqlValidator.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
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
