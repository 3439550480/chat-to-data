cmake_minimum_required(VERSION 3.12)    # 升级：3.10 → 3.12 以支持 CONFIGURE_DEPENDS

# 项目名称
project(userEntity)

# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 odb 编译器：配置阶段即可清晰报错（而非编译期晦涩错误）
find_program(ODB_EXECUTABLE odb REQUIRED)

# 定义输出目录（在源目录中）——课件式：产物可见、结构即课件
set(ODB_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/odb)
set(SQL_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sql)

# 确保输出目录存在
file(MAKE_DIRECTORY ${ODB_OUTPUT_DIR})
file(MAKE_DIRECTORY ${SQL_OUTPUT_DIR})

# 自动收集data目录下所有的*Entity.h文件
# CONFIGURE_DEPENDS：新增实体文件时构建系统自动重新配置，无需手动重跑 cmake
file(GLOB_RECURSE ENTITY_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*Entity.h")

# 存储所有生成的odb目标
set(ODB_TARGETS "")

# 遍历所有Entity文件，为每个文件生成odb代码和SQL文件
foreach(entity_file ${ENTITY_FILES})
    # 获取文件名（不含路径和扩展名）
    get_filename_component(entity_name ${entity_file} NAME_WE)

    # 获取源文件的相对路径（用于依赖检查）
    file(RELATIVE_PATH rel_entity_file ${CMAKE_CURRENT_SOURCE_DIR} ${entity_file})

    # 生成的odb代码目标名称
    set(odb_target "generate_${entity_name}_odb")

    # 从Entity文件名推断SQL表名
    string(REGEX REPLACE "Entity$" "" table_name ${entity_name})
    string(TOLOWER ${table_name} table_name_lower)
    set(sql_table_name "tbl_${table_name_lower}")

    # 定义生成的文件路径（在源目录中）
    set(generated_odb_hxx "${ODB_OUTPUT_DIR}/${entity_name}-odb.hxx")
    set(generated_odb_cxx "${ODB_OUTPUT_DIR}/${entity_name}-odb.cxx")
    set(generated_odb_ixx "${ODB_OUTPUT_DIR}/${entity_name}-odb.ixx")
    set(generated_sql_file "${SQL_OUTPUT_DIR}/${entity_name}.sql")

    # 创建自定义命令来生成文件
    # 注意：输出文件在源目录，但命令会在build目录执行
    add_custom_command(
        OUTPUT
            ${generated_odb_hxx}
            ${generated_odb_cxx}
            ${generated_odb_ixx}
            ${generated_sql_file}
        COMMAND ${ODB_EXECUTABLE} -d mysql --std c++17
            --generate-query
            --generate-schema
            --output-dir ${ODB_OUTPUT_DIR}
            --schema-name ${sql_table_name}
            ${entity_file}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ODB_OUTPUT_DIR}/${entity_name}.sql
            ${generated_sql_file}
        COMMAND ${CMAKE_COMMAND} -E remove
            ${ODB_OUTPUT_DIR}/${entity_name}.sql
        DEPENDS ${entity_file}
        COMMENT "生成${entity_name}的odb代码和SQL文件到data/odb和data/sql目录"
    )

    # 为每个entity创建一个自定义目标
    add_custom_target(${odb_target}
        DEPENDS
            ${generated_odb_hxx}
            ${generated_odb_cxx}
            ${generated_odb_ixx}
            ${generated_sql_file}
    )

    list(APPEND ODB_TARGETS ${odb_target})
endforeach()

# 汇总目标：生成所有odb代码
add_custom_target(generate_all_odb ALL DEPENDS ${ODB_TARGETS})

# 将源目录下的odb目录添加到包含路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
