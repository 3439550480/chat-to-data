#pragma once
#include <memory>
#include <bite_scaffold/rpc.h>
#include "../proto/protoCode/dbService.pb.h"

namespace databaseService {

class DBBusiness;

// 数据库服务 RPC 接口实现：继承 proto 生成的 DatabaseService 虚基类
class DBServiceImpl : public chat2Data::DatabaseService::DatabaseService {
public:
    explicit DBServiceImpl(std::shared_ptr<DBBusiness> dbBusiness);
    ~DBServiceImpl() override;
    // 连接数据库
    void ConnectDatabase(::google::protobuf::RpcController* controller,
                         const chat2Data::DatabaseService::ConnectDatabaseRequest* request,
                         chat2Data::DatabaseService::ConnectDatabaseResponse* response,
                         ::google::protobuf::Closure* done) override;
    // 断开数据库连接
    void DisconnectDatabase(::google::protobuf::RpcController* controller,
                            const chat2Data::DatabaseService::DisconnectDatabaseRequest* request,
                            chat2Data::DatabaseService::DisconnectDatabaseResponse* response,
                            ::google::protobuf::Closure* done) override;
    // 列出数据库中的所有表
    void ListTables(::google::protobuf::RpcController* controller,
                    const chat2Data::DatabaseService::ListTablesRequest* request,
                    chat2Data::DatabaseService::ListTablesResponse* response,
                    ::google::protobuf::Closure* done) override;
    // 获取表中的数据
    void GetTableData(::google::protobuf::RpcController* controller,
                      const chat2Data::DatabaseService::GetTableDataRequest* request,
                      chat2Data::DatabaseService::GetTableDataResponse* response,
                      ::google::protobuf::Closure* done) override;
    // 执行 SQL 语句
    void ExecuteSQL(::google::protobuf::RpcController* controller,
                    const chat2Data::DatabaseService::ExecuteSQLRequest* request,
                    chat2Data::DatabaseService::ExecuteSQLResponse* response,
                    ::google::protobuf::Closure* done) override;
    // 获取数据库连接中的临时表
    void GetConnTempTables(::google::protobuf::RpcController* controller,
                           const chat2Data::DatabaseService::GetConnTempTablesRequest* request,
                           chat2Data::DatabaseService::GetConnTempTablesResponse* response,
                           ::google::protobuf::Closure* done) override;
    // 获取表的结构
    void GetTableStruct(::google::protobuf::RpcController* controller,
                        const chat2Data::DatabaseService::GetTableStructRequest* request,
                        chat2Data::DatabaseService::GetTableStructResponse* response,
                        ::google::protobuf::Closure* done) override;
    // 获取表的采样数据
    void GetSampleData(::google::protobuf::RpcController* controller,
                       const chat2Data::DatabaseService::GetSampleDataRequest* request,
                       chat2Data::DatabaseService::GetSampleDataResponse* response,
                       ::google::protobuf::Closure* done) override;
    // 导入 Excel 数据到数据库
    void ImportExcelData(::google::protobuf::RpcController* controller,
                         const chat2Data::DatabaseService::ImportExcelDataRequest* request,
                         chat2Data::DatabaseService::ImportExcelDataResponse* response,
                         ::google::protobuf::Closure* done) override;
    // 删除 Excel 文件对应的数据库表
    void DropTableExcel(::google::protobuf::RpcController* controller,
                        const chat2Data::DatabaseService::DropTableExcelRequest* request,
                        chat2Data::DatabaseService::DropTableExcelResponse* response,
                        ::google::protobuf::Closure* done) override;
    // 删除用户的所有数据库连接
    void DeleteUserAllConn(::google::protobuf::RpcController* controller,
                           const chat2Data::DatabaseService::DeleteUserAllConnRequest* request,
                           chat2Data::DatabaseService::DeleteUserAllConnResponse* response,
                           ::google::protobuf::Closure* done) override;
private:
    std::shared_ptr<DBBusiness> _dbBusiness;   // 业务层实例指针
};

} // namespace databaseService
