#pragma once
#include <string>
#include <vector>
#include <memory>
#include <OpenXLSX.hpp>
// 课件问题㉕修订：课件最终版在这里 include 了 pb.h——解析器层是纯业务结构，
// 与 proto 无关（转换在业务层的 convertToProtoWorksheetData），故去掉，保持分层干净

namespace excelParserService {

// 单元格结构
struct CellData {
    std::string value;           // 单元格数值（统一转字符串）
    std::string type;            // 单元格类型：Empty, Integer, Float, Boolean, String
};
// 列信息结构（列名 + 推断的列类型）
struct ColumnData {
    std::string name;            // 列名（已做合法性处理）
    std::string type;            // 列类型：TEXT, BIGINT, DOUBLE, BOOLEAN, DATE
};
// worksheet 的行数据结构
struct RowData {
    std::vector<CellData> cells; // 一行数据，包含多个单元格
};
// worksheet 信息结构
struct WorksheetInfo {
    std::string name;                    // worksheet名称
    std::vector<ColumnData> columns;     // 列信息（表头+推断类型）
    std::vector<RowData> rows;           // 数据行（不含表头）
    int totalRows = 0;                   // 总行数（含表头行）
    int totalCols = 0;                   // 总列数
};

// Excel解析器：负责解析Excel文件（OpenXLSX封装）
class ExcelParser {
public:
    ExcelParser();
    ~ExcelParser();
    // 获取excel文件中所有worksheet名称
    std::vector<std::string> getWorksheetNames(const std::string& filePath);
    // 解析指定worksheet数据（表头+列类型+数据行；失败返回nullptr）
    std::unique_ptr<WorksheetInfo> parseWorksheet(const std::string& filePath,
                                                  const std::string& worksheetName);

    // ---- 纯函数辅助组：无状态、无副作用，公开以便单元测试与将来复用 ----
    // 验证列名合法性（空/数字开头/非法字符 → 规范化）
    std::string validateColumnName(const std::string& name, int columnIndex);
    // 移除字符串首尾空白字符
    std::string trimWhitespace(const std::string& str);
    // 检测字符串是否为数值（含正负号、小数点、科学计数法）
    bool isNumericString(const std::string& str);
    // 检测字符串是否为布尔值（true/1/t/yes/y 及其否定）
    bool isBooleanString(const std::string& str);
    // 检测字符串是否为日期类型（8种常见格式正则）
    bool isDateString(const std::string& str);
    // 根据采样单元格推断该列的实际类型（投票制）
    std::string inferColumnType(const std::vector<CellData>& cellValues);

private:
    // 解析指定单元格数值（按 OpenXLSX 类型分发转字符串）
    CellData parseCellValue(const OpenXLSX::XLCellValue& value);
};

} // namespace excelParserService
