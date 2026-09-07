cmake_minimum_required(VERSION 3.12)
project(NotifyService)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件（全部源码 + proto 生成物）
add_executable(${PROJECT_NAME}
    main.cc
    notifyServer.cc
    notifyServiceImpl.cc
    notifyBusiness.cc
    emailWorker.cc
    emailSender.cc
    verifyCodeEmailSender.cc
    normalEmailSender.cc
    ../proto/protoCode/notifyService.pb.cc
    ../common/errorHandler.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..      # app/ 根（供 ../proto/... 形式的 include 解析）
)

# 注意：须在 add_executable 之后（Finddepends 模块给既有目标挂依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold   # 传递依赖：brpc/protobuf/etcd/spdlog/gflags 等
    curl                           # libcurl（邮件 SMTP 发送，scaffold 不传递，显式链接）
    pthread
)
