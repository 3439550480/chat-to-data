#pragma once
#include <memory>
#include <string>
#include <vector>
#include "excelParser.h"
#include "../proto/protoCode/excelParseService.pb.h"

namespace excelParserService {

// Excel解析业务层：FastDFS 临时下载 → ExcelParser 解析 → 转 proto 结构
class ExcelParserBusiness {
public:
    ExcelParserBusiness();
    ~ExcelParserBusiness();
    // 获取Excel文件中的所有工作表名称（通过FastDFS文件ID）
    // @throws EXCEL_FILE_PATH_INVALID / EXCEL_FILE_OPEN_FAILED
    std::vector<std::string> getWorksheets(const std::string& fdfsFileId);
    // 解析Excel文件中的指定工作表数据（通过FastDFS文件ID）
    // @throws EXCEL_FILE_PATH_INVALID / EXCEL_DATA_INVALID / EXCEL_FILE_OPEN_FAILED / EXCEL_PARSE_FAILED
    std::vector<chat2Data::excelParseService::WorksheetData> parseExcel(
        const std::string& fdfsFileId,
        const std::vector<std::string>& worksheets);
private:
    // 将 WorksheetInfo 转换为 Proto 格式的 WorksheetData
    chat2Data::excelParseService::WorksheetData convertToProtoWorksheetData(
        const std::unique_ptr<WorksheetInfo>& worksheetInfo);
private:
    std::unique_ptr<ExcelParser> _excelParser;    // excel解析器实例
};

} // namespace excelParserService
