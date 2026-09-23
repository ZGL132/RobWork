/**
 * @file   DiagnosticsAssembly.cpp
 * @brief  装配层诊断栈的一步式装配实现——harness 与宿主插件共用的装配
 *         序列（语义自 UI-T15 HarnessMain 内联版逐行搬移，行为一致）。
 *
 * 设计依据：见头文件注释（diagnostics.md §4.5/§6.2/§7.3/§7.7/§9.2 与
 * 任务契约 UI-T15/UI-T16）。
 */

#include "DiagnosticsAssembly.hpp"

#include <stdexcept>
#include <system_error>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // StableCodeRegistry/registerBuiltinCodes（码表装配）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>     // ui::uiDiagnosticCodeDescriptors（ui 九码描述符供体——§3.5）

namespace sdurws {
namespace ird {
namespace ui {
namespace app {

void assembleDiagnostics(const std::filesystem::path& devlogDir, DiagnosticsStack& stack)
{
    namespace fs = std::filesystem;

    // ① 日志管线：用户级/开发级双文件（dev-diagnostics.log 是 Dev 码唯一
    //    出线——§6.2）。目录不存在则创建（开发工具的自我服务）。
    std::error_code ec;
    fs::create_directories(devlogDir, ec);
    stack.pipeline = std::make_shared<diagnostics::LoggingPipeline>(
        stack.clock, stack.fileOps);
    diagnostics::LogSinkConfig logConfig;
    logConfig.enabled = true;
    logConfig.directory = devlogDir;
    stack.pipeline->configure(logConfig);

    // ② 稳定码表：内置 87 码全量收编（§4.6——各单元 PRJ-*/EX-*/… 的码值
    //    权威）＋ui 侧描述符供体（IWorkbenchShell::uiDiagnosticCodeDescriptors
    //    ——ui.md §3.5 九码），注册全部完成后 seal（装配期单线程约定）。
    stack.registry = std::make_shared<diagnostics::StableCodeRegistry>();
    diagnostics::registerBuiltinCodes(*stack.registry);
    for (const diagnostics::CodeDescriptor& descriptor :
         ui::uiDiagnosticCodeDescriptors()) {
        stack.registry->registerCode(descriptor);
    }
    stack.registry->seal();

    // ③ 诊断工厂：绑定码表＋时钟后 seal（两段式装配——§9.2 运行期拒绝
    //    再登记的次序保证）。
    stack.factory = std::make_shared<diagnostics::DiagnosticsFactory>(
        *stack.registry, stack.clock);
    stack.factory->seal();

    // ④ 脱敏服务（默认策略——开发期全量规则即可）；目录导出面挂接脱敏
    //    双保险（§7.7"输出前强制再过一遍脱敏"）。failureSink＝日志管线
    //    （非 owning——栈对象填充后不移动，地址稳定）。
    stack.redaction = std::make_shared<diagnostics::RedactionService>(
        diagnostics::RedactionPolicy{}, stack.pipeline.get());
    stack.catalog = std::make_shared<diagnostics::DiagCatalog>();
    stack.catalog->attachRedactionService(stack.redaction);
}

}  // namespace app
}  // namespace ui
}  // namespace ird
}  // namespace sdurws
