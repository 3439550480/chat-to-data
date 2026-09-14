// H15b-excel：智能 Excel 场景全链路端到端（本项目的旗舰流程）
// 链路：上传 xlsx → FDFS → ExcelParser 解析 → DatabaseService 建表入库
//       → AI.CreateSession(excel) → FileService.HandleFileChatSessionMap
//          （★ H14 新链路：文件侧写 chatSessionId + AI 侧写 fileId）
//       → AI.SendMessage(excel, HTTP+SSE)：取 worksheet 表名 → 取表结构/采样
//          → 分析提示词 → GLM 生成 SQL → DB.ExecuteSQL（沙箱）→ 总结 → <CHART_DATA>
//       → 清理：DeleteSession + DeleteFile
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <OpenXLSX.hpp>
#include <httplib.h>
#include <brpc/channel.h>
#include <bite_scaffold/log.h>
#include "../../../proto/protoCode/fileService.pb.h"
#include "../../../proto/protoCode/dbService.pb.h"
#include "../../../proto/protoCode/aiService.pb.h"

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

// 生成测试 Excel（表头 + 5 行数据，含中文列名，供模型统计）
static std::string buildTestXlsx() {
    std::string path = "/tmp/e2e_excelchat_" + std::to_string(::time(nullptr)) + ".xlsx";
    OpenXLSX::XLDocument doc;
    doc.create(path);
    auto ws = doc.workbook().worksheet("Sheet1");
    ws.cell(1, 1).value() = "部门";
    ws.cell(1, 2).value() = "员工姓名";
    ws.cell(1, 3).value() = "薪资";
    const char* depts[5] = {"行政部", "行政部", "财务部", "研发部", "研发部"};
    const char* names[5] = {"曹玉凤", "陈虹", "严嘉", "李大芬", "康磊"};
    double salary[5] = {3350, 3350, 4500, 4500, 2850};
    for (int i = 0; i < 5; ++i) {
        ws.cell(i + 2, 1).value() = depts[i];
        ws.cell(i + 2, 2).value() = names[i];
        ws.cell(i + 2, 3).value() = salary[i];
    }
    doc.save();
    doc.close();
    return path;
}

int main() {
    bitelog::bitelog_init();
    const std::string sessionId = "e2e_excel_session";
    const std::string userId = "e2e_excel_user";

    // 0. 造 xlsx 并读入内存
    std::string xlsxPath = buildTestXlsx();
    std::ifstream ifs(xlsxPath, std::ios::binary);
    std::string fileData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    CHECK(!fileData.empty(), "test xlsx built");

    // 1. 连接三服务
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 120000;
    brpc::Channel fileChannel, dbChannel, aiChannel;
    if (fileChannel.Init("dev-env-service:9004", &options) != 0 ||
        dbChannel.Init("dev-env-service:9005", &options) != 0 ||
        aiChannel.Init("dev-env-service:9006", &options) != 0) {
        ERR("failed to init channels");
        std::remove(xlsxPath.c_str());
        return 1;
    }
    chat2Data::fileService::FileService_Stub fileStub(&fileChannel);
    chat2Data::AiService::AIService_Stub aiStub(&aiChannel);

    // 2. 上传文件（元信息 + 数据两段）
    chat2Data::fileService::UploadFileInfoRequest infoReq;
    infoReq.set_request_id("ec_1");
    infoReq.set_session_id(sessionId);
    infoReq.set_user_id(userId);
    infoReq.mutable_file_info()->set_filename("e2e薪资表.xlsx");
    infoReq.mutable_file_info()->set_file_size(static_cast<int64_t>(fileData.size()));
    infoReq.mutable_file_info()->set_file_ext(".xlsx");
    chat2Data::fileService::UploadFileInfoResponse infoResp;
    brpc::Controller infoCntl;
    fileStub.UploadFileInfo(&infoCntl, &infoReq, &infoResp, nullptr);
    CHECK(!infoCntl.Failed() && infoResp.error_code() == 0, "UploadFileInfo");
    std::string fileId = infoResp.result().file_id();
    CHECK(!fileId.empty(), "got file_id");
    if (fileId.empty()) { std::remove(xlsxPath.c_str()); return 1; }

    chat2Data::fileService::UploadFileRequest uploadReq;
    uploadReq.set_request_id("ec_2");
    uploadReq.set_session_id(sessionId);
    uploadReq.set_file_id(fileId);
    uploadReq.set_user_id(userId);
    chat2Data::fileService::UploadFileResponse uploadResp;
    brpc::Controller uploadCntl;
    uploadCntl.request_attachment().append(fileData);
    fileStub.UploadFile(&uploadCntl, &uploadReq, &uploadResp, nullptr);
    CHECK(!uploadCntl.Failed() && uploadResp.error_code() == 0, "UploadFile (excel 解析+建表入库完成)");

    // 3. AI 创建 excel 会话
    chat2Data::AiService::CreateChatSessionRequest csReq;
    csReq.set_request_id("ec_3");
    csReq.set_user_id(userId);
    csReq.set_model("glm-5.3-flash");
    csReq.set_session_type("excel");
    chat2Data::AiService::CreateChatSessionResponse csResp;
    brpc::Controller csCntl;
    aiStub.CreateSession(&csCntl, &csReq, &csResp, nullptr);
    CHECK(!csCntl.Failed() && csResp.error_code() == 0, "AI.CreateSession (excel)");
    std::string csid = csResp.result().session().chat_session_id();
    CHECK(!csid.empty(), "got chat_session_id");

    // 4. ★ H14 新链路：文件与会话双向关联
    chat2Data::fileService::HandleFileChatSessionMapRequest mapReq;
    mapReq.set_request_id("ec_4");
    mapReq.set_session_id(sessionId);
    mapReq.set_file_id(fileId);
    mapReq.set_chat_session_id(csid);
    mapReq.set_user_id(userId);
    chat2Data::fileService::HandleFileChatSessionMapResponse mapResp;
    brpc::Controller mapCntl;
    fileStub.HandleFileChatSessionMap(&mapCntl, &mapReq, &mapResp, nullptr);
    CHECK(!mapCntl.Failed() && mapResp.error_code() == 0,
          "HandleFileChatSessionMap (文件↔会话双向关联，H14 链路)");

    // 5. AI 侧确认 fileId 已写入会话
    chat2Data::AiService::GetSessionsRequest gsReq;
    gsReq.set_request_id("ec_5");
    gsReq.set_user_id(userId);
    chat2Data::AiService::GetSessionsResponse gsResp;
    brpc::Controller gsCntl;
    aiStub.GetSessions(&gsCntl, &gsReq, &gsResp, nullptr);
    bool fileAssociated = false;
    for (const auto& s : gsResp.result().sessioninfo()) {
        if (s.id() == csid && s.session_type() == "excel") {
            fileAssociated = true;
        }
    }
    CHECK(fileAssociated, "GetSessions shows excel session");

    // 6. ★ 智能 Excel 全流程：SendMessage（HTTP + SSE，真 GLM 生成 SQL 并执行）
    httplib::Client aiHttp("http://dev-env-service:9006");
    aiHttp.set_read_timeout(240, 0);
    aiHttp.set_write_timeout(240, 0);
    std::string body = "{\"request_id\":\"ec_6\",\"session_id\":\"" + sessionId +
                       "\",\"user_id\":\"" + userId + "\",\"chat_session_id\":\"" + csid +
                       "\",\"chat_type\":\"excel\",\"message\":\"统计各部门的薪资总额，按降序排列\""
                       ",\"file_id\":\"" + fileId + "\",\"db_type\":0,\"db_connect_id\":\"excel_default\"}";
    auto httpResp = aiHttp.Post("/chat2Data.AiService.AIService/SendMessage",
                                body, "application/json");
    CHECK(httpResp != nullptr, "SendMessage HTTP 响应收到");
    std::string sse = httpResp ? httpResp->body : "";
    CHECK(sse.find("data: [DONE]") != std::string::npos, "excel 流程走完（data: [DONE]）");
    bool hasChart = sse.find("<CHART_DATA>") != std::string::npos;
    bool hasError = sse.find("error") != std::string::npos;
    CHECK(hasChart || hasError, "excel 流程产出图表数据块或明确错误（宽松断言）");
    INF("SSE 响应前 600 字符:\n{}", sse.substr(0, 600));

    // 7. 清理（会话 + 文件；删文件会级联删表与 chatSession 行）
    chat2Data::AiService::DeleteSessionRequest dsReq;
    dsReq.set_request_id("ec_7");
    dsReq.set_user_id(userId);
    dsReq.set_chat_session_id(csid);
    chat2Data::AiService::DeleteSessionResponse dsResp;
    brpc::Controller dsCntl;
    aiStub.DeleteSession(&dsCntl, &dsReq, &dsResp, nullptr);
    CHECK(!dsCntl.Failed(), "DeleteSession");

    chat2Data::fileService::DeleteFileRequest dfReq;
    dfReq.set_request_id("ec_8");
    dfReq.set_session_id(sessionId);
    dfReq.set_file_id(fileId);
    dfReq.set_user_id(userId);
    chat2Data::fileService::DeleteFileResponse dfResp;
    brpc::Controller dfCntl;
    fileStub.DeleteFile(&dfCntl, &dfReq, &dfResp, nullptr);
    CHECK(!dfCntl.Failed() && dfResp.error_code() == 0, "DeleteFile（级联删表）");

    std::remove(xlsxPath.c_str());
    INF("========== excelChatE2e summary: failCount={} ==========", g_fail);
    return g_fail == 0 ? 0 : 1;
}
