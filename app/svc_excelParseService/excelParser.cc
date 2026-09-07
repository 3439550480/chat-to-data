#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <bite_scaffold/log.h>
#include "excelParser.h"

namespace excelParserService {

ExcelParser::ExcelParser() {
}

ExcelParser::~ExcelParser() {
}

// 核心责任：安全地获取工作表元数据。
std::vector<std::string> ExcelParser::getWorksheetNames(const std::string& filePath) {
    std::vector<std::string> names;
    try {
        // 1. 打开Excel文件
        OpenXLSX::XLDocument doc;
        doc.open(filePath);
        if (!doc.isOpen()) {
            ERR("Failed to open Excel file: {}", filePath);
            return names;
        }
        // 2. 获取工作簿对象
        auto workbook = doc.workbook();
        // 3. 获取工作表名称列表
        names = workbook.worksheetNames();
        INF("Successfully retrieved {} worksheet names from file: {}", names.size(), filePath);
        // 4. 关闭文件
        doc.close();
    } catch (const OpenXLSX::XLException& e) {
        ERR("OpenXLSX exception while getting worksheet names: {}", e.what());
    } catch (const std::exception& e) {
        ERR("Exception while getting worksheet names: {}", e.what());
    }
    return names;
}

// 核心责任：OpenXLSX 原生类型 → 统一 CellData 结构的适配器。
CellData ExcelParser::parseCellValue(const OpenXLSX::XLCellValue& value) {
    CellData cell;
    // 1. 空单元格
    if (value.type() == OpenXLSX::XLValueType::Empty) {
        cell.type = "Empty";
        cell.value = "";
        return cell;
    }
    // 2. 布尔值（转 "1"/"0"，与数据库 BOOLEAN 存储对齐）
    if (value.type() == OpenXLSX::XLValueType::Boolean) {
        bool boolValue = value.get<bool>();
        cell.type = "Boolean";
        cell.value = boolValue ? "1" : "0";
        return cell;
    }
    // 3. 整数
    if (value.type() == OpenXLSX::XLValueType::Integer) {
        int64_t intValue = value.get<int64_t>();
        cell.type = "Integer";
        cell.value = std::to_string(intValue);
        return cell;
    }
    // 4. 浮点数
    if (value.type() == OpenXLSX::XLValueType::Float) {
        double floatValue = value.get<double>();
        cell.type = "Float";
        cell.value = std::to_string(floatValue);
        return cell;
    }
    // 5. 字符串（异常兜底：get<string> 理论上不会再抛，防御一下）
    try {
        cell.type = "String";
        cell.value = value.get<std::string>();
    } catch (...) {
        cell.type = "String";
        cell.value = "";
    }
    return cell;
}

// ==================== 列名与类型推断辅助 ====================

// SQL 保留字集合（大写比对）：直接用作列名会导致建表失败
static const std::unordered_set<std::string> kSqlReservedWords = {
    "SELECT", "INSERT", "UPDATE", "DELETE", "WHERE", "FROM", "TABLE", "CREATE",
    "DROP", "ALTER", "INDEX", "KEY", "PRIMARY", "UNIQUE", "FOREIGN", "ORDER",
    "GROUP", "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "UNION", "VALUES",
    "SET", "DEFAULT", "LIMIT", "HAVING", "CASE", "WHEN", "THEN", "ELSE",
    "END", "EXISTS", "BETWEEN", "LIKE", "IN", "ON", "AS", "AND", "OR",
    "NOT", "NULL", "DISTINCT", "INTO", "BY", "USE", "DATABASE", "COLUMN",
    "ASC", "DESC", "CHECK", "RANGE"
};

// 核心责任：将任意表头清洗为合法的数据库列名（入库前第一道关卡）。
// 规则：空→"column_N"；SQL保留字→"col_"前缀；非法字符→'_'；数字开头→"col_"前缀
// （去重不在本函数做——parseWorksheet 统一用 usedNames 集合处理清洗后碰撞）
// validateColumnName — 列名规范化
// | 原始列名 | columnIndex | 输出 | 触发规则 |
// | :--- | :--- | :--- | :--- |
// | `""` | 3 | `"column_3"` | 空列名 → 默认命名 |
// | `"SELECT"` | 1 | `"col_SELECT"` | SQL 保留字 → 加前缀 |
// | `"order"` | 2 | `"col_order"` | 保留字大小写不敏感匹配 |
// | `"金额(元)"` | 4 | `"金额_元_"` | `(` `)` → `_`，UTF-8 保留 |
// | `"A-B.C"` | 5 | `"A_B_C"` | `-` `.` → `_` |
// | `"3rd_col"` | 6 | `"col_3rd_col"` | 数字开头 → 加前缀 |
// | `"valid_name$"` | 7 | `"valid_name$"` | `$` 合法保留 |
// | `"___"` | 8 | `"___"` | 全下划线合法（但下游可能需额外校验） |
std::string ExcelParser::validateColumnName(const std::string& name, int columnIndex) {
    // 1. 空列名默认使用 "column_索引" 命名
    if (name.empty()) {
        return "column_" + std::to_string(columnIndex);
    }
    // 2. SQL 保留字：加 "col_" 前缀规避建表冲突（评审补充）
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (kSqlReservedWords.count(upper)) {
        return "col_" + name;
    }
    // 3. 无效字符（非字母/数字/下划线）替换为 '_'；保留 UTF-8 多字节（>=0x80）与 '$'
    //    （注：损坏的 UTF-8 序列会原样保留，属已知限制——xlsx 内 XML 通常保证合法）
    std::string result;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c >= 0x80 || c == '$') {
            result += c;
        } else {
            result += '_';
        }
    }
    // 4. 数字开头的列名添加 "col_" 前缀（数据库列名不能以数字开头）
    if (!result.empty() && std::isdigit(result[0])) {
        result = "col_" + result;
    }
    return result;
}

// 核心责任：为类型检测提供干净的输入（unsigned char 转型避免 isspace 负值 UB）。
// 空白字符清理
// | 输入 | 输出 | 说明 |
// | :--- | :--- | :--- |
// | `"  hello  "` | `"hello"` | 标准首尾空格去除 |
// | `"\t\n data \r\n"` | `"data"` | 混合制表符、换行符、回车符 |
// | `"   "` | `""` | 纯空白 → 空串 |
// | `""` | `""` | 空串 → 空串 |
// | `"no_space"` | `"no_space"` | 无空白 → 原样返回 |
// | `" \x80\xFF "` | `"\x80\xFF"` | UTF-8 多字节不被误判为空白（`unsigned char` 转型生效） |
std::string ExcelParser::trimWhitespace(const std::string& str) {
    size_t start = 0;
    size_t end = str.length();
    while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    return str.substr(start, end - start);
}

// 核心责任：精确判定字符串可否安全转数值（避免 stod 的异常/静默截断）。
// 格式：[符号] 数字/小数点 [e/E [符号] 数字]
// 支持 "123"、"-1.5"、"+.5"、"1.5E+10"；拒绝 "", ".", "-", "12.34.56", "1e", "--1"
// isNumericString — 数值检测
// | 输入 | 输出 | 原因 |
// | :--- | :--- | :--- |
// | `"123"` | ✅ | 纯整数 |
// | `"-1.5"` | ✅ | 负号 + 小数 |
// | `"+.5"` | ✅ | 正号 + 无前导零小数 |
// | `"1.5E+10"` | ✅ | 科学计数法（修复版新增） |
// | `"1.5e-3"` | ✅ | 小写 e + 负指数 |
// | `"  42  "` | ✅ | 首尾空格被 trim 后判定 |
// | `""` | ❌ | 空串 |
// | `"   "` | ❌ | trim 后为空 |
// | `"."` | ❌ | 无数字 |
// | `"-"` | ❌ | 仅符号无数字 |
// | `"12.34.56"` | ❌ | 多个小数点 |
// | `"1e"` | ❌ | 指数段无数字 |
// | `"1e+"` | ❌ | 指数段符号后无数字 |
// | `"--1"` | ❌ | 双符号 |
// | `"12abc"` | ❌ | 尾部非法字符，`pos != length` |
// | `"N/A"` | ❌ | 非数值文本 |

bool ExcelParser::isNumericString(const std::string& str) {
    if (str.empty()) { return false; }
    std::string trimmed = trimWhitespace(str);
    if (trimmed.empty()) { return false; }
    size_t pos = 0;
    // 1. 可选正负号
    if (trimmed[0] == '-' || trimmed[0] == '+') { pos = 1; }
    // 2. 尾数部分：数字与至多一个小数点，至少一个数字
    size_t dotPos = std::string::npos;
    size_t digitCount = 0;
    while (pos < trimmed.length()) {
        if (std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
            ++digitCount;
        } else if (trimmed[pos] == '.' && dotPos == std::string::npos) {
            dotPos = pos;
        } else {
            break;                      // 可能是指数段开始或非法字符，跳出细判
        }
        ++pos;
    }
    if (digitCount == 0) { return false; }
    // 3. 可选指数段：e/E + 可选符号 + 至少一个数字（课件缺失，评审补充：
    //    否则文本存储的 "1.5E+10" 会被误判为 TEXT）
    if (pos < trimmed.length() && (trimmed[pos] == 'e' || trimmed[pos] == 'E')) {
        ++pos;
        if (pos < trimmed.length() && (trimmed[pos] == '+' || trimmed[pos] == '-')) {
            ++pos;
        }
        size_t expDigits = 0;
        while (pos < trimmed.length() &&
               std::isdigit(static_cast<unsigned char>(trimmed[pos]))) {
            ++expDigits;
            ++pos;
        }
        if (expDigits == 0) { return false; }   // "1e"、"1e+" 判否
    }
    // 4. 必须整串消费完毕（课件此处 substr(pos) 为死代码，改为真正的整串校验）
    return pos == trimmed.length();
}

// 核心责任：宽泛匹配布尔语义（大小写不敏感；"1"/"0" 在 inferColumnType 中被数值先截走）。
// isBooleanString — 布尔值检测
// | 输入 | 输出 | 匹配集合 |
// | :--- | :--- | :--- |
// | `"TRUE"` | ✅ | 真值集（大小写不敏感） |
// | `"yes"` | ✅ | 真值集 |
// | `"Y"` | ✅ | 真值集 |
// | `"t"` | ✅ | 真值集 |
// | `"1"` | ✅ | 真值集（但在 `inferColumnType` 中被数值优先截走） |
// | `"false"` | ✅ | 假值集 |
// | `"NO"` | ✅ | 假值集 |
// | `"n"` | ✅ | 假值集 |
// | `"f"` | ✅ | 假值集 |
// | `"0"` | ✅ | 假值集（同上，被数值优先截走） |
// | `"maybe"` | ❌ | 不在任何集合中 |
// | `"2"` | ❌ | 非布尔语义 |
// | `""` | ❌ | 空串 |

bool ExcelParser::isBooleanString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "true" || lower == "1" || lower == "t" || lower == "yes" || lower == "y") {
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "f" || lower == "no" || lower == "n") {
        return true;
    }
    return false;
}

// 核心责任：识别 8 种常见日期格式。
// 评审修复：static const 只编译一次（原课件每次调用重编译 8 个正则，
// 100行×50列采样下累计数百毫秒纯编译开销）
// isDateString — 日期格式匹配
// | 输入 | 输出 | 匹配模式 |
// | :--- | :--- | :--- |
// | `"2024-01-15"` | ✅ | `\d{4}-\d{2}-\d{2}` |
// | `"2024/01/15"` | ✅ | `\d{4}/\d{2}/\d{2}` |
// | `"01-15-2024"` | ✅ | `\d{2}-\d{2}-\d{4}` |
// | `"01/15/2024"` | ✅ | `\d{2}/\d{2}/\d{4}` |
// | `"2024.01.15"` | ✅ | `\d{4}\.\d{2}\.\d{2}` |
// | `"15.01.2024"` | ✅ | `\d{2}\.\d{2}\.\d{4}` |
// | `"2024-01-15 14:30:00"` | ✅ | 带时间（`\s+` 分隔） |
// | `"2024/01/15 14:30:00"` | ✅ | 带时间（斜杠版） |
// | `"2024年01月15日"` | ❌ | 中文日期不支持（已知限制） |
// | `"2024-1-15"` | ❌ | 月/日必须两位（`\d{2}`） |
// | `"2024-01-15T14:30:00"` | ❌ | ISO8601 T 分隔符不支持 |
// | `"not-a-date"` | ❌ | 完全不匹配 |
// | `"2024-13-45"` | ✅ | ⚠️ 正则只做格式匹配，不做语义校验 |

bool ExcelParser::isDateString(const std::string& str) {
    static const std::vector<std::regex> kDatePatterns = {
        std::regex(R"(\d{4}-\d{2}-\d{2})"),                       // 2023-01-01
        std::regex(R"(\d{4}/\d{2}/\d{2})"),                       // 2023/01/01
        std::regex(R"(\d{2}-\d{2}-\d{4})"),                       // 01-01-2023
        std::regex(R"(\d{2}/\d{2}/\d{4})"),                       // 01/01/2023
        std::regex(R"(\d{4}\.\d{2}\.\d{2})"),                     // 2023.01.01
        std::regex(R"(\d{2}\.\d{2}\.\d{4})"),                     // 01.01.2023
        std::regex(R"(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})"),   // 2023-01-01 12:00:00
        std::regex(R"(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2})"),   // 2023/01/01 12:00:00
    };
    for (const auto& pattern : kDatePatterns) {
        if (std::regex_match(str, pattern)) {
            return true;
        }
    }
    return false;
}

// 核心责任：语义层决策中枢——采样逐格判型投票，产出建表用的列类型。
// 评审修复：同票数按固定优先级 TEXT>DOUBLE>BIGINT>BOOLEAN>DATE 裁决
// （unordered_map 遍历序不稳定 → 原版同票结果随机；TEXT 最安全所以同票胜出）
// inferColumnType — 类型投票推断
// 场景 A：明确多数派
// 文本

// 采样值: ["100", "200", "300", "hello", ""]
// → BIGINT=3, TEXT=1 (空值跳过)
// → 结果: "BIGINT"
// 场景 B：字符串二次深挖
// 文本

// 采样值: ["1.5", "2.7", "3.14", "N/A", "text"]
// → isNumericString("1.5")=✅ → DOUBLE
// → isNumericString("2.7")=✅ → DOUBLE  
// → isNumericString("3.14")=✅ → DOUBLE
// → "N/A" 跳过, "text" → TEXT
// → DOUBLE=3, TEXT=1
// → 结果: "DOUBLE"    ← 而非 TEXT！
// 场景 C：同票数 Tie-Breaker
// 文本

// 采样值: ["100", "200", "hello", "world"]
// → BIGINT=2, TEXT=2
// → kTypePriority = {"TEXT", "DOUBLE", "BIGINT", ...}
// → TEXT 优先级更高
// → 结果: "TEXT"      ← 安全兜底，不会因选 BIGINT 导致写入失败
// 场景 D：全空/N/A 列
// 文本

// 采样值: ["", "N/A", "", "n/a", ""]
// → 全部跳过，typeCount 为空
// → 结果: "TEXT"      ← 安全默认值
// 场景 E：布尔 vs 数值竞争
// 文本

// 采样值: ["1", "0", "true", "false", "yes"]
// → "1": isNumericString=✅ → DOUBLE (数值优先于布尔)
// → "0": isNumericString=✅ → DOUBLE
// → "true": isNumericString=❌ → isBooleanString=✅ → BOOLEAN
// → "false": → BOOLEAN
// → "yes": → BOOLEAN
// → DOUBLE=2, BOOLEAN=3
// → 结果: "BOOLEAN"
// 💡 注意：如果 "1" 和 "0" 在 Excel 中是真正的 Boolean 物理类型（而非 String），则走 cell.type == "Boolean" 直接映射分支，不经过字符串检测，此时 BOOLEAN=5，结果仍为 BOOLEAN。
std::string ExcelParser::inferColumnType(const std::vector<CellData>& cellValues) {
    // 优先级即 tie-breaker 顺序：TEXT 字符串永远可入库，同票时最安全
    static const std::vector<std::string> kTypePriority = {
        "TEXT", "DOUBLE", "BIGINT", "BOOLEAN", "DATE"
    };
    std::unordered_map<std::string, int> typeCount;
    for (const auto& cell : cellValues) {
        // 1. 空值与 N/A 不参与投票
        if (cell.value.empty() || cell.value == "N/A" || cell.value == "n/a") {
            continue;
        }
        // 2. 判型：OpenXLSX 原生类型 → DB 类型；字符串二次深挖（数值→布尔→日期顺序有讲究：
        //    "1" 命中数值、"2023-01-01" 的 '-' 会被数值判别拦下落到日期）
        std::string detectedType;
        if (cell.type == "Integer") {
            detectedType = "BIGINT";
        } else if (cell.type == "Float") {
            detectedType = "DOUBLE";
        } else if (cell.type == "Boolean") {
            detectedType = "BOOLEAN";
        } else if (cell.type == "String") {
            if (isNumericString(cell.value)) {
                detectedType = "DOUBLE";
            } else if (isBooleanString(cell.value)) {
                detectedType = "BOOLEAN";
            } else if (isDateString(cell.value)) {
                detectedType = "DATE";
            } else {
                detectedType = "TEXT";
            }
        } else {
            detectedType = "TEXT";
        }
        ++typeCount[detectedType];
    }
    // 3. 全空列（全空值/全 N/A）默认 TEXT
    if (typeCount.empty()) {
        return "TEXT";
    }
    // 4. 按优先级顺序取严格更多者 —— 同票数时优先级高者（更安全的类型）胜出
    std::string maxType;
    int maxCount = 0;
    for (const auto& type : kTypePriority) {
        auto it = typeCount.find(type);
        if (it != typeCount.end() && it->second > maxCount) {
            maxCount = it->second;
            maxType = type;
        }
    }
    return maxType;
}

// ==================== parseWorksheet：单遍扫描版 ====================

// RAII 关闭器：无论正常返回还是异常路径都确保文档关闭（课件手动 close 的补充）
namespace {
struct DocGuard {
    OpenXLSX::XLDocument* doc;
    ~DocGuard() {
        if (doc && doc->isOpen()) {
            try { doc->close(); } catch (...) {}
        }
    }
};
} // namespace

// 核心责任：端到端编排器——单遍扫描完成表头/采样/数据行（评审重构替代双遍随机访问）。
// @return 成功返回 WorksheetInfo；文件/工作表不存在或解析异常返回 nullptr
// 输入文件: test.xlsx, Sheet: "Data"
// 总行数: 6 (含表头), 总列数: 3

// ═══ 阶段1: 迭代器扫描 ═══
// iterRows=1 → headerCells = ["ID", "SELECT", ""]
// iterRows=2 → cells = ["A1", "100", "2024-01-01"]
//            → sampledRows[0] = copy(cells)
//            → rows.push_back(move(cells))     ← 零拷贝
// iterRows=3 → cells = ["A2", "yes", "2024/02/28"]
//            → sampledRows[1] = copy(cells)
//            → rows.push_back(move(cells))
// ...
// iterRows=6 → 最后一行，sampledRows 已满(上限5)，仅 push rows

// ═══ 阶段2: 列名规范化 + 去重 ═══
// headerCells[0] = "ID"     → validateColumnName → "ID"       → usedNames={"ID"}
// headerCells[1] = "SELECT" → validateColumnName → "col_SELECT" → usedNames+={"col_SELECT"}
// headerCells[2] = ""       → validateColumnName → "column_3"  → usedNames+={"column_3"}

// ═══ 阶段3: 按列汇集采样 + 投票 ═══
// Col0 采样: ["A1","A2",...] → 全String → TEXT
// Col1 采样: ["100","yes",...] → DOUBLE=1,BOOLEAN=4 → BOOLEAN
// Col2 采样: ["2024-01-01","2024/02/28",...] → DATE=5 → DATE

// ═══ 最终输出 ═══
// columns: [{name:"ID", type:"TEXT"}, 
//           {name:"col_SELECT", type:"BOOLEAN"}, 
//           {name:"column_3", type:"DATE"}]
// rows: 5 条数据行（已 move 进 vector，无重复读取）
// 文件: DocGuard 析构自动关闭 ✅
std::unique_ptr<WorksheetInfo> ExcelParser::parseWorksheet(const std::string& filePath,
                                                           const std::string& worksheetName) {
    auto worksheetInfo = std::make_unique<WorksheetInfo>();
    try {
        // 1. 打开Excel文件（DocGuard 保证任何路径都关闭）
        OpenXLSX::XLDocument doc;
        DocGuard guard{&doc};
        doc.open(filePath);
        if (!doc.isOpen()) {
            ERR("Failed to open Excel file: {}", filePath);
            return nullptr;
        }
        // 2. 获取工作簿对象
        auto workbook = doc.workbook();
        // 3. 检查工作表是否存在
        if (!workbook.worksheetExists(worksheetName)) {
            ERR("Worksheet '{}' not found in file: {}", worksheetName, filePath);
            return nullptr;
        }
        // 4. 获取工作表对象及规模
        auto worksheet = workbook.worksheet(worksheetName);
        worksheetInfo->name = worksheetName;
        worksheetInfo->totalRows = static_cast<int>(worksheet.rowCount());
        worksheetInfo->totalCols = static_cast<int>(worksheet.columnCount());
        INF("Parsing worksheet '{}' with {} rows and {} columns",
            worksheetName, worksheetInfo->totalRows, worksheetInfo->totalCols);
        if (worksheetInfo->totalRows == 0 || worksheetInfo->totalCols == 0) {
            ERR("Worksheet '{}' is empty", worksheetName);
            return nullptr;
        }
        // 5~8. 单遍扫描（评审重构①②：顺序迭代器替代逐格 cell(row,col) 随机访问；
        //      采样行缓存直接 move 进数据行，前 100 行不再读第二遍）：
        //      第 1 行=表头；2..min(101,总行数)=采样；所有数据行入 rows
        int sampleLimit = std::min(100, worksheetInfo->totalRows - 1);
        std::vector<std::vector<CellData>> sampledRows;
        sampledRows.reserve(sampleLimit);
        std::vector<CellData> headerCells;
        int iterRows = 0;   // 迭代器实际产出的行数（用于与 rowCount 对账）
        for (auto& row : worksheet.rows()) {
            ++iterRows;
            std::vector<CellData> cells;
            cells.reserve(worksheetInfo->totalCols);
            int colIdx = 0;
            for (auto& cell : row.cells(static_cast<uint16_t>(worksheetInfo->totalCols))) {
                if (colIdx >= worksheetInfo->totalCols) { break; }  // 维度外防御
                cells.push_back(parseCellValue(cell.value()));
                ++colIdx;
            }
            // 行尾空单元格可能被迭代器跳过，补齐到列数（对齐正确性由 E8 单测把关）
            while (static_cast<int>(cells.size()) < worksheetInfo->totalCols) {
                cells.push_back(CellData{"", "Empty"});
            }
            if (iterRows == 1) {
                headerCells = std::move(cells);     // 第 1 行为表头
                continue;
            }
            if (static_cast<int>(sampledRows.size()) < sampleLimit) {
                sampledRows.push_back(cells);       // 采样留副本（拷贝）
            }
            RowData rowData;
            rowData.cells = std::move(cells);       // 数据行零拷贝收编
            worksheetInfo->rows.push_back(std::move(rowData));
        }
        if (iterRows != worksheetInfo->totalRows) {
            WRN("Worksheet '{}' iterated {} rows but rowCount reported {} (fully empty rows may be skipped)",
                worksheetName, iterRows, worksheetInfo->totalRows);
        }
        // 6. 列名规范化 + 去重（评审补充：清洗后可能碰撞，如 "A-B"/"A.B" 都成 "A_B"）
        std::unordered_set<std::string> usedNames;
        usedNames.reserve(headerCells.size());
        for (size_t i = 0; i < headerCells.size(); ++i) {
            std::string base = validateColumnName(headerCells[i].value, static_cast<int>(i) + 1);
            std::string finalName = base;
            int suffix = 2;
            while (!usedNames.insert(finalName).second) {
                finalName = base + "_" + std::to_string(suffix++);
            }
            ColumnData column;
            column.name = finalName;
            worksheetInfo->columns.push_back(column);
        }
        // 7. 按列汇集采样数据，投票推断类型
        for (size_t col = 0; col < worksheetInfo->columns.size(); ++col) {
            std::vector<CellData> columnValues;
            columnValues.reserve(sampledRows.size());
            for (const auto& row : sampledRows) {
                if (col < row.size()) {
                    columnValues.push_back(row[col]);
                }
            }
            worksheetInfo->columns[col].type = inferColumnType(columnValues);
        }
        INF("Successfully parsed worksheet '{}': {} columns, {} rows",
            worksheetName, worksheetInfo->columns.size(), worksheetInfo->rows.size());
        // 8~9. 数据行已在扫描中收编；文件由 DocGuard 关闭
    } catch (const OpenXLSX::XLException& e) {
        ERR("OpenXLSX exception while parsing worksheet: {}", e.what());
        return nullptr;
    } catch (const std::exception& e) {
        ERR("Exception while parsing worksheet: {}", e.what());
        return nullptr;
    }
    return worksheetInfo;
}

} // namespace excelParserService
