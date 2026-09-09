cmake_minimum_required(VERSION 3.12)
project(fileDataTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 测试可执行：数据层两件套 + ODB 生成物 + gtest（真 MySQL/Redis）
add_executable(${PROJECT_NAME}
    fileDataTest.cc
    ../../data/fileData.cc
    ../../data/worksheetData.cc
    ../../data/odb/fileEntity-odb.cxx
    ../../data/odb/worksheetEntity-odb.cxx
    ../../common/utils.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..        # app/ 根
    ${CMAKE_CURRENT_SOURCE_DIR}/../../data   # data/：odb 生成物 include 解析
)

find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
)
