cmake_minimum_required(VERSION 3.12)
project(excelParserTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    excelParserTest.cc
    ../../svc_excelParseService/excelParser.cc
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

find_package(bite_scaffold REQUIRED)
find_package(OpenXLSX REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    OpenXLSX::OpenXLSX
    gtest
    pthread
)
