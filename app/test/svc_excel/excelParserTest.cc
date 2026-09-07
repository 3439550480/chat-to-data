#include <gtest/gtest.h>
#include <OpenXLSX.hpp>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <bite_scaffold/log.h>
#include "../../svc_excelParseService/excelParser.h"

using namespace excelParserService;

int main(int argc, char** argv) {
    bitelog::bitelog_init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// 生成覆盖全部风险点的测试 xlsx（每次测试独立文件，测试完删除）
static std::string buildTestFile(const std::string& path) {
    OpenXLSX::XLDocument doc;
    doc.create(path);
    auto wbx = doc.workbook();
    wbx.addWorksheet("Empty");
    wbx.addWorksheet("Gaps");
    auto ws = wbx.worksheet("Sheet1");

    // ---- 表头（10 列，覆盖：中文/数字开头/大写字母/保留字/碰撞对/科学计数法/尾部空列）----
    const char* headers[] = {"姓名", "Age", "price", "ok", "day",
                             "A-B", "A.B", "select", "123abc", "tail"};
    for (int c = 1; c <= 10; ++c) {
        ws.cell(1, c).value() = headers[c - 1];
    }
    // ---- 3 行数据 ----
    struct Row { const char* name; int age; double price; bool ok; const char* day;
                 const char* ab1; const char* ab2; const char* sel; const char* num; };
    const Row rows[] = {
        {"张三", 25, 3.14, true,  "2023-01-01", "x1", "x3", "hello", "1.5E+10"},
        {"李四", 30, 2.71, false, "2023/06/15", "x2", "x4", "world", "2.0E+3"},
        {"王五", 35, 9.99, true,  "2024-12-31", "x5", "x6", "text",  "3.25E-2"},
    };
    for (int r = 0; r < 3; ++r) {
        int row = r + 2;
        ws.cell(row, 1).value() = rows[r].name;   // String→TEXT（中文）
        ws.cell(row, 2).value() = rows[r].age;    // Integer→BIGINT
        ws.cell(row, 3).value() = rows[r].price;  // Float→DOUBLE
        ws.cell(row, 4).value() = rows[r].ok;     // Boolean→BOOLEAN
        ws.cell(row, 5).value() = rows[r].day;    // String(日期)→DATE
        ws.cell(row, 6).value() = rows[r].ab1;    // 清洗碰撞对
        ws.cell(row, 7).value() = rows[r].ab2;
        ws.cell(row, 8).value() = rows[r].sel;    // 保留字
        ws.cell(row, 9).value() = rows[r].num;    // 科学计数法文本→DOUBLE
        // 第 10 列（tail）数据格故意不写 → 行尾空单元格对齐验证
    }
    // ---- Gaps 表：row2 有数据、row3 全空（节点不存在）、row4 有数据 ----
    auto gp = wbx.worksheet("Gaps");
    gp.cell(1, 1).value() = "id";
    gp.cell(2, 1).value() = "a";
    gp.cell(4, 1).value() = "b";
    doc.save();
    doc.close();
    return path;
}

class ExcelParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        _path = buildTestFile("/tmp/excel_parser_test_" +
                              std::to_string(::time(nullptr)) + "_" +
                              std::to_string(_seq++) + ".xlsx");
    }
    void TearDown() override { std::remove(_path.c_str()); }
    std::string _path;
    static int _seq;
};
int ExcelParserTest::_seq = 0;

// 1. 表名列表：包含全部 3 张表
TEST_F(ExcelParserTest, GetWorksheetNames) {
    ExcelParser parser;
    auto names = parser.getWorksheetNames(_path);
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "Sheet1");
    // addWorksheet 顺序：Empty、Gaps（这里验证存在性）
    ASSERT_TRUE(std::find(names.begin(), names.end(), "Empty") != names.end());
    ASSERT_TRUE(std::find(names.begin(), names.end(), "Gaps") != names.end());
}

// 2. 主表全量验证：规模 / 列名清洗 / 类型推断七路
TEST_F(ExcelParserTest, ParseMainSheet) {
    ExcelParser parser;
    auto info = parser.parseWorksheet(_path, "Sheet1");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "Sheet1");
    EXPECT_EQ(info->totalRows, 4);   // 1 表头 + 3 数据
    EXPECT_EQ(info->totalCols, 10);
    EXPECT_EQ(info->rows.size(), 3u);
    ASSERT_EQ(info->columns.size(), 10u);

    // 列名清洗与去重（评审点 1b/1c）
    EXPECT_EQ(info->columns[0].name, "姓名");        // UTF-8 保留
    EXPECT_EQ(info->columns[1].name, "Age");
    EXPECT_EQ(info->columns[5].name, "A_B");          // "A-B" 清洗
    EXPECT_EQ(info->columns[6].name, "A_B_2");        // "A.B" 碰撞 → 去重后缀
    EXPECT_EQ(info->columns[7].name, "col_select");   // SQL 保留字
    EXPECT_EQ(info->columns[8].name, "col_123abc");   // 数字开头

    // 类型推断七路（评审点：数值/浮点/布尔/日期/文本/科学计数法/全空默认）
    EXPECT_EQ(info->columns[0].type, "TEXT");
    EXPECT_EQ(info->columns[1].type, "BIGINT");
    EXPECT_EQ(info->columns[2].type, "DOUBLE");
    EXPECT_EQ(info->columns[3].type, "BOOLEAN");
    EXPECT_EQ(info->columns[4].type, "DATE");
    EXPECT_EQ(info->columns[8].type, "DOUBLE");       // "1.5E+10" 文本→数值（评审补充）
    EXPECT_EQ(info->columns[9].type, "TEXT");         // 全空列默认 TEXT

    // 数据行内容抽查 + 行尾空单元格对齐（评审点 7b 赌注①）
    EXPECT_EQ(info->rows[0].cells[0].value, "张三");
    EXPECT_EQ(info->rows[0].cells[0].type, "String");
    EXPECT_EQ(info->rows[0].cells[1].value, "25");
    EXPECT_EQ(info->rows[0].cells[1].type, "Integer");
    EXPECT_EQ(info->rows[0].cells[3].value, "1");     // bool true → "1"
    EXPECT_EQ(info->rows[0].cells[3].type, "Boolean");
    EXPECT_EQ(info->rows[0].cells[9].value, "");      // tail 列数据为空
    EXPECT_EQ(info->rows[0].cells[9].type, "Empty");  // 补齐的行尾空单元格
    EXPECT_EQ(info->rows[0].cells.size(), 10u);       // 每行列数恒等于 totalCols
    EXPECT_EQ(info->rows[2].cells[8].value, "3.25E-2");
}

// 3. 中间全空行：row3 无节点 → 迭代器物化空行占位（实测语义），
//    数据行下标与工作表行号保持对齐：rows = ["a", "", "b"]
TEST_F(ExcelParserTest, ParseGapsSheet) {
    ExcelParser parser;
    auto info = parser.parseWorksheet(_path, "Gaps");
    ASSERT_NE(info, nullptr);
    ASSERT_EQ(info->rows.size(), 3u);                // row2/row3(物化空行)/row4
    EXPECT_EQ(info->rows[0].cells[0].value, "a");
    EXPECT_EQ(info->rows[1].cells[0].value, "");     // 空行原位保留
    EXPECT_EQ(info->rows[1].cells[0].type, "Empty");
    EXPECT_EQ(info->rows[2].cells[0].value, "b");    // 对齐未错位
}

// 4. 空表 → nullptr（防御路径）
TEST_F(ExcelParserTest, ParseEmptySheet) {
    ExcelParser parser;
    EXPECT_EQ(parser.parseWorksheet(_path, "Empty"), nullptr);
}

// 5. 不存在的工作表 → nullptr
TEST_F(ExcelParserTest, ParseNonExistentSheet) {
    ExcelParser parser;
    EXPECT_EQ(parser.parseWorksheet(_path, "NoSuchSheet"), nullptr);
}

// 6. 不存在的文件 → 表名列表为空 + 解析 nullptr
TEST_F(ExcelParserTest, ParseNonExistentFile) {
    ExcelParser parser;
    EXPECT_TRUE(parser.getWorksheetNames("/tmp/no_such_file_xxx.xlsx").empty());
    EXPECT_EQ(parser.parseWorksheet("/tmp/no_such_file_xxx.xlsx", "Sheet1"), nullptr);
}

// 7. 类型推断 tie-breaker（评审点 6a）：等量 TEXT 与 BOOLEAN → TEXT 胜出
TEST(ExcelParserInfer, TieBreakPreferText) {
    ExcelParser parser;
    std::vector<CellData> sample = {
        {"true", "String"},   // isBooleanString 命中 → BOOLEAN
        {"hello", "String"},  // TEXT
        {"true", "String"},
        {"world", "String"},
    };
    EXPECT_EQ(parser.inferColumnType(sample), "TEXT");
}

// 8. 纯辅助函数抽测：数值判别边界（评审补充的指数段）
TEST(ExcelParserInfer, NumericEdgeCases) {
    ExcelParser parser;
    EXPECT_TRUE(parser.isNumericString("123"));
    EXPECT_TRUE(parser.isNumericString("-1.5"));
    EXPECT_TRUE(parser.isNumericString("+.5"));
    EXPECT_TRUE(parser.isNumericString("1.5E+10"));    // 指数段
    EXPECT_TRUE(parser.isNumericString("2.0E-3"));
    EXPECT_TRUE(parser.isNumericString(" 42 "));
    EXPECT_FALSE(parser.isNumericString(""));
    EXPECT_FALSE(parser.isNumericString("."));
    EXPECT_FALSE(parser.isNumericString("-"));
    EXPECT_FALSE(parser.isNumericString("12.34.56"));
    EXPECT_FALSE(parser.isNumericString("1e"));        // 指数无数字
    EXPECT_FALSE(parser.isNumericString("--1"));
    EXPECT_FALSE(parser.isNumericString("abc"));
}
