#pragma once

#include <string>
#include <cstddef>
#include <odb/core.hxx>

// 数据库表名：tbl_worksheet（worksheet → 数据库表 映射表）
// 外键级联决策（F2）：不建 FK，采用方案A——业务层 deleteFile 显式删除映射行
#pragma db object table("tbl_worksheet")
class WorksheetEntity {
public:
    WorksheetEntity() {}
    WorksheetEntity(const std::string& fileId,
                    const std::string& worksheetName,
                    const std::string& tableName)
        : _fileId(fileId), _worksheetName(worksheetName), _tableName(tableName) {}

    unsigned long long id() const { return _id; }
    void setId(unsigned long long id) { _id = id; }

    std::string fileId() const { return _fileId; }
    void setFileId(const std::string& fileId) { _fileId = fileId; }

    std::string worksheetName() const { return _worksheetName; }
    void setWorksheetName(const std::string& worksheetName) { _worksheetName = worksheetName; }

    std::string tableName() const { return _tableName; }
    void setTableName(const std::string& tableName) { _tableName = tableName; }

private:
    friend class odb::access;

    #pragma db id auto
    unsigned long long _id;

    // 索引：fileId（按文件查映射的高频路径）
    #pragma db column("fileId") type("VARCHAR(32) CHARACTER SET utf8mb4") index
    std::string _fileId;

    #pragma db column("worksheetName") type("VARCHAR(32) CHARACTER SET utf8mb4")
    std::string _worksheetName;

    #pragma db column("tableName") type("VARCHAR(64) CHARACTER SET utf8mb4")
    std::string _tableName;
};
