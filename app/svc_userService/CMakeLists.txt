cmake_minimum_required(VERSION 3.12)
project(UserService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件（全部源码 + proto 生成物 + ODB 生成物）
add_executable(${PROJECT_NAME}
    main.cc
    userServer.cc
    userServiceImpl.cc
    userBusiness.cc
    sessionManager.cc
    ../proto/protoCode/userService.pb.cc
    ../data/userData.cc
    ../data/sessionData.cc
    ../data/verifyCodeData.cc
    ../data/odb/sessionEntity-odb.cxx
    ../data/odb/userEntity-odb.cxx
    ../common/utils.cpp           # 课件笔误 utils.cc → utils.cpp
    ../common/errorHandler.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..      # chat2data/ 根（common/ proto/ 所在）
    ${CMAKE_CURRENT_SOURCE_DIR}/../data # data/：odb 生成物 include 解析
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold   # 传递依赖：brpc/protobuf/odb/redis++/etcd/spdlog/jsoncpp 等
    crypt                          # crypt_r（bcrypt 密码加密）
    pthread
)
