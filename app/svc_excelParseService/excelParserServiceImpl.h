#pragma once
#include <string>
#include <memory>
#include <google/protobuf/service.h>
#include "../proto/protoCode/excelParseService.pb.h"
#include "excelParserBusiness.h"

namespace excelParserService {

// Excel解析服务 RPC 接口实现：继承 proto 生成的 ExcelParserService 虚基类
class ExcelParserServiceImpl : public chat2Data::excelParseService::ExcelParserService {
public:
    explicit ExcelParserServiceImpl(std::shared_ptr<ExcelParserBusiness> business);
    ~ExcelParserServiceImpl() override;
    // 获取excel文件的worksheet表名列表
    void GetWorksheets(google::protobuf::RpcController* controller,
                       const chat2Data::excelParseService::GetWorksheetsRequest* request,
                       chat2Data::excelParseService::GetWorksheetsResponse* response,
                       google::protobuf::Closure* done) override;
    // 解析excel文件各个worksheet表数据
    void ParseExcel(google::protobuf::RpcController* controller,
                    const chat2Data::excelParseService::ParseExcelRequest* request,
                    chat2Data::excelParseService::ParseExcelResponse* response,
                    google::protobuf::Closure* done) override;
private:
    std::shared_ptr<ExcelParserBusiness> _business;    // 业务层实例指针
};

} // namespace excelParserService
