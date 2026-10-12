/**
 * @file   SaveAsServiceImpl.hpp
 * @brief  另存为服务实现（SaveAsServiceImpl）——§5.8 ISaveAsService
 *         实体与 §8.8/§8.9 协议的实现载体：快照捕获→同卷临时位复制→
 *         闭包校验→换新 projectId 三处改写→发布→打开协议进入。私有
 *         实现头，不出 include/——R-2 纪律。
 *
 * 设计依据：
 *   - units/project.md §5.8（ISaveAsService 服务实体）、§8.8（另存为
 *     协议八步——本类 saveAs 是其唯一编排点）、§8.9（选择性复制与引
 *     用完整性——快照捕获的消费语义）、§8.7（打开协议服务侧——闭包
 *     校验与进入的协议出处）、§4.2（project.json 另存为整体重写）、
 *     D-16（全量复制＋三处改写）、D-17（快照一致视图——捕获机制）；
 *   - 需求 PM-05（另存为存储侧）、AT-13（失败不留半成品）、AT-20
 *     （复制后引用一致）；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 1～2。
 *
 * 背景说明（本类在单元内的位置——为什么不是存储上下文的第六端口）：
 *   §5.1 ProjectStore 的五端口访问器已随 PRJ-T14 全部挂载冻结；§5.8
 *   未给 ProjectStore 增加端口。另存为是**装配侧服务**（SolidifyService
 *   先例同款）：由 L5/测试经 createSaveAsService() 构造，取数走既有②
 *   查询端口、发布走既有 win32 原语（AtomicFile/publishNew——不绕过
 *   事务原语私写）、进入走既有工厂 open 协议——零存储上下文结构变更。
 *
 * 错误语义（错误二分，AGENTS §3；结果轨口径见 PackageService.hpp
 *   SaveAsResult 注释——实现口径登记）：
 *   - 调用方错误 → std::invalid_argument fail-fast（targetDir 空/已
 *     存在/父目录缺失）；
 *   - 状态与环境错误 → 返回值轨（ok=false＋error：源上下文已关闭＝
 *     ContextClosed、源存储损坏＝StoreCorrupt〔捕获/校验路径〕、磁盘/
 *     权限＝DiskFull/AccessDenied/WriteRejected〔复制/改写/发布路径，
 *     既有映射同表〕）；
 *   - 临时位清理失败 → 开发诊断（不掩盖第一手失败原因——PM-05 尽力
 *     义务口径）。
 *
 * 线程模型：m_opMutex 服务级操作互斥（并发 saveAs 串行——快照捕获的
 *   读轨一致性语义收敛点；与源上下文命令提交可并存，D-17 保证副本
 *   自洽）。
 *
 * 所有权与生命周期：createSaveAsService 经 unique_ptr 交付装配方；
 *   source/fileOps/eventBus/sink 全部**非 owning**——生存期由装配方
 *   保证覆盖本对象。fileOps 为空＝生产默认（真实 Win32FileOps 共享
 *   实例）；非空＝测试注入面（AtomicFile 构造注入先例——D-10 形态，
 *   故障注入用例经此驱动复制/发布失败路径）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_SAVEASSERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_SAVEASSERVICEIMPL_HPP

#include <mutex>

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "StoreClone.hpp"

namespace sdurws::ird::project {

/**
 * @brief §5.8 ISaveAsService 契约的实现（final——不再派生；消费者只
 *        面向公共抽象与工厂，实现细节不外泄——R-2）。
 */
class SaveAsServiceImpl final : public ISaveAsService {
public:
    /**
     * @brief 构造另存为服务（工厂与测试可达）。
     *
     * @param source      [in] 源存储上下文（非 owning——②端口取数面）
     * @param fileOps     [in] 文件操作通道（非 owning；可空＝生产默认
     *                    ——真实 Win32FileOps；测试注入 fake 驱动失败
     *                    路径——D-10 形态）
     * @param eventBus    [in] 事件总线（非 owning，可空＝新上下文跳过
     *                    事件——OpenStoreRequest.eventBus 同口径）
     * @param diagnostics [in] 诊断 sink（非 owning，可空＝丢弃）
     */
    SaveAsServiceImpl(ProjectStore& source,
                      win32::IFileOps* fileOps,
                      core::IDomainEventBus* eventBus,
                      IDiagnosticsSink* diagnostics);

    /**
     * @brief 执行另存为（§8.8/§8.9 协议编排——执行序与错误语义见公共
     *        头 ISaveAsService::saveAs 与 SaveAsResult 注释）。
     *
     * 执行序（各步失败即转入清理：临时位整树删除、目标零写入）：
     *   1. 前置校验（invalid_argument fail-fast / Closed 状态入结果轨）；
     *   2. 快照捕获（captureSnapshot——失败＝源存储损坏，零磁盘副作用）；
     *   3. 建同卷临时位（".saveas-<8hex>.tmp"）；
     *   4. 复制快照清单（write-through 闸门）；
     *   5. 闭包校验（对副本 open(ReadOnly)——PM-02②③⑤全量）；
     *   6. 换新 projectId＋三处改写（rewriteProjectIdForCopy）；
     *   7. 发布（publishDirectory——目录形态 publishNew）；
     *   8. 打开进入（factory.open Writable——降级只读按实际结果）。
     *
     * @param options [in] 另存为选项
     * @return 另存为结果
     *
     * @throws std::invalid_argument 调用方契约违约（见公共头 @throws）
     */
    [[nodiscard]] SaveAsResult saveAs(const SaveAsOptions& options) override;

private:
    ProjectStore& m_source;              ///< 源上下文（非 owning）
    win32::IFileOps* m_ops;              ///< 文件操作通道（非 owning；恒非空）
    core::IDomainEventBus* m_eventBus;   ///< 事件总线（非 owning，可空）
    IDiagnosticsSink* m_sink;            ///< 诊断 sink（非 owning，可空）
    std::mutex m_opMutex;                ///< 服务级操作互斥
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_SAVEASSERVICEIMPL_HPP
