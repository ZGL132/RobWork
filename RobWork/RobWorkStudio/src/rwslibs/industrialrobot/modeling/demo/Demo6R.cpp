/**
 * @file   Demo6R.cpp
 * @brief  6 自由度机械臂 demo 项目生成器（sdurws_ird_demo6r）——创建
 *         demo 项目并把 generic-6r 模板草稿经 DraftService 持久化，供
 *         宿主打开后演示建模面板全链（owner 演示指令 2026-09-26）。
 *
 * 设计依据：
 *   - units/modeling.md §5.1/§5.2（generic-6r 模板——真实域路径产出
 *     T-MDL-1 默认参数表）、§8（草稿）；
 *   - units/project.md §5.1（createNew 新建空项目；DraftService 草稿
 *     保存——PM-04"保存与应用分离"：本工具只落草稿不产生修订，宿主里
 *     点"应用修改"才走命令管线产生修订——演示叙事的一部分）。
 *
 * 诚实边界：项目里尚无建模根对象修订（首版演示不注册命令处理器——
 * apply-robot-design 处理器注册通道属 project 契约增量，见 ui.md §16.7
 * 收口余量）；演示时"应用修改"会得到如实的拒绝反馈。
 *
 * 用法：sdurws_ird_demo6r [目标目录]（缺省 ./demo-6r-project；目录必须
 * 不存在或为空——createNew §5.1 前置契约）。
 *
 * 线程模型：单线程顺序执行；确定性：模板创建为纯函数（同输入同输出）。
 */

#include <sdurws/ird/core/Identity.hpp>            // core::ProjectId/BranchId/RevisionId/DiagnosticRecord
#include <sdurws/ird/modeling/Codec.hpp>           // RobotDesignCodec/kCurrentFormatVersion（域负载 canonical 编码）
#include <sdurws/ird/modeling/Template.hpp>        // RobotDesignTemplateFactory/kTemplateIdGeneric6R
#include <sdurws/ird/project/DraftService.hpp>     // DraftService/DraftDocument/SaveResult（草稿保存）
#include <sdurws/ird/project/PersistenceFormat.hpp>  // project::DraftDocument 完整定义
#include <sdurws/ird/project/ProjectStore.hpp>     // ProjectStoreFactory/createNew/commands/drafts/query

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// 全局作用域命名空间别名（main 不嵌套在 sdurws::ird 内——嵌套限定名
// 需要显式别名才可短写）。
namespace core = sdurws::ird::core;
namespace modeling = sdurws::ird::modeling;
namespace project = sdurws::ird::project;
namespace runtime = sdurws::ird::runtime;

int main(int argc, char* argv[])
{
    try {
        // 目标目录：命令行首参或缺省——createNew 前置要求"目录不存在或为空"。
        const std::string target =
            argc > 1 ? argv[1] : std::string("demo-6r-project");

        std::cout << "[demo6r] 创建 demo 项目: " << target << "\n";
        project::OpenStoreResult opened = project::ProjectStoreFactory::createNew(
            std::filesystem::path(target), std::string_view("6R Demo"));

        project::ProjectStore& store = *opened.store;
        const core::ProjectId projectId = store.projectId();

        // 权威分支表首条＝初始分支（createNew 播种；INV-M3 权威纪律）。
        const auto tips = store.query().branchTips();
        const core::BranchId branch =
            tips.empty() ? core::BranchId{} : tips.front().id;

        // generic-6r 模板草稿（真实域路径——RobotDesignTemplateFactory
        // §9.4.2 纯函数创建；Ground 安装预设＝MDL-22 缺省）。
        modeling::RobotDesignTemplateFactory factory;
        std::vector<core::DiagnosticRecord> diags;
        const modeling::TemplateOutcome outcome = factory.createDraft(
            modeling::TemplateId{modeling::kTemplateIdGeneric6R},
            runtime::InstallationPresetToken::Ground,
            std::string("demo"), diags);
        const modeling::ModelingWorkingSet ws = outcome.get();

        // 域负载＝根对象 canonical 字节（RobotDesignCodec——与宿主面板
        // buildDraftDocument 同源编码，恢复链路互通的保证）。payload 契约
        // 要求 UTF-8 文本——hex 铠装（与 ModelingUiModule 同算法，两端一致）。
        modeling::RobotDesignCodec codec;
        const auto encoded =
            codec.encode(modeling::ObjectVariant{ws.design},
                         modeling::kCurrentFormatVersion);
        const auto& bytes = encoded.get();
        std::string payload;
        payload.reserve(bytes.size() * 2);
        {
            static const char* kDigits = "0123456789abcdef";
            for (const std::uint8_t b : bytes) {
                payload.push_back(kDigits[b >> 4]);
                payload.push_back(kDigits[b & 0xF]);
            }
        }

        // 草稿文档（§4.4.5）：归属三元组取自本 store；基线修订留全零
        // （空项目无 tip——恢复后首次应用在提交期解析 tip，§6.2）。
        project::DraftDocument document;
        document.schemaVersion = 1;
        document.projectId = projectId;
        document.branchId = branch;
        document.moduleId = "modeling";
        document.payload = std::move(payload);

        const project::SaveResult save = store.drafts().save(document);
        std::cout << "[demo6r] 草稿保存: ok=" << (save.ok ? "1" : "0") << "\n";

        std::cout << "[demo6r] 完成——在宿主中打开本目录即可演示：\n"
                  << "  树呈现 6R 结构（基座安装/关节 J1~J6/连杆/工具/场景）\n"
                  << "  属性编辑（L-2 域裁决＋就地拒绝）\n"
                  << "  就绪条真实判定\n"
                  << "  应用修改（提示：未注册命令处理器时为如实拒绝——收口项）\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[demo6r] 失败: " << error.what() << "\n";
        return 1;
    }
}
