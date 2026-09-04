# CMake最小版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(GatewayService)

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加可执行文件目标
add_executable(${PROJECT_NAME}
    main.cc
    gatewayServer.cc
    gatewayServiceImpl.cc
)

# 依赖检查
# bite_scaffold：微服务脚手架，提供服务发现、RPC通信等基础组件
# OpenSSL：提供SSL/TLS加密支持（配合 CPPHTTPLIB_OPENSSL_SUPPORT 启用 httplib 的 HTTPS 能力）
find_package(bite_scaffold REQUIRED)
find_package(OpenSSL REQUIRED)

# 设置链接库
# bite_scaffold::bite_scaffold：微服务脚手架核心库
# OpenSSL::SSL/Crypto：OpenSSL加密库
# sqlite3：SQLite数据库（系统库，无需 find_package）
target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    OpenSSL::SSL
    OpenSSL::Crypto
    sqlite3
)

# 编译宏定义
# CPPHTTPLIB_OPENSSL_SUPPORT：启用cpp-httplib库的OpenSSL支持
target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
