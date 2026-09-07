#include "excelParserServiceImpl.h"
#include <brpc/controller.h>
#include <bite_scaffold/log.h>
#include "../common/errorHandler.h"

namespace excelParserService {

ExcelParserServiceImpl::ExcelParserServiceImpl(std::shared_ptr<ExcelParserBusiness> business)
    : _business(business) {
    INF("ExcelParserServiceImpl initialized");
}

ExcelParserServiceImpl::~ExcelParserServiceImpl() {
    INF("ExcelParserServiceImpl destroyed");
}

void ExcelParserServiceImpl::GetWorksheets(google::protobuf::RpcController* controller,
                    const chat2Data::excelParseService::GetWorksheetsRequest* request,
                    chat2Data::excelParseService::GetWorksheetsResponse* response,
                    google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()（无论 return 还是抛异常都会应答）
    brpc::ClosureGuard done_guard(done);
    // 2. 解析 rpc 请求参数
    std::string requestId = request->request_id();
    std::string fdfsFileId = request->fdfs_file_id();
    try {
        // 3. 校验参数
        if (fdfsFileId.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_PATH_INVALID);
        }
        // 4. 调用业务层（同步：下载+解析在本次调用内完成）
        auto worksheets = _business->getWorksheets(fdfsFileId);
        // 5. 构造 rpc 响应
        for (const auto& worksheet : worksheets) {
            response->add_worksheets(worksheet);
        }
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("GetWorksheets success: requestId={}, fdfsFileId={}, worksheetCount={}",
            requestId, fdfsFileId, worksheets.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
        ERR("GetWorksheets failed: {}", e.what());
    }
}

void ExcelParserServiceImpl::ParseExcel(google::protobuf::RpcController* controller,
                    const chat2Data::excelParseService::ParseExcelRequest* request,
                    chat2Data::excelParseService::ParseExcelResponse* response,
                    google::protobuf::Closure* done) {
    // 1. ClosureGuard 以 RAII 机制管理 done->Run()
    brpc::ClosureGuard done_guard(done);
    // 2. 解析 rpc 请求参数（repeated string → vector）
    std::string requestId = request->request_id();
    std::string fdfsFileId = request->fdfs_file_id();
    std::vector<std::string> worksheets(request->worksheets().begin(),
                                        request->worksheets().end());
    try {
        // 3. 校验参数
        if (fdfsFileId.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_PATH_INVALID);
        }
        if (worksheets.empty()) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_DATA_INVALID);
        }
        // 4. 调用业务层解析各工作表数据
        auto worksheetDataList = _business->parseExcel(fdfsFileId, worksheets);
        // 5. 构造 rpc 响应（嵌套结构用 CopyFrom 填充）
        for (auto& worksheetData : worksheetDataList) {
            response->add_worksheets()->CopyFrom(worksheetData);
        }
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(chat2Data::ErrorCode::SUCCESS));
        INF("ParseExcel success: requestId={}, fdfsFileId={}, worksheetCount={}",
            requestId, fdfsFileId, worksheetDataList.size());
    } catch (const chat2Data::Chat2DataException& e) {
        response->set_request_id(requestId);
        response->set_error_code(static_cast<int32_t>(e.getErrorCode()));
        response->set_error_msg(chat2Data::error2String(e.getErrorCode()));
        ERR("ParseExcel failed: {}", e.what());
    }
}

} // namespace excelParserService
