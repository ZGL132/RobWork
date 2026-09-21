/**
 * @file   Section12RegistryTest.cpp
 * @brief  UI-T14 §12.3 用例清单逐例注册审计（契约 acceptance 1 的具名
 *         自证面）：验收承接的十一个用例族（WB/SES/DRF/CMD/HKY/STG/TSK/
 *         PLG/DIA/PERF/LCY）全部用例编号＋执行限制 UI-EXEC-1 必须在 ui
 *         单元的测试源码中有具名落位（模型测试/契约测试/GUI 测试三目标
 *         任一），缺一即失败并列出缺失清单。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T14.json acceptance 1（"§12.3 用例
 *     清单逐例注册（UI-WB/SES/DRF/CMD/HKY/STG/TSK/PLG/DIA/PERF/LCY
 *     全族）……登记执行限制 UI-EXEC-1"）；
 *   - units/ui.md §12.3（用例清单——编号规则 `UI-<域>-<序号>`；每例给
 *     出需求/AT 依据）、§12.4（每例绑定 TestRecord；GUI 环境不满足记
 *     envUnavailable 不得绿灯）、§13 UI-T14 行（ctest ird/ird_gui 注册；
 *     ird-test-report.json 留痕）；
 *   - 先例：ui/test/BuildRedLineTest.cpp（对源码树的文本级扫描用例形态
 *     ——以单元源码为数据源的独立复核双保险设计）。
 *
 * 为什么扫描源码文本而不是枚举 ctest：用例编号的"落位"载体是各具名
 * TEST 的命名与追溯注释（ird-test-report.json 的 TestRecord 由 gtest
 * 用例聚合产出，追溯字段经 IRD_TEST_INFO 写入）——扫描测试源码树等同
 * 于对"编号→具名用例"绑定关系做存在性审计；ctest 标签注册（ird/
 * ird_gui）已由 LinkageContractTest.ThreeTestTargetsAndLabels_UI_CTR
 * 钉住，本用例不重复。
 *
 * 匹配口径：语料归一化（下划线→连字符）后匹配 `UI-<域>-<序号>` 全字
 * 位串——gtest 用例名（UI_TSK_1 形）与追溯注释（UI-TSK-1 形）两种绑
 * 定书写都计入。本文件自身排除在语料外（其清单本身就是全部编号，自
 * 匹配会让审计恒真）。
 *
 * 家族范围口径（acceptance 原文）：逐例审计承接族＝WB/SES/DRF/CMD/
 * HKY/STG/TSK/PLG/DIA/PERF/LCY 十一族全部用例＋UI-EXEC-1 执行限制登
 * 记。§12.3 中随 UI-T05/T06/T07/T08/T10 增行的 V3D/PALETTE/POL/FRM/
 * HELP 家族由各已合入任务的测试文件承载（其行内"同源互证"观测点即
 * 指向对应文件），不属本契约承接族清单。
 *
 * 线程模型：单线程；只读源码树（IRD_UI_UNIT_ROOT 注入路径）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// ui 单元树根（industrialrobot 目录——IRD_UI_UNIT_ROOT 注入；规范化
/// 消除尾部 ".." 段——LinkageContractTest 同款口径）。
const std::filesystem::path& unitRoot()
{
    static const std::filesystem::path dir =
        std::filesystem::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    return dir;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 语料归一化：下划线→连字符（gtest 用例名 UI_TSK_1 与注释 UI-TSK-1
/// 两种绑定书写统一为可匹配形态；其余字符不动——编号词表本身全大写
/// 字母＋连字符＋数字）。
std::string normalize(std::string text)
{
    std::replace(text.begin(), text.end(), '_', '-');
    return text;
}

/// §12.3 承接族全部用例编号（验收原文"全族"清单——units/ui.md §12.3
/// 表逐行核对；行序＝卡表行序）。
const std::vector<std::string>& requiredCaseIds()
{
    static const std::vector<std::string> ids{
        // WB 族（五区布局——UX-09）
        "UI-WB-1", "UI-WB-2", "UI-WB-3",
        // SES 族（会话生命周期——PM-07/PM-03/PM-10）
        "UI-SES-1", "UI-SES-2", "UI-SES-3", "UI-SES-4", "UI-SES-5",
        "UI-SES-6", "UI-SES-7",
        // DRF 族（草稿交互——PM-04/PM-08）
        "UI-DRF-1", "UI-DRF-2", "UI-DRF-3", "UI-DRF-4",
        // CMD 族（命令注册与确认——SA-16/SA-15）
        "UI-CMD-1", "UI-CMD-2", "UI-CMD-3", "UI-CMD-4", "UI-CMD-5",
        "UI-CMD-6", "UI-CMD-7",
        // HKY 族（快捷键——UX-13/SA-16）
        "UI-HKY-1", "UI-HKY-2", "UI-HKY-3",
        // STG 族（阶段投影与七态映射——UX-12/UX-10/UX-06）
        "UI-STG-1", "UI-STG-2", "UI-STG-3",
        // TSK 族（任务呈现——TASK-01~03）
        "UI-TSK-1", "UI-TSK-2", "UI-TSK-3", "UI-TSK-4", "UI-TSK-5",
        // PLG 族（插件装配——SA-01/UX-14）
        "UI-PLG-1", "UI-PLG-2",
        // DIA 族（诊断呈现——UX-03/NFR-SEC-07）
        "UI-DIA-1",
        // PERF 族（UI 线程纪律——NFR-PERF-01）
        "UI-PERF-1",
        // LCY 族（关闭后迟到写——SA-17）
        "UI-LCY-1",
        // 执行限制登记（§12.3 表尾：队列位置无对外接口——只验证文案与
        // 诊断呈现不验证数值；acceptance 1 明文要求登记）
        "UI-EXEC-1",
    };
    return ids;
}

/**
 * §12.3 逐例注册审计：承接族每个用例编号必须在三个测试目标的源码
 * （ui/test、ui/contract_test、ui/gui_test）中有落位；缺失编号在断言
 * 消息中全量列出（一次跑完看到全部缺口——不带病逐轮补）。
 */
TEST(Section12Registry, AllAcceptanceFamilyCasesRegistered_UI_T14)
{
    IRD_TEST_INFO("UX-03", {"UX-09", "UX-10", "UX-13"}, std::nullopt);

    // 语料＝三测试目标源码树全部 .cpp 拼接（排除本文件——见文件头
    // "自匹配会让审计恒真"说明）。
    std::string corpus;
    const std::filesystem::path unitDir = unitRoot() / "ui";
    for (const auto* sub : {"test", "contract_test", "gui_test"}) {
        std::error_code ec;
        for (const auto& entry :
             std::filesystem::directory_iterator(unitDir / sub, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().filename() == "Section12RegistryTest.cpp") {
                continue;   // 自排除
            }
            if (entry.path().extension() != ".cpp") {
                continue;
            }
            corpus += readFile(entry.path());
            corpus += '\n';
        }
        ASSERT_FALSE(ec) << "遍历测试源码目录失败: " << (unitDir / sub).string()
                         << "（" << ec.message() << "）";
    }
    ASSERT_FALSE(corpus.empty()) << "测试源码语料为空（扫描根错误？）";
    const std::string normalized = normalize(corpus);

    // 逐编号存在性核对；缺失全量收集（一次暴露全部缺口）。
    std::vector<std::string> missing;
    for (const auto& id : requiredCaseIds()) {
        if (normalized.find(id) == std::string::npos) {
            missing.push_back(id);
        }
    }
    EXPECT_TRUE(missing.empty())
        << "§12.3 承接族用例未在 ui 测试源码中具名落位（缺 " << missing.size()
        << " 例）:";
    for (const auto& id : missing) {
        ADD_FAILURE() << "  缺失用例编号: " << id;
    }
}

}  // namespace
