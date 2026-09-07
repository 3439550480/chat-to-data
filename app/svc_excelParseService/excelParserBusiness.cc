#include <cstdio>
#include <bite_scaffold/log.h>
// 课件问题㉚：fastcommon/common_define.h 把 byte #define 成 signed char，
// 会炸掉 protobuf parse_context.h 的 uint32 byte 局部变量——
// 因此必须先 include 间接拉入 protobuf 的本服务头文件，再 include fdfs（同 brpc/hiredis 宏冲突纪律）
#include "excelParserBusiness.h"
#include <bite_scaffold/fdfs.h>
#ifdef byte
#undef byte   // 双保险：即使将来有人打乱顺序也不炸（本文件不使用 byte 标识符）
#endif
#include "../common/utils.h"
#include "../common/errorHandler.h"

namespace excelParserService {

// RAII 临时文件清理器：任何路径退出（含异常）都删除临时文件（课件部分 throw 路径漏删）
namespace {
struct TempFileGuard {
    std::string path;
    ~TempFileGuard() {
        if (!path.empty()) {
            std::remove(path.c_str());
        }
    }
};
} // namespace

ExcelParserBusiness::ExcelParserBusiness()
    : _excelParser(std::make_unique<ExcelParser>()) {
    INF("ExcelParserBusiness initialized");
}

ExcelParserBusiness::~ExcelParserBusiness() {
    INF("ExcelParserBusiness destroyed");
}

std::vector<std::string> ExcelParserBusiness::getWorksheets(const std::string& fdfsFileId) {
    // 1. 检查FastDFS文件ID是否为空
    if (fdfsFileId.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_PATH_INVALID);
    }
    // 2. 生成临时文件路径（课件问题㉘修复：秒级时间戳在并发同秒请求下会撞名，
    //    改用 UUID 保证唯一）
    std::string tempFilePath = "/tmp/excel_temp_" + chat2Data::Utils::generateUuid() + ".xlsx";
    TempFileGuard guard{tempFilePath};
    // 3. 从FastDFS下载文件到本地临时文件
    if (!bitefdfs::FDFSClient::download_to_file(fdfsFileId, tempFilePath)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_OPEN_FAILED);
    }
    // 4. 通过文件路径调用ExcelParser获取所有工作表名称
    auto worksheets = _excelParser->getWorksheetNames(tempFilePath);
    if (worksheets.empty()) {
        // 打开成功但解析不出任何工作表：按课件语义归为文件打开失败
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_OPEN_FAILED);
    }
    // 5. 返回所有工作表名称（临时文件由 guard 删除）
    INF("ExcelParserBusiness::getWorksheets found {} worksheets", worksheets.size());
    return worksheets;
}

std::vector<chat2Data::excelParseService::WorksheetData> ExcelParserBusiness::parseExcel(
    const std::string& fdfsFileId,
    const std::vector<std::string>& worksheets) {
    // 1. 检查FastDFS文件ID是否为空
    if (fdfsFileId.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_PATH_INVALID);
    }
    // 2. 检查工作表名称是否为空
    if (worksheets.empty()) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_DATA_INVALID);
    }
    // 3. 生成临时文件路径（㉘：UUID 命名）
    std::string tempFilePath = "/tmp/excel_temp_" + chat2Data::Utils::generateUuid() + ".xlsx";
    TempFileGuard guard{tempFilePath};
    // 4. 从FastDFS下载文件到本地临时文件
    if (!bitefdfs::FDFSClient::download_to_file(fdfsFileId, tempFilePath)) {
        throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_FILE_OPEN_FAILED);
    }
    // 5. 逐个解析请求的工作表数据
    std::vector<chat2Data::excelParseService::WorksheetData> result;
    result.reserve(worksheets.size());
    for (const auto& worksheetName : worksheets) {
        auto worksheetInfo = _excelParser->parseWorksheet(tempFilePath, worksheetName);
        if (!worksheetInfo) {
            throw chat2Data::Chat2DataException(chat2Data::ErrorCode::EXCEL_PARSE_FAILED);
        }
        // 转换为 Proto 格式（临时文件由 guard 删除，异常路径同样安全）
        result.push_back(convertToProtoWorksheetData(worksheetInfo));
    }
    INF("ExcelParserBusiness::parseExcel parsed {} worksheets", result.size());
    return result;
}

chat2Data::excelParseService::WorksheetData
ExcelParserBusiness::convertToProtoWorksheetData(
    const std::unique_ptr<WorksheetInfo>& worksheetInfo) {
    chat2Data::excelParseService::WorksheetData protoData;
    // 1. 设置worksheet名称以及总行数和总列数
    protoData.set_name(worksheetInfo->name);
    protoData.set_total_rows(worksheetInfo->totalRows);
    protoData.set_total_cols(worksheetInfo->totalCols);
    // 2. 设置worksheet列信息（列名+推断类型，供数据库子服务建表）
    for (const auto& column : worksheetInfo->columns) {
        auto* protoColumn = protoData.add_columns();
        protoColumn->set_name(column.name);
        protoColumn->set_type(column.type);
    }
    // 3. 设置worksheet行数据
    for (const auto& row : worksheetInfo->rows) {
        auto* protoRow = protoData.add_rows();
        for (const auto& cell : row.cells) {
            auto* protoCell = protoRow->add_cells();
            protoCell->set_value(cell.value);
            protoCell->set_type(cell.type);
        }
    }
    return protoData;
}

} // namespace excelParserService
