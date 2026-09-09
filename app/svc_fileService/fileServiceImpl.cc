#include <exception>
#include <bite_scaffold/log.h>
#include <bite_scaffold/rpc.h>
#include "../common/errorHandler.h"
#include "../common/utils.h"
#include "fileServiceImpl.h"
#include "fileBusiness.h"
#include "common.h"

namespace fileService {

FileServiceImpl::FileServiceImpl(std::shared_ptr<FileBusiness> fileBusiness)
    : _fileBusiness(fileBusiness) {
}

FileServiceImpl::~FileServiceImpl() {}

// 上传文件信息（Excel 两段式上传的第一段：fileId 服务端生成并返回给前端）
void FileServiceImpl::UploadFileInfo(google::protobuf::RpcController* controller,
                                     const chat2Data::fileService::UploadFileInfoRequest* request,
                                     chat2Data::fileService::UploadFileInfoResponse* response,
                                     google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()
    brpc::ClosureGuard done_guard(done);
    // 2. 解析 rpc 请求参数
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string userId = request->user_id();
    const auto& fileInfoProto = request->file_info();
    // 3. 校验参数
    if (sessionId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 4. 构建文件信息（fileId 服务端生成；上传时间取当前时间）
        FileInfo fileInfo;
        fileInfo._fileId = chat2Data::Utils::generateUuid();
        fileInfo._fileName = fileInfoProto.filename();
        fileInfo._fileSize = fileInfoProto.file_size();
        fileInfo._fileExt = fileInfoProto.file_ext();
        fileInfo._userId = userId;
        fileInfo._uploadTime = std::time(nullptr);
        // 5. 保存文件信息到数据库
        std::string fileId = _fileBusiness->saveFileInfo(fileInfo);
        // 6. 返回 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_file_id(fileId);
        INF("UploadFileInfo success: requestId={}, fileId={}", requestId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取文件信息
void FileServiceImpl::GetFileInfo(google::protobuf::RpcController* controller,
                                  const chat2Data::fileService::GetFileInfoRequest* request,
                                  chat2Data::fileService::GetFileInfoResponse* response,
                                  google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || fileId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 获取文件信息（缓存优先，含归属校验）
        FileInfo fileInfo = _fileBusiness->getFileInfo(fileId, userId);
        // 构建 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        chat2Data::fileService::FileDetail* result = response->mutable_result();
        result->set_file_id(fileInfo._fileId);
        result->set_file_name(fileInfo._fileName);
        result->set_file_size(fileInfo._fileSize);
        result->set_upload_time(fileInfo._uploadTime);
        result->set_file_ext(fileInfo._fileExt);
        INF("GetFileInfo success: requestId={}, fileId={}", requestId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 上传文件数据（Excel 两段式第二段：数据在 attachment 中，不走序列化）
void FileServiceImpl::UploadFile(google::protobuf::RpcController* controller,
                                 const chat2Data::fileService::UploadFileRequest* request,
                                 chat2Data::fileService::UploadFileResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || fileId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 1. 获取文件元数据（UploadFileInfo 阶段已落库）
        FileInfo fileInfo = _fileBusiness->getFileInfo(fileId, userId);
        // 2. 从 attachment 取文件数据（课件冗余的 append(空IOBuf) 已删）
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        std::string fileData = cntl->request_attachment().to_string();
        // 3. 上传到 FastDFS（内部含：解析Excel → worksheet映射落库 → 入库桩）
        _fileBusiness->uploadExcelFile(sessionId, fileId, userId,
                                       fileInfo._fileName, fileData);
        // 4. 返回 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("UploadFile success: requestId={}, fileId={}, size={}",
            requestId, fileId, fileData.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 下载文件（数据通过 response attachment 回传，不走序列化）
void FileServiceImpl::DownloadFile(google::protobuf::RpcController* controller,
                                   const chat2Data::fileService::DownloadFileRequest* request,
                                   chat2Data::fileService::DownloadFileResponse* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || fileId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 1. 从 FastDFS 下载文件数据（业务层含归属校验）
        std::string fileData = _fileBusiness->downloadExcelFile(fileId, userId);
        // 2. 文件数据通过 response attachment 回传
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        cntl->response_attachment().append(fileData);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("DownloadFile success: requestId={}, fileId={}, size={}",
            requestId, fileId, fileData.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 删除文件
void FileServiceImpl::DeleteFile(google::protobuf::RpcController* controller,
                                 const chat2Data::fileService::DeleteFileRequest* request,
                                 chat2Data::fileService::DeleteFileResponse* response,
                                 google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || fileId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 删除：FastDFS 物理文件 + worksheet 映射（方案A显式级联）+ 元数据
        _fileBusiness->deleteFile(fileId, userId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("DeleteFile success: requestId={}, fileId={}", requestId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 预览Excel文件
void FileServiceImpl::PreviewExcel(google::protobuf::RpcController* controller,
                                   const chat2Data::fileService::PreviewExcelRequest* request,
                                   chat2Data::fileService::PreviewExcelResponse* response,
                                   google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string userId = request->user_id();
    int32_t pageNumber = request->page_number();
    int32_t pageSize = request->page_size();
    if (sessionId.empty() || fileId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    // 分页参数防御：非法值归位默认（页码1，每页50）
    if (pageNumber <= 0) pageNumber = 1;
    if (pageSize <= 0) pageSize = 50;
    try {
        // 查询 excel 数据（数据半截桩：当前返回空 ExcelData）
        chat2Data::fileService::ExcelData excelData =
            _fileBusiness->previewExcel(fileId, userId, pageNumber, pageSize);
        // 构建 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        chat2Data::fileService::PreviewExcelResult* result = response->mutable_result();
        result->set_file_id(fileId);
        *result->mutable_excel_data() = excelData;
        INF("PreviewExcel success: requestId={}, fileId={}", requestId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取文件列表
void FileServiceImpl::GetFileList(google::protobuf::RpcController* controller,
                                  const chat2Data::fileService::GetFileListRequest* request,
                                  chat2Data::fileService::GetFileListResponse* response,
                                  google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 查询文件列表
        std::vector<FileInfo> fileList = _fileBusiness->getFileList(userId);
        // 构建 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        for (const auto& fileInfo : fileList) {
            chat2Data::fileService::FileListItem* item =
                response->mutable_result()->add_file_list();
            item->set_file_id(fileInfo._fileId);
            item->set_file_name(fileInfo._fileName);
            item->set_file_size(fileInfo._fileSize);
            item->set_upload_time(fileInfo._uploadTime);
            item->set_chat_session_id(fileInfo._chatSessionId);
        }
        INF("GetFileList success: requestId={}, userId={}, count={}",
            requestId, userId, fileList.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 关联文件和聊天会话
void FileServiceImpl::HandleFileChatSessionMap(google::protobuf::RpcController* controller,
                                               const chat2Data::fileService::HandleFileChatSessionMapRequest* request,
                                               chat2Data::fileService::HandleFileChatSessionMapResponse* response,
                                               google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    std::string chatSessionId = request->chat_session_id();
    std::string userId = request->user_id();
    if (sessionId.empty() || fileId.empty() || chatSessionId.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        _fileBusiness->associateFileChatSession(fileId, chatSessionId, userId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("HandleFileChatSessionMap success: requestId={}, fileId={}, chatSessionId={}",
            requestId, fileId, chatSessionId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 上传SQLite文件（单接口：元数据+数据一步完成，fileId 服务端生成）
void FileServiceImpl::UploadSQLiteFile(google::protobuf::RpcController* controller,
                                       const chat2Data::fileService::UploadSQLiteFileRequest* request,
                                       chat2Data::fileService::UploadSQLiteFileResponse* response,
                                       google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = chat2Data::Utils::generateUuid();   // SQLite：服务端生成 fileId
    std::string filename = request->filename();
    std::string userId = request->user_id();
    if (sessionId.empty() || filename.empty() || userId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        // 1. 从 attachment 取文件数据
        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        std::string fileData = cntl->request_attachment().to_string();
        // 2. 构建元数据（会话关联由前端在 SQLite 连接成功后主动发起）
        FileInfo fileInfo;
        fileInfo._fileId = fileId;
        fileInfo._fileName = filename;
        fileInfo._fileSize = fileData.size();
        fileInfo._fileExt = ".db";
        fileInfo._userId = userId;
        fileInfo._chatSessionId = "";
        fileInfo._uploadTime = std::time(nullptr);
        // 3. 两步落库：先元数据后数据
        _fileBusiness->saveFileInfo(fileInfo);
        _fileBusiness->uploadSQLiteFile(sessionId, fileId, userId, filename, fileData);
        // 4. 返回 rpc 响应
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_file_id(fileId);
        INF("UploadSQLiteFile success: requestId={}, fileId={}", requestId, fileId);
    } catch (const chat2Data::Chat2DataException& e) {
        // 5. 失败回滚：删除已保存的元数据和缓存（数据未入库则无需动 FastDFS）
        _fileBusiness->deleteFileInfo(fileId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取SQLite文件信息（返回 FastDFS 文件id，数据库子服务使用）
void FileServiceImpl::GetSQLiteFile(google::protobuf::RpcController* controller,
                                    const chat2Data::fileService::GetSQLiteFileRequest* request,
                                    chat2Data::fileService::GetSQLiteFileResponse* response,
                                    google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    if (sessionId.empty() || fileId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        std::string fdfsFileId = _fileBusiness->downloadSQLiteFile(fileId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        response->mutable_result()->set_fdfsfileid(fdfsFileId);
        INF("GetSQLiteFile success: requestId={}, fileId={}, fdfsFileId={}",
            requestId, fileId, fdfsFileId);
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

// 获取Worksheet数据库表名列表（AI子服务使用）
void FileServiceImpl::GetWorksheetDBTables(google::protobuf::RpcController* controller,
                                           const chat2Data::fileService::GetWorksheetDBTablesRequest* request,
                                           chat2Data::fileService::GetWorksheetDBTablesResponse* response,
                                           google::protobuf::Closure* done) {
    brpc::ClosureGuard done_guard(done);
    std::string requestId = request->request_id();
    std::string sessionId = request->session_id();
    std::string fileId = request->file_id();
    if (sessionId.empty() || fileId.empty()) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        response->set_error_msg(chat2Data::error2String(chat2Data::ErrorCode::FILE_PARAM_INVALID));
        return;
    }
    try {
        std::vector<std::string> worksheetTables = _fileBusiness->getWorksheetDBTables(fileId);
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        for (const auto& tableName : worksheetTables) {
            response->mutable_result()->add_worksheet_dbtables(tableName);
        }
        INF("GetWorksheetDBTables success: requestId={}, fileId={}, count={}",
            requestId, fileId, worksheetTables.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
    }
}

} // namespace fileService
