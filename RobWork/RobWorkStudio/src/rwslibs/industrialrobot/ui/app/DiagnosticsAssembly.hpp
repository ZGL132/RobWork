/**
 * @file   DiagnosticsAssembly.hpp
 * @brief  装配层诊断栈的一步式装配（日志管线＋稳定码表＋诊断工厂＋脱敏
 *         ＋会话级目录）——harness（sdurws_ird_ui_app）与宿主插件
 *         （sdurws_ird_ui_plugin）共用的装配序列片段。
 *
 * 设计依据：
 *   - units/ui.md §10.1（ShellWiring 注入包的 diagnostics 成员来源）、§10.5
 *     （UiSessionControllerDeps 诊断面）；diagnostics.md §4.5/§9.2（码表
 *     装配序：registerCode 全部完成后 seal；DiagnosticsFactory 两段式）、
 *     §6.2（Dev 码唯一出线＝日志管线）、§7.7（目录导出挂接脱敏双保险）、
 *     §7.3（日志管线配置）；
 *   - 任务契约 tasks/foundation/UI-T15.json（装配序列的原始登记面——
 *     HarnessMain 装配步 ①）与 tasks/foundation/UI-T16.json（"插件复用
 *     harness PortAdapters 适配形态"的装配面复用授权——O-38 裁决②）。
 *
 * 背景说明（为什么抽成本文件）：
 *   UI-T16 起存在两个装配层消费者：顶层窗口宿主（harness）与嵌入式 Dock
 *   宿主（插件）。两者的诊断栈装配序列逐行相同（同码表、同管线、同脱敏
 *   双保险）——抽成单一函数供双方调用，杜绝两份装配序列漂移（单一口径，
 *   NFR-MNT-03 的装配侧延伸）。语义与 UI-T15 的 HarnessMain 内联版逐行
 *   一致（harness 行为回归零变化）。
 *
 * 线程约束：装配在应用主线程执行（QApplication 之后、任何 Widget 消费者
 *   之前——诊断栈先于壳/会话控制器的装配序）。
 */

#ifndef SDURWS_IRD_UI_APP_DIAGNOSTICS_ASSEMBLY_HPP
#define SDURWS_IRD_UI_APP_DIAGNOSTICS_ASSEMBLY_HPP

#include <filesystem>
#include <memory>

#include <sdurws/ird/diagnostics/Catalog.hpp>     // diagnostics::DiagCatalog/IDiagnosticSink/IDevLogSink
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::StableCodeRegistry（码表——头内成员所需）
#include <sdurws/ird/diagnostics/Factory.hpp>     // diagnostics::DiagnosticsFactory
#include <sdurws/ird/diagnostics/Logging.hpp>     // diagnostics::LoggingPipeline/SystemClock/FileLogFileOps
#include <sdurws/ird/diagnostics/Redaction.hpp>   // diagnostics::RedactionService

namespace sdurws {
namespace ird {
namespace ui {
namespace app {

// =====================================================================
// 诊断栈（装配产物集合——所有权在调用方作用域，壳/控制器只持共享引用）
// =====================================================================

/**
 * @brief 装配层诊断栈（assembleDiagnostics 的产物集合）。
 *
 * 注意（出参稳定性契约）：栈对象经出参填充、填充后不再移动——
 * RedactionService 的 failureSink 裸指针与 LoggingPipeline 的地址绑定依赖
 * 这一稳定性（调用方以栈对象/稳定存储持有，不得按值传递或搬移）。
 */
struct DiagnosticsStack {
    diagnostics::SystemClock clock;                 ///< 产品时钟（时间戳/节流来源）
    diagnostics::FileLogFileOps fileOps;            ///< 文件接缝（开-写-关单行落盘）
    std::shared_ptr<diagnostics::LoggingPipeline> pipeline;    ///< 日志管线（ILogger＋IDevLogSink）
    std::shared_ptr<diagnostics::StableCodeRegistry> registry; ///< 稳定码表（87 码全量收编＋ui 码）
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;  ///< 诊断工厂（create 唯一入口）
    std::shared_ptr<diagnostics::RedactionService> redaction;  ///< 脱敏服务（NFR-SEC-07）
    std::shared_ptr<diagnostics::DiagCatalog> catalog;         ///< 会话级诊断目录（IDiagnosticSink）
};

/**
 * @brief 装配诊断栈：日志管线＋码表＋工厂＋目录＋脱敏（diagnostics
 *        §4.5/§7.3/§9.2——UI-T15 装配序列的单一权威实现）。
 *
 * 装配序（每步依赖上一步产物）：①日志管线（Dev 码唯一出线）→ ②稳定码表
 * （内置 87 码全量收编＋ui 九码描述符供体，注册全部完成后 seal）→ ③诊断
 * 工厂（绑定码表＋时钟后 seal——两段式）→ ④脱敏服务＋目录（目录导出面
 * 挂接脱敏双保险 §7.7；failureSink＝日志管线，非 owning——栈对象不搬移）。
 *
 * @param devlogDir [in] 开发日志目录（不存在则创建——开发期通道的自我服务；
 *                  用户级/开发级双文件落此目录，dev-diagnostics.log 为 Dev
 *                  码唯一出线）
 * @param stack     [out] 装配产物（调用方持有——见 DiagnosticsStack 稳定性契约）
 *
 * @throws diagnostics 装配类异常（码描述符校验失败等——装配错误 fail-fast，
 *         由调用方转换为可观测失败，禁止静默吞错）
 */
void assembleDiagnostics(const std::filesystem::path& devlogDir, DiagnosticsStack& stack);

}  // namespace app
}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_APP_DIAGNOSTICS_ASSEMBLY_HPP
