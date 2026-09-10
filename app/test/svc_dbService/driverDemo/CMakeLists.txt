cmake_minimum_required(VERSION 3.12)
project(dbDriverDemo)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 双驱动样例：dbDriver 四件套 + utils（base64/utf8）
# 路径说明：本目录 app/test/svc_dbService/driverDemo → 三层 ../ 回到 app/
add_executable(${PROJECT_NAME}
    main.cc
    ../../../svc_dbService/dbDriver/databaseSchema.cc
    ../../../svc_dbService/dbDriver/mysqlDatabase.cc
    ../../../svc_dbService/dbDriver/sqliteDatabase.cc
    ../../../svc_dbService/dbDriver/databaseFactory.cc
    ../../../common/utils.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..                      # app/ 根
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../svc_dbService/dbDriver  # dbDriver 头文件
)

find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    mysqlclient   # MySQL C API
    sqlite3       # SQLite C API
    pthread
)
