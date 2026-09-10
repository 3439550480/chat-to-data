#pragma once
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <cstdint>
#include <gflags/gflags.h>
#include <bite_scaffold/rpc.h>
#include "../data/fileData.h"
#include "../data/worksheetData.h"
#include "common.h"
#include "../proto/protoCode/fileService.pb.h"

// 课件问题㉜修复：业务层使用 FLAGS_excel_service / FLAGS_db_service（调用 key），
// 课件只在 main.cc 定义了 excel_parser_service_name（watch 用）——名字对不上，编译必炸。
// 与用户子服务㉓同族：watch 管发现，这里管调用 key，两组 flag 分离
DECLARE_string(excel_service);
DECLARE_string(db_service);

namespace fileService {

class FileBusiness {
public:
    FileBusiness(std::shared_ptr<FileInfoData> fileInfoData,
                 std::shared_ptr<WorksheetData> worksheetData,
                 std::shared_ptr<biterpc::SvcChannels> svcChannels);
    // ---------- 元数据组（F5） ----------
    // 对应上传文件信息的RPC接口：将文件信息保存或更新到MySQL和缓存
    // @return fileId
    // @throws FILE_INFO_SAVE_FAILED
    std::string saveFileInfo(const FileInfo& fileInfo);
    // 对应获取文件信息的RPC接口：缓存优先，未命中回源+回填
    // @throws FILE_PERMISSION_DENIED / FILE_NOT_FOUND
    FileInfo getFileInfo(const std::string& fileId, const std::string& userId);
    // 对应获取文件列表的RPC接口
    std::vector<FileInfo> getFileList(const std::string& userId);
    // 对应关联文件和聊天会话的RPC接口（AI子服务部分等17章完善）
    // @throws FILE_NOT_FOUND / FILE_INFO_SAVE_FAILED
    bool associateFileChatSession(const std::string& fileId,
                                  const std::string& chatSessionId,
                                  const std::string& userId);
    // 对应获取Worksheet数据库表名列表的RPC接口（AI子服务构建提示词用）
    std::vector<std::string> getWorksheetDBTables(const std::string& fileId);
    // ---------- 文件数据组（F6） ----------
    // 对应上传Excel文件的RPC接口：FastDFS 上传 → 映射落库 → 解析+入库（入库半截桩）
    // @throws FILE_UPLOAD_FASTDFS_FAILED / FILE_NOT_FOUND / FILE_INFO_SAVE_FAILED /
    //         FILE_EXCEL_WORKSHEETS_GET_FAILED / FILE_WORKSHEET_SAVE_FAILED / FILE_EXCEL_PARSE_FAILED
    std::string uploadExcelFile(const std::string& sessionId,
                                const std::string& fileId, const std::string& userId,
                                const std::string& fileName, const std::string& fileData);
    // 对应上传SQLite文件的RPC接口：仅上传存储，无后续解析
    // @throws FILE_SQLITE_UPLOAD_FAILED / FILE_NOT_FOUND / FILE_INFO_SAVE_FAILED
    std::string uploadSQLiteFile(const std::string& sessionId,
                                 const std::string& fileId, const std::string& userId,
                                 const std::string& filename, const std::string& fileData);
    // 对应下载Excel文件的RPC接口（归属校验后取文件数据，供 attachment 回传）
    // @throws FILE_NOT_FOUND
    std::string downloadExcelFile(const std::string& fileId, const std::string& userId);
    // 对应获取SQLite文件信息的RPC接口（返回 FastDFS 文件id，数据库子服务用）
    // @throws FILE_SQLITE_FILE_NOT_FOUND
    std::string downloadSQLiteFile(const std::string& fileId);
    // 对应删除文件的RPC接口：FastDFS 删除 + worksheet 映射显式级联删除（方案A） + 元数据删除
    // @throws FILE_NOT_FOUND / FILE_DELETE_FAILED
    bool deleteFile(const std::string& fileId, const std::string& userId);
    // 仅删除数据库和缓存中的文件信息（SQLite 上传失败回滚用）
    bool deleteFileInfo(const std::string& fileId);
    // 对应预览Excel文件的RPC接口：worksheet 映射 → 数据库子服务取分页数据（桩，等数据库子服务）
    // @throws FILE_NOT_FOUND / FILE_EXCEL_PARSE_FAILED
    chat2Data::fileService::ExcelData previewExcel(const std::string& fileId,
                                                   const std::string& userId,
                                                   int32_t pageNumber, int32_t pageSize);
private:
    // ---------- FastDFS 三件套 ----------
    std::optional<std::string> uploadToFastDFS(const std::string& fileData,
                                               const std::string& filename);
    std::optional<std::string> downloadFromFastDFS(const std::string& fdfsFileId);
    bool deleteFromFastDFS(const std::string& fdfsFileId);
    // ---------- 跨服务协作 ----------
    // 通过Excel解析子服务获取worksheet表名列表
    std::vector<std::string> getWorksheetList(const std::string& fdfsFileId);
    // 计算worksheet对应的数据库表名（㉝修复：fileId 连字符一并清洗）
    std::string calculateTableName(const std::string& worksheetName, const std::string& fileId);
    // 通过Excel解析子服务解析数据并交数据库子服务入库（入库半截桩，等数据库子服务章节）
    bool importExcelData2DB(const std::string& fdfsFileId,
                            const std::string& fileId,
                            const std::vector<std::string>& worksheets);
    // 通过数据库子服务获取Excel表数据（桩，等数据库子服务章节）
    chat2Data::fileService::ExcelData getExcelDataFromDB(
        const std::string& fileId,
        const std::vector<WorksheetEntity>& worksheets,
        int32_t pageNumber, int32_t pageSize);
private:
    std::shared_ptr<FileInfoData> _fileInfoData;          // 文件元数据操作
    std::shared_ptr<WorksheetData> _worksheetData;        // worksheet映射操作
    std::shared_ptr<biterpc::SvcChannels> _svcChannels;   // 跨服务通道
};

} // namespace fileService
