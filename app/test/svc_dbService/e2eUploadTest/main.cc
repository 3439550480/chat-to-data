// G9b：跨服务端到端测试（brpc 客户端，四服务联动）
// 链路：本程序 → FileService.UploadFileInfo/UploadFile(attachment xlsx)
//        → [FileService] FDFS 存储 → ExcelParserService.GetWorksheets
//        → 写 tbl_worksheet 映射 → ExcelParserService.ParseExcel
//        → DatabaseService.ImportExcelData（建表 + 批量入库）
// 验证：MySQL 出现新表且数据正确 → FileService.PreviewExcel 能读回 →
//       FileService.DeleteFile 触发 DropTableExcel，表从库中消失（闭环）
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <OpenXLSX.hpp>
#include <brpc/channel.h>
#include <bite_scaffold/log.h>
#include "../../../proto/protoCode/fileService.pb.h"
#include "../../../proto/protoCode/dbService.pb.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            INF("[OK]   {}", msg);                                              \
        } else {                                                                \
            ERR("[FAIL] {}", msg);                                              \
            ++g_fail;                                                           \
        }                                                                       \
    } while (0)

// 生成测试用 Excel（表头 + 3 行数据，含中文与浮点）
static std::string buildTestXlsx() {
    std::string path = "/tmp/e2e_upload_" + std::to_string(::time(nullptr)) + ".xlsx";
    OpenXLSX::XLDocument doc;
    doc.create(path);
    auto ws = doc.workbook().worksheet("Sheet1");
    ws.cell(1, 1).value() = "客户名称";
    ws.cell(1, 2).value() = "金额";
    const char* names[3] = {"张三", "李四", "王五"};
    double amounts[3] = {1234.56, 2345.67, 3456.78};
    for (int i = 0; i < 3; ++i) {
        ws.cell(i + 2, 1).value() = names[i];
        ws.cell(i + 2, 2).value() = amounts[i];
    }
    doc.save();
    doc.close();
    return path;
}

int main() {
    bitelog::bitelog_init();

    // 0. 造 Excel 并读入内存
    std::string xlsxPath = buildTestXlsx();
    std::ifstream ifs(xlsxPath, std::ios::binary);
    std::string fileData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    CHECK(!fileData.empty(), "test xlsx generated and loaded");

    // 1. 连接文件服务
    brpc::Channel fileChannel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 60000;
    if (fileChannel.Init("dev-env-service:9004", &options) != 0) {
        ERR("failed to init file service channel");
        return 1;
    }
    chat2Data::fileService::FileService_Stub fileStub(&fileChannel);
    const std::string sessionId = "e2e_session";
    const std::string userId = "e2e_user";

    // 2. 上传文件元信息（第一段）
    chat2Data::fileService::UploadFileInfoRequest infoReq;
    infoReq.set_request_id("e2e_req_1");
    infoReq.set_session_id(sessionId);
    infoReq.set_user_id(userId);
    infoReq.mutable_file_info()->set_filename("e2e销售数据.xlsx");
    infoReq.mutable_file_info()->set_file_size(static_cast<int64_t>(fileData.size()));
    infoReq.mutable_file_info()->set_file_ext(".xlsx");
    chat2Data::fileService::UploadFileInfoResponse infoResp;
    brpc::Controller infoCntl;
    fileStub.UploadFileInfo(&infoCntl, &infoReq, &infoResp, nullptr);
    CHECK(!infoCntl.Failed() && infoResp.error_code() == 0, "UploadFileInfo");
    std::string fileId = infoResp.result().file_id();
    CHECK(!fileId.empty(), "got file_id from UploadFileInfo");
    if (fileId.empty()) { std::remove(xlsxPath.c_str()); return 1; }

    // 3. 上传文件数据（第二段：数据走 attachment）
    chat2Data::fileService::UploadFileRequest uploadReq;
    uploadReq.set_request_id("e2e_req_2");
    uploadReq.set_session_id(sessionId);
    uploadReq.set_file_id(fileId);
    uploadReq.set_user_id(userId);
    chat2Data::fileService::UploadFileResponse uploadResp;
    brpc::Controller uploadCntl;
    uploadCntl.request_attachment().append(fileData);      // 零拷贝通道
    fileStub.UploadFile(&uploadCntl, &uploadReq, &uploadResp, nullptr);
    CHECK(!uploadCntl.Failed() && uploadResp.error_code() == 0, "UploadFile (attachment)");

    // 4. 连接数据库服务，找出现的新表（表名 = Sheet1_<fileId 连字符换下划线>）
    brpc::Channel dbChannel;
    if (dbChannel.Init("dev-env-service:9005", &options) != 0) {
        ERR("failed to init db service channel");
        std::remove(xlsxPath.c_str());
        return 1;
    }
    chat2Data::DatabaseService::DatabaseService_Stub dbStub(&dbChannel);
    chat2Data::DatabaseService::ListTablesRequest listReq;
    listReq.set_request_id("e2e_req_3");
    listReq.set_session_id(sessionId);
    listReq.set_db_connect_id("excel_default");
    chat2Data::DatabaseService::ListTablesResponse listResp;
    brpc::Controller listCntl;
    dbStub.ListTables(&listCntl, &listReq, &listResp, nullptr);
    CHECK(!listCntl.Failed() && listResp.error_code() == 0, "DatabaseService.ListTables");
    std::string expectedTable;
    for (const auto& t : listResp.result().tables()) {
        if (t.rfind("Sheet1_", 0) == 0 && t.find(fileId.substr(0, 4)) != std::string::npos) {
            expectedTable = t;
            break;
        }
    }
    CHECK(!expectedTable.empty(), "imported worksheet table exists in MySQL");
    INF("imported table name: {}", expectedTable);

    // 5. 读回表数据验证（force_original=true）
    if (!expectedTable.empty()) {
        chat2Data::DatabaseService::GetTableDataRequest dataReq;
        dataReq.set_request_id("e2e_req_4");
        dataReq.set_session_id(sessionId);
        dataReq.set_db_connect_id("excel_default");
        dataReq.set_table_name(expectedTable);
        dataReq.set_force_original(true);
        dataReq.set_page_number(1);
        dataReq.set_page_size(10);
        chat2Data::DatabaseService::GetTableDataResponse dataResp;
        brpc::Controller dataCntl;
        dbStub.GetTableData(&dataCntl, &dataReq, &dataResp, nullptr);
        CHECK(!dataCntl.Failed() && dataResp.error_code() == 0, "DatabaseService.GetTableData");
        const auto& schema = dataResp.result().table_schema();
        CHECK(schema.table_data().total_rows() == 3, "imported row count == 3");
        CHECK(schema.column_info_size() == 3, "column count == 3 (id + 客户名称 + 金额)");
        std::string col1 = schema.column_info_size() > 1 ? schema.column_info(1).name() : "";
        std::string col2 = schema.column_info_size() > 2 ? schema.column_info(2).name() : "";
        CHECK(col1 == "客户名称" && col2 == "金额", "chinese column names preserved");
        if (schema.table_data().rows_size() > 0) {
            const auto& firstRow = schema.table_data().rows(0);
            CHECK(firstRow.cells_size() >= 3 && firstRow.cells(1) == "张三",
                  "chinese cell value preserved (张三)");
        }
    }

    // 6. 预览 Excel（走 FileService.previewExcel → getExcelDataFromDB 回收后的路径）
    chat2Data::fileService::PreviewExcelRequest previewReq;
    previewReq.set_request_id("e2e_req_5");
    previewReq.set_session_id(sessionId);
    previewReq.set_file_id(fileId);
    previewReq.set_user_id(userId);
    previewReq.set_page_number(1);
    previewReq.set_page_size(50);
    chat2Data::fileService::PreviewExcelResponse previewResp;
    brpc::Controller previewCntl;
    fileStub.PreviewExcel(&previewCntl, &previewReq, &previewResp, nullptr);
    CHECK(!previewCntl.Failed() && previewResp.error_code() == 0, "FileService.PreviewExcel");
    const auto& excelData = previewResp.result().excel_data();
    CHECK(excelData.sheets_size() == 1, "preview returns 1 sheet");
    if (excelData.sheets_size() > 0) {
        const auto& sheet = excelData.sheets(0);
        CHECK(sheet.name() == "Sheet1", "preview sheet name == Sheet1");
        CHECK(sheet.total_rows() == 3, "preview total_rows == 3");
        CHECK(sheet.columns_size() == 3, "preview columns count == 3");
        CHECK(sheet.data_size() == 3, "preview returns 3 rows");
    }

    // 7. 删除文件 → 应联动删除数据库表（DropTableExcel 闭环）
    chat2Data::fileService::DeleteFileRequest delReq;
    delReq.set_request_id("e2e_req_6");
    delReq.set_session_id(sessionId);
    delReq.set_file_id(fileId);
    delReq.set_user_id(userId);
    chat2Data::fileService::DeleteFileResponse delResp;
    brpc::Controller delCntl;
    fileStub.DeleteFile(&delCntl, &delReq, &delResp, nullptr);
    CHECK(!delCntl.Failed() && delResp.error_code() == 0, "FileService.DeleteFile");

    chat2Data::DatabaseService::ListTablesResponse listResp2;
    brpc::Controller listCntl2;
    dbStub.ListTables(&listCntl2, &listReq, &listResp2, nullptr);
    bool tableStillExists = false;
    for (const auto& t : listResp2.result().tables()) {
        if (t == expectedTable) { tableStillExists = true; }
    }
    CHECK(!tableStillExists, "imported table dropped after DeleteFile (closed loop)");

    std::remove(xlsxPath.c_str());
    INF("========== e2e summary: failCount={} ==========", g_fail);
    return g_fail == 0 ? 0 : 1;
}
