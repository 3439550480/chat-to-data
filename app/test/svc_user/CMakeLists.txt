cmake_minimum_required(VERSION 3.12)
project(userDataTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 测试可执行：userData.cc + ODB 生成物 + gtest
add_executable(${PROJECT_NAME}
    userDataTest.cc
    ../../data/userData.cc
    ../../data/odb/userEntity-odb.cxx
    ../../common/utils.cpp          # generateUuid（测试用随机身份）
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..        # app/ 根（common/ data/ svc_userService/）
    ${CMAKE_CURRENT_SOURCE_DIR}/../../data   # data/：让 odb 生成物中的 #include "userEntity.h" 可解析
)

# bite_scaffold：提供 DBFactory/RedisFactory 及 ODB 运行时依赖
# 注意：须在 add_executable 之后 find_package（其 Finddepends 模块会给既有目标挂链接依赖）
find_package(bite_scaffold REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
)

# ---- SessionData 测试（独立可执行，各自 main）----
add_executable(sessionDataTest
    sessionDataTest.cc
    ../../data/sessionData.cc
    ../../data/odb/sessionEntity-odb.cxx
    ../../common/utils.cpp          # generateUuid（测试用随机身份）
)
target_include_directories(sessionDataTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../data
)
target_link_libraries(sessionDataTest PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
)

# ---- VerifyCodeData 测试（纯 Redis，无 ODB）----
add_executable(verifyCodeDataTest
    verifyCodeDataTest.cc
    ../../data/verifyCodeData.cc
    ../../common/utils.cpp
)
target_include_directories(verifyCodeDataTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(verifyCodeDataTest PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
)

# ---- UserBusiness 测试（完整装配：数据层三件套 + SessionManager + UserBusiness）----
add_executable(userBusinessTest
    userBusinessTest.cc
    ../../data/userData.cc
    ../../data/sessionData.cc
    ../../data/verifyCodeData.cc
    ../../data/odb/userEntity-odb.cxx
    ../../data/odb/sessionEntity-odb.cxx
    ../../svc_userService/sessionManager.cc
    ../../svc_userService/userBusiness.cc
    ../../proto/protoCode/notifyService.pb.cc   # N9：getVerifyCode 调通知子服务所需
    ../../common/utils.cpp
    ../../common/errorHandler.cpp   # Chat2DataException 实现
)
target_include_directories(userBusinessTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../data
)
target_link_libraries(userBusinessTest PRIVATE
    bite_scaffold::bite_scaffold
    gtest
    pthread
    crypt
)
