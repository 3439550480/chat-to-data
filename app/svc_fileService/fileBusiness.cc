#include <fstream>
#include <sys/stat.h>
#include <jsoncpp/json/json.h>
#include <bite_scaffold/log.h>
#include "../common/errorHandler.h"
#include "../common/utils.h"
#include "fileBusiness.h"
// 课件问题㉚纪律：pb.h 已随 fileBusiness.h 拉入，fdfs.h 必须在其后 + #undef byte 双保险
#include "../proto/protoCode/excelParseService.pb.h"
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte
#endif

namespace fileService {

FileBusiness::FileBusiness(std::shared_ptr<FileInfoData> fileInfoData,
                           std::shared_ptr<WorksheetData> worksheetData,
                           std::shared_ptr<biterpc::SvcChannels> svcChannels)
    : _fileInfoData(fileInfoData)
    , _worksheetData(worksheetData)
    , _svcChannels(svcChannels) {
    INF("FileBusiness initialized");
}

// 保存或更新文件信息到数据库（写策略：写DB成功后，再删除Redis缓存）
std::string FileBusiness::saveFileInfo(const FileInfo& fileInfo) {
    // 1. 构建FileInfoEntity实例
    FileInfoEntity entity(fileInfo._fileId,
                          fileInfo._fileName,
                          fileInfo._fileExt,
                          fileInfo._fileSize,
                          fileInfo._uploadTime,
                          fileInfo._fdfsFileId,
                          fileInfo._userId,
                          fileInfo._chatSessionId);
    // 2. 保存或更新到数据库
    if (!_fileInfoData->saveFileInfoToDb(entity)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_INFO_SAVE_FAILED);
    }
    // 3. 删除旧缓存（下次读取回源新数据）
    _fileInfoData->deleteFileInfoCache(fileInfo._fileId);
    INF("FileInfo saved: fileId={}, fileName={}", fileInfo._fileId, fileInfo._fileName);
    return fileInfo._fileId;
}

// 获取文件信息（读策略：缓存命中直返；未命中回源+回填）
FileInfo FileBusiness::getFileInfo(const std::string& fileId, const std::string& userId) {
    // 1. 先查缓存
    auto cacheResult = _fileInfoData->getFileInfoFromCacheByFileId(fileId);
    if (cacheResult.has_value()) {
        // 2. 缓存命中：归属校验（文件不属于当前用户=无权限）
        FileInfoEntity entity = cacheResult.value();
        if (entity.userId() != userId) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_PERMISSION_DENIED);
        }
        // 3. 转换返回
        FileInfo fileInfo;
        fileInfo._fileId = entity.fileId();
        fileInfo._fileName = entity.fileName();
        fileInfo._fileSize = entity.fileSize();
        fileInfo._uploadTime = entity.uploadTime();
        fileInfo._fileExt = entity.fileExt();
        fileInfo._fdfsFileId = entity.fdfsFileId();
        fileInfo._userId = entity.userId();
        fileInfo._chatSessionId = entity.chatSessionId();
        return fileInfo;
    }
    // 4. 缓存未命中：回源DB（带归属校验）
    auto dbResult = _fileInfoData->getFileInfoByFileIdAndUserIdFromDb(fileId, userId);
    if (!dbResult.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_NOT_FOUND);
    }
    // 5. 回填缓存
    FileInfoEntity entity = dbResult.value();
    _fileInfoData->saveFileInfoToCache(entity);
    // 6. 转换返回
    FileInfo fileInfo;
    fileInfo._fileId = entity.fileId();
    fileInfo._fileName = entity.fileName();
    fileInfo._fileSize = entity.fileSize();
    fileInfo._uploadTime = entity.uploadTime();
    fileInfo._fileExt = entity.fileExt();
    fileInfo._fdfsFileId = entity.fdfsFileId();
    fileInfo._userId = entity.userId();
    fileInfo._chatSessionId = entity.chatSessionId();
    return fileInfo;
}

// 获取指定用户的文件列表（列表场景直接走DB，不做逐条缓存）
std::vector<FileInfo> FileBusiness::getFileList(const std::string& userId) {
    // 1. 从MySQL获取指定用户的文件信息
    auto dbResults = _fileInfoData->getFileListByUserIdFromDb(userId);
    // 2. 转换为FileInfo对象
    std::vector<FileInfo> fileList;
    fileList.reserve(dbResults.size());
    for (const auto& entity : dbResults) {
        FileInfo fileInfo;
        fileInfo._fileId = entity.fileId();
        fileInfo._fileName = entity.fileName();
        fileInfo._fileSize = entity.fileSize();
        fileInfo._uploadTime = entity.uploadTime();
        fileInfo._fileExt = entity.fileExt();
        fileInfo._fdfsFileId = entity.fdfsFileId();
        fileInfo._userId = entity.userId();
        fileInfo._chatSessionId = entity.chatSessionId();
        fileList.push_back(fileInfo);
    }
    return fileList;
}

// 关联文件和聊天会话（更新chatSessionId；AI子服务侧的联动等17章完善）
bool FileBusiness::associateFileChatSession(const std::string& fileId,
                                            const std::string& chatSessionId,
                                            const std::string& userId) {
    // 1. 从数据库获取文件信息（带归属校验）
    auto dbResult = _fileInfoData->getFileInfoByFileIdAndUserIdFromDb(fileId, userId);
    if (!dbResult.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_NOT_FOUND);
    }
    // 2. 更新聊天会话ID
    FileInfoEntity entity = dbResult.value();
    entity.setChatSessionId(chatSessionId);
    // 3. 保存到数据库并删缓存
    if (!_fileInfoData->saveFileInfoToDb(entity)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_INFO_SAVE_FAILED);
    }
    _fileInfoData->deleteFileInfoCache(fileId);
    // 4. TODO(17章AI子服务)：更新聊天会话中的文件Id
    return true;
}

// 获取指定文件ID对应的worksheet数据库表名列表（AI子服务用）
std::vector<std::string> FileBusiness::getWorksheetDBTables(const std::string& fileId) {
    // 1. 缓存优先
    auto cacheResult = _worksheetData->getWorksheetMappingsFromCache(fileId);
    if (cacheResult.has_value()) {
        std::vector<std::string> tableNames;
        tableNames.reserve(cacheResult.value().size());
        for (const auto& worksheet : cacheResult.value()) {
            tableNames.push_back(worksheet.tableName());
        }
        INF("Got worksheet tables from cache: fileId={}, count={}",
            fileId, tableNames.size());
        return tableNames;
    }
    // 2. 回源DB
    auto dbResult = _worksheetData->getWorksheetMappingsByFileIdFromDb(fileId);
    if (dbResult.empty()) {
        WRN("No worksheet tables found: fileId={}", fileId);
        return {};
    }
    // 3. 回填缓存
    _worksheetData->saveWorksheetMappingsToCache(fileId, dbResult);
    // 4. 提取表名列表
    std::vector<std::string> tableNames;
    tableNames.reserve(dbResult.size());
    for (const auto& worksheet : dbResult) {
        tableNames.push_back(worksheet.tableName());
    }
    INF("Got worksheet tables from db: fileId={}, count={}", fileId, tableNames.size());
    return tableNames;
}

// ==================== 文件数据组 ====================

// 上传Excel文件：FastDFS 上传 → 更新元数据 → 获取表名列表 → 映射落库 → 解析+入库
std::string FileBusiness::uploadExcelFile(const std::string& sessionId,
                                          const std::string& fileId, const std::string& userId,
                                          const std::string& fileName, const std::string& fileData) {
    // 1. 上传文件到FastDFS
    auto fdfsFileIdOpt = uploadToFastDFS(fileData, fileName);
    if (!fdfsFileIdOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_UPLOAD_FASTDFS_FAILED);
    }
    // 2. 更新元数据中的 fdfsFileId 字段并同步缓存（写DB后删缓存）
    auto dbResult = _fileInfoData->getFileInfoByFileIdFromDb(fileId);
    if (!dbResult.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_NOT_FOUND);
    }
    FileInfoEntity entity = dbResult.value();
    entity.setFdfsFileId(fdfsFileIdOpt.value());
    if (!_fileInfoData->saveFileInfoToDb(entity)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_INFO_SAVE_FAILED);
    }
    _fileInfoData->deleteFileInfoCache(fileId);
    // 3. 通过Excel解析子服务获取worksheet表名列表（传FastDFS文件id而非本地路径）
    auto worksheets = getWorksheetList(fdfsFileIdOpt.value());
    if (worksheets.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_EXCEL_WORKSHEETS_GET_FAILED);
    }
    // 4. 保存worksheet→数据库表映射（表名经㉝修复后的清洗规则生成）
    for (const auto& worksheetName : worksheets) {
        std::string tableName = calculateTableName(worksheetName, fileId);
        WorksheetEntity worksheetEntity(fileId, worksheetName, tableName);
        if (!_worksheetData->saveWorksheetMappingToDb(worksheetEntity)) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_WORKSHEET_SAVE_FAILED);
        }
    }
    // 5. 通过Excel解析子服务解析数据，并交数据库子服务入库（入库半截桩，㉜）
    if (!importExcelData2DB(fdfsFileIdOpt.value(), fileId, worksheets)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_EXCEL_PARSE_FAILED);
    }
    INF("Excel file uploaded successfully: fileId={}, fdfsFileId={}", fileId, fdfsFileIdOpt.value());
    return fdfsFileIdOpt.value();
}

// 上传SQLite文件：仅存储（无解析、无入库）
std::string FileBusiness::uploadSQLiteFile(const std::string& sessionId,
                                           const std::string& fileId, const std::string& userId,
                                           const std::string& filename, const std::string& fileData) {
    // 1. 直接上传到FastDFS
    auto fdfsFileIdOpt = uploadToFastDFS(fileData, filename);
    if (!fdfsFileIdOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_SQLITE_UPLOAD_FAILED);
    }
    // 2. 更新元数据中的 fdfsFileId 并同步缓存
    auto dbResult = _fileInfoData->getFileInfoByFileIdFromDb(fileId);
    if (!dbResult.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_NOT_FOUND);
    }
    FileInfoEntity entity = dbResult.value();
    entity.setFdfsFileId(fdfsFileIdOpt.value());
    if (!_fileInfoData->saveFileInfoToDb(entity)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_INFO_SAVE_FAILED);
    }
    _fileInfoData->deleteFileInfoCache(fileId);
    INF("SQLite file uploaded successfully: fileId={}, fdfsFileId={}", fileId, fdfsFileIdOpt.value());
    return fdfsFileIdOpt.value();
}

// 下载Excel文件（归属校验 → FastDFS 取数据，供 handler 走 attachment 回传）
std::string FileBusiness::downloadExcelFile(const std::string& fileId, const std::string& userId) {
    // 1. 获取文件信息（含归属校验）
    auto fileInfo = getFileInfo(fileId, userId);
    // 2. 从FastDFS下载
    auto fileDataOpt = downloadFromFastDFS(fileInfo._fdfsFileId);
    if (!fileDataOpt.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_NOT_FOUND);
    }
    return fileDataOpt.value();
}

// 获取SQLite文件在FastDFS中的文件id（数据库子服务连接SQLite前先取此id下载）
std::string FileBusiness::downloadSQLiteFile(const std::string& fileId) {
    auto dbResult = _fileInfoData->getFileInfoByFileIdFromDb(fileId);
    if (!dbResult.has_value()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_SQLITE_FILE_NOT_FOUND);
    }
    FileInfoEntity entity = dbResult.value();
    INF("Get SQLite file fdfsFileId: fileId={}, fdfsFileId={}", fileId, entity.fdfsFileId());
    return entity.fdfsFileId();
}

// 删除文件：FastDFS 删除 + worksheet 映射显式级联（方案A）+ 元数据删除
bool FileBusiness::deleteFile(const std::string& fileId, const std::string& userId) {
    // 1. 获取文件信息（含归属校验）
    auto fileInfo = getFileInfo(fileId, userId);
    // 2. 从FastDFS删除物理文件（失败仅告警——元数据仍会删除，物理残留可后台清理）
    if (!deleteFromFastDFS(fileInfo._fdfsFileId)) {
        WRN("Failed to delete file from FastDFS: fileId={}", fileId);
    }
    // 3. Excel 文件：删除对应的数据库表（数据库子服务的 DropTableExcel RPC）
    //    课件问题㉜桩：dbService.proto 尚未创建（数据库子服务章节），整段留档——
    //    恢复时按课件第37页实现：取映射表名 → DropTableExcel(db_connect_id="excel_default")
    //    if (fileInfo._fileExt == "xlsx") {
    //        auto dbChannel = _svcChannels->getNode(FLAGS_db_service);
    //        ... stub.DropTableExcel(&controller, &request, nullptr); ...
    //    }
    // 4. worksheet 映射显式级联删除（方案A，替代 FK ON DELETE CASCADE）——
    //    必须在元数据删除前执行（此时还能拿到映射；无FK不会自动级联）
    if (!_worksheetData->deleteWorksheetMappingsByFileIdFromDb(fileId)) {
        WRN("Failed to delete worksheet mappings: fileId={}", fileId);
    }
    _worksheetData->deleteWorksheetMappingsCache(fileId);
    // 5. 删除元数据
    if (!_fileInfoData->deleteFileInfoByFileIdFromDb(fileId)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_DELETE_FAILED);
    }
    _fileInfoData->deleteFileInfoCache(fileId);
    INF("File deleted successfully: fileId={}", fileId);
    return true;
}

// 仅删除元数据与缓存（SQLite 上传失败时的回滚路径，不动 FastDFS）
bool FileBusiness::deleteFileInfo(const std::string& fileId) {
    if (!_fileInfoData->deleteFileInfoByFileIdFromDb(fileId)) {
        WRN("Failed to delete file info from database: fileId={}", fileId);
        return false;
    }
    _fileInfoData->deleteFileInfoCache(fileId);
    INF("File info deleted successfully: fileId={}", fileId);
    return true;
}

// 预览Excel：worksheet映射 → 数据库子服务取分页数据（数据半截桩，㉜）
chat2Data::fileService::ExcelData FileBusiness::previewExcel(const std::string& fileId,
                                                             const std::string& userId,
                                                             int32_t pageNumber, int32_t pageSize) {
    // 1. 获取文件信息（含归属校验）
    auto fileInfo = getFileInfo(fileId, userId);
    // 2. 缓存优先获取worksheet映射
    auto cacheResult = _worksheetData->getWorksheetMappingsFromCache(fileId);
    std::vector<WorksheetEntity> worksheets;
    if (!cacheResult.has_value()) {
        worksheets = _worksheetData->getWorksheetMappingsByFileIdFromDb(fileId);
        if (!worksheets.empty()) {
            _worksheetData->saveWorksheetMappingsToCache(fileId, worksheets);
        }
    } else {
        worksheets = cacheResult.value();
    }
    if (worksheets.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::FILE_EXCEL_PARSE_FAILED);
    }
    // 3. 通过数据库子服务获取worksheet表数据（㉜桩：返回空ExcelData，等数据库子服务章节）
    return getExcelDataFromDB(fileId, worksheets, pageNumber, pageSize);
}

// ==================== 私有辅助 ====================

// 上传文件到FastDFS（buffer 方式）
std::optional<std::string> FileBusiness::uploadToFastDFS(const std::string& fileData,
                                                         const std::string& filename) {
    auto result = bitefdfs::FDFSClient::upload_from_buff(fileData);
    if (!result.has_value()) {
        ERR("Failed to upload file to FastDFS: filename={}", filename);
        return std::nullopt;
    }
    INF("File uploaded to FastDFS: filename={}, fileId={}", filename, result.value());
    return result.value();
}

// 从FastDFS下载文件到 buffer
std::optional<std::string> FileBusiness::downloadFromFastDFS(const std::string& fdfsFileId) {
    std::string fileData;
    if (!bitefdfs::FDFSClient::download_to_buff(fdfsFileId, fileData)) {
        ERR("Failed to download file from FastDFS: fileId={}", fdfsFileId);
        return std::nullopt;
    }
    INF("File downloaded from FastDFS: fileId={}, size={}", fdfsFileId, fileData.size());
    return fileData;
}

// 从FastDFS删除文件
bool FileBusiness::deleteFromFastDFS(const std::string& fdfsFileId) {
    if (!bitefdfs::FDFSClient::remove(fdfsFileId)) {
        WRN("Failed to delete file from FastDFS: fileId={}", fdfsFileId);
        return false;
    }
    INF("File deleted from FastDFS: fileId={}", fdfsFileId);
    return true;
}

// 通过Excel解析子服务获取worksheet表名列表
std::vector<std::string> FileBusiness::getWorksheetList(const std::string& fdfsFileId) {
    // 1. 获取Excel解析子服务的通信通道
    auto channel = _svcChannels->getNode(FLAGS_excel_service);
    if (!channel) {
        ERR("ExcelParserService not available");
        return {};
    }
    // 2. 构建请求
    chat2Data::excelParseService::GetWorksheetsRequest request;
    request.set_request_id(chat2Data::Utils::generateUuid());
    request.set_fdfs_file_id(fdfsFileId);
    // 3. 发起RPC调用
    brpc::Controller controller;
    chat2Data::excelParseService::ExcelParserService_Stub stub(channel.get());
    chat2Data::excelParseService::GetWorksheetsResponse response;
    stub.GetWorksheets(&controller, &request, &response, nullptr);
    // 4. 检查响应
    if (controller.Failed() || response.error_code() != 0) {
        ERR("Failed to get worksheets from ExcelParserService: error_code={}, error_msg={}",
            response.error_code(), response.error_msg());
        return {};
    }
    // 5. 提取worksheet列表
    std::vector<std::string> worksheets;
    worksheets.reserve(response.worksheets().size());
    for (const auto& worksheet : response.worksheets()) {
        worksheets.push_back(worksheet);
    }
    INF("Got {} worksheets from ExcelParserService", worksheets.size());
    return worksheets;
}

// 根据worksheet名称构造数据库表名：非法字符→'_'（保留字母数字下划线与UTF-8），尾部拼 fileId
// 课件问题㉝修复：fileId 是含连字符的 UUID，直接拼接会产生非法 SQL 表名——
// 对 fileId 同样做清洗（连字符→'_'），保证产物只含 [字母数字_中文]
std::string FileBusiness::calculateTableName(const std::string& worksheetName,
                                             const std::string& fileId) {
    std::string sanitizedName;
    for (char c : worksheetName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
            static_cast<unsigned char>(c) > 127) {
            sanitizedName += c;
        } else {
            sanitizedName += '_';
        }
    }
    std::string sanitizedFileId;
    for (char c : fileId) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
            static_cast<unsigned char>(c) > 127) {
            sanitizedFileId += c;
        } else {
            sanitizedFileId += '_';
        }
    }
    return sanitizedName + "_" + sanitizedFileId;
}

// 解析Excel数据并交数据库子服务入库（入库半截桩，㉜——解析半截已真实可用）
bool FileBusiness::importExcelData2DB(const std::string& fdfsFileId,
                                      const std::string& fileId,
                                      const std::vector<std::string>& worksheets) {
    // 1. 通过Excel解析子服务解析Excel文件数据
    // 1.1 获取通信通道
    auto excelChannel = _svcChannels->getNode(FLAGS_excel_service);
    if (!excelChannel) {
        ERR("ExcelParserService not available when importing Excel data to database");
        return false;
    }
    // 1.2 构建解析请求
    chat2Data::excelParseService::ParseExcelRequest parseRequest;
    parseRequest.set_request_id(chat2Data::Utils::generateUuid());
    parseRequest.set_fdfs_file_id(fdfsFileId);
    for (const auto& worksheet : worksheets) {
        parseRequest.add_worksheets(worksheet);
    }
    // 1.3 发起解析请求
    brpc::Controller parseController;
    chat2Data::excelParseService::ExcelParserService_Stub excelStub(excelChannel.get());
    chat2Data::excelParseService::ParseExcelResponse parseResponse;
    excelStub.ParseExcel(&parseController, &parseRequest, &parseResponse, nullptr);
    // 1.4 检查解析结果
    if (parseController.Failed() || parseResponse.error_code() != 0) {
        ERR("Failed to parse Excel from ExcelParserService: error_code={}, error_msg={}",
            parseResponse.error_code(), parseResponse.error_msg());
        return false;
    }
    // 2. 导入解析后的数据到数据库
    // 课件问题㉜桩：等数据库子服务章节实现（届时将 WorksheetData(列信息) 与
    // RowData(行数据) 按映射表名逐表写入）
    INF("Excel data parsed (DB import pending database sub-service): fileId={}, worksheets={}",
        fileId, worksheets.size());
    return true;
}

// 通过数据库子服务获取Excel表数据（㉜桩：空实现，等数据库子服务章节）
chat2Data::fileService::ExcelData FileBusiness::getExcelDataFromDB(
        const std::string& fileId, const std::vector<WorksheetEntity>& worksheets,
        int32_t pageNumber, int32_t pageSize) {
    chat2Data::fileService::ExcelData excelData;
    // TODO(数据库子服务章节)：按 worksheet→表名映射，逐表分页查询数据填充 Sheet
    INF("getExcelDataFromDB stub: fileId={}, worksheets={}, page={}/{}",
        fileId, worksheets.size(), pageNumber, pageSize);
    return excelData;
}

} // namespace fileService
