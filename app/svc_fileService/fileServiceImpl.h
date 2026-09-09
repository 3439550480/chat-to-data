#pragma once
#include <string>
#include <memory>
#include <bite_scaffold/rpc.h>
#include "../proto/protoCode/fileService.pb.h"

namespace fileService {
class FileBusiness;

class FileServiceImpl : public chat2Data::fileService::FileService {
public:
    explicit FileServiceImpl(std::shared_ptr<FileBusiness> fileBusiness);
    ~FileServiceImpl() override;
    // 上传文件信息
    void UploadFileInfo(google::protobuf::RpcController* controller,
                        const chat2Data::fileService::UploadFileInfoRequest* request,
                        chat2Data::fileService::UploadFileInfoResponse* response,
                        google::protobuf::Closure* done) override;
    // 获取文件信息
    void GetFileInfo(google::protobuf::RpcController* controller,
                     const chat2Data::fileService::GetFileInfoRequest* request,
                     chat2Data::fileService::GetFileInfoResponse* response,
                     google::protobuf::Closure* done) override;
    // 上传文件数据（数据在 attachment 中）
    void UploadFile(google::protobuf::RpcController* controller,
                    const chat2Data::fileService::UploadFileRequest* request,
                    chat2Data::fileService::UploadFileResponse* response,
                    google::protobuf::Closure* done) override;
    // 下载文件数据（数据通过 attachment 回传）
    void DownloadFile(google::protobuf::RpcController* controller,
                      const chat2Data::fileService::DownloadFileRequest* request,
                      chat2Data::fileService::DownloadFileResponse* response,
                      google::protobuf::Closure* done) override;
    // 删除文件
    void DeleteFile(google::protobuf::RpcController* controller,
                    const chat2Data::fileService::DeleteFileRequest* request,
                    chat2Data::fileService::DeleteFileResponse* response,
                    google::protobuf::Closure* done) override;
    // 预览Excel文件
    void PreviewExcel(google::protobuf::RpcController* controller,
                      const chat2Data::fileService::PreviewExcelRequest* request,
                      chat2Data::fileService::PreviewExcelResponse* response,
                      google::protobuf::Closure* done) override;
    // 获取文件列表
    void GetFileList(google::protobuf::RpcController* controller,
                     const chat2Data::fileService::GetFileListRequest* request,
                     chat2Data::fileService::GetFileListResponse* response,
                     google::protobuf::Closure* done) override;
    // 处理文件会话映射
    void HandleFileChatSessionMap(google::protobuf::RpcController* controller,
                                  const chat2Data::fileService::HandleFileChatSessionMapRequest* request,
                                  chat2Data::fileService::HandleFileChatSessionMapResponse* response,
                                  google::protobuf::Closure* done) override;
    // 上传SQLite文件（数据在 attachment 中）
    void UploadSQLiteFile(google::protobuf::RpcController* controller,
                          const chat2Data::fileService::UploadSQLiteFileRequest* request,
                          chat2Data::fileService::UploadSQLiteFileResponse* response,
                          google::protobuf::Closure* done) override;
    // 获取SQLite文件信息（返回 FastDFS 文件id）
    void GetSQLiteFile(google::protobuf::RpcController* controller,
                       const chat2Data::fileService::GetSQLiteFileRequest* request,
                       chat2Data::fileService::GetSQLiteFileResponse* response,
                       google::protobuf::Closure* done) override;
    // 获取Worksheet数据库表名列表
    void GetWorksheetDBTables(google::protobuf::RpcController* controller,
                              const chat2Data::fileService::GetWorksheetDBTablesRequest* request,
                              chat2Data::fileService::GetWorksheetDBTablesResponse* response,
                              google::protobuf::Closure* done) override;
private:
    std::shared_ptr<FileBusiness> _fileBusiness;
};

} // namespace fileService
