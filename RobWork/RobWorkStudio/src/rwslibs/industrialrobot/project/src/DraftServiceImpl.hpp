/**
 * @file   DraftServiceImpl.hpp
 * @brief  草稿服务实现（DraftServiceImpl）——§5.4 DraftService 契约的
 *         实现载体：原子替换+.bak 轮换落盘/损坏恢复/汇总投影/放弃
 *         （私有实现头，不出 include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.4（save/tryLoad/summarize/discard/list 的前置/
 *     后置/权限契约表与落盘协议原文）、§8.1～§8.6（生命周期语义——本类
 *     是其直接实现载体：§8.1 单文件原子替换不走七步协议、§8.2 落盘失败
 *     返回错误＋诊断、§8.3 拒绝后草稿保留、§8.4 恢复/损坏诊断/.bak 优先
 *     ／.new 丢弃、§8.5 投影不解释模块内容、§8.6 只读拒绝保存与放弃）、
 *     §9.6～§9.8（写入口统一门卫/writer 互斥/在途票据/线程模型）、
 *     §4.1 drafts 行（目录命名与 .bak 保留上一版）、§4.4.5（归属三元组
 *     加载校验）；
 *   - 需求 PM-04（保存与应用分离）、PM-07（只读禁编辑的存储侧落实）、
 *     PM-08/PM-15（草稿恢复数据面）、NFR-COR-02（确定性投影）；
 *   - 任务契约 tasks/foundation/PRJ-T12.json acceptance 1～4。
 *
 * 背景说明（实现形态——为什么薄、为什么依赖宿主）：
 *   与 QueryPortImpl 同款：本类**不持有任何项目状态**。身份事实来自宿主
 *   （projectId 归属校验、权威元数据的分支 tip stale 判据）、互斥与门卫
 *   用宿主的锁（m_lifecycleMutex/m_writerMutex）、在途票据经宿主的
 *   acquireInFlight()（§9.7"DraftService（在途保存）"引用持有者）。磁盘
 *   操作经 AtomicFile 原语（writeThrough/publishNew/replaceFile——§7.2
 *   的持久性/原子可见性保证面），目录创建用 std::filesystem 非抛形态
 *   （目录创建不是原子替换协议的环节）。本类无自身可变状态，多线程
 *   共享安全（互斥在宿主内）。
 *
 * 门卫复用口径（与 ProjectStoreImpl::executeCommit 的关系）：
 *   写门卫三道（状态机/锁面/权威探测）在 executeCommit 内联实现——本类
 *   按同序同码复刻（拒绝码与用户级诊断逐字对齐：ContextClosed＋
 *   PRJ-WRITE-AUTHORITY-LOST／LockHeldByOther＋PRJ-LOCK-HELD／
 *   WriteRejected）。差异仅在错误出口：executeCommit 抛异常（命令路径
 *   由 CommandResult 捕获归类），本类写入返回值轨（§5.4 SaveResult/
 *   DiscardResult——ui 定时器安全，见公共头错误语义）。门卫收敛为共享
 *   内部方法的重构涉及已验收的 T08/T10 面，归后续任务（本文件头登记，
 *   不在本任务私改已验收语义）。
 *
 * 增量落位口径（DTB §5.4，登记于 units/project.md §12）：
 *   1. discard 额外清理 .new 残留（§5.4 表只点名 current/.bak）——放弃
 *      的完备语义：用户明确不要的数据不因崩溃残留复活。
 *   2. 投影 present 位＝磁盘事实投影（损坏文件条目 present=false）——
 *      阶段 B 域模块注册面就位后扩展"已注册无草稿"条目。
 *   3. save 对"分支不存在于权威表"不做校验（§5.4 前置仅"上下文
 *      Active"）——孤儿风险由调用方纪律与打开协议⑤步扫描兜底。
 *
 * 错误语义（错误二分——完整口径见公共头 DraftService.hpp）：
 *   - 调用方错误 → std::invalid_argument fail-fast（模块 token 白名单、
 *     文档 projectId 与上下文不符）；
 *   - 状态/环境错误 → 返回值轨（save/discard 的 ok/error）＋用户级诊断
 *     （门卫类有收编码）或开发诊断（磁盘 I/O 类无精确收编码——CR-08
 *     不私造）；
 *   - 数据侧错误 → tryLoad 的 try 轨（诊断＋nullopt＋.bak 回退）；
 *     读轨的 Closed 拒绝 → 抛 StoreError(ContextClosed)。
 *
 * P-PR-1 处置（acceptance 4）：payload 只经 Codec dump/parse 往返（UTF-8
 *   健全性检查之外零解释——CR-02/D-10）；core 契约消费以 v0.1 为基线，
 *   冻结 diff 后增量同步，不私改 core。
 *
 * 线程模型（§9.8）：写方法任意线程进入（门卫＋writer 互斥内部串行）；
 *   读方法并发只读安全（权威快照的一致性窗口在 writer 锁内完成）。锁序
 *   遵守宿主约定：m_lifecycleMutex 先于 m_writerMutex，不反向。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_DRAFTSERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_DRAFTSERVICEIMPL_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/project/DraftService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>

#include "win32/AtomicFile.hpp"

namespace sdurws::ird::project {

// 前向声明：宿主存储上下文（完整定义于 ProjectStoreImpl.hpp——friend
// 访问收敛，本头不 include 宿主私有头，防环）。
class ProjectStoreImpl;

/**
 * @brief §5.4 DraftService 契约的实现（final——不再派生；消费者只面向
 *        公共抽象，本类型不出公共头——R-2）。
 *
 * 生命周期与所有权：由 ProjectStoreImpl 构造体末尾创建（宿主引用构造
 *   完成后装配）、unique_ptr 持有、经 ProjectStore::drafts() 以接口引用
 *   交付；与上下文同生命周期（宿主引用非 owning——同一上下文体成员，
 *   构造序保证宿主可用）。
 */
class DraftServiceImpl final : public DraftService {
public:
    /**
     * @brief 构造草稿服务实现（仅宿主可达——装配于宿主构造末尾）。
     *
     * @param host [in] 宿主存储上下文（非 owning 引用——提供身份事实/
     *             锁/在途票据/诊断 sink；生存期覆盖本对象）
     */
    explicit DraftServiceImpl(ProjectStoreImpl& host);

    // ---- DraftService 契约（§5.4；逐方法契约见公共头注释） ----

    [[nodiscard]] SaveResult save(const DraftDocument& doc) override;
    [[nodiscard]] std::optional<DraftDocument> tryLoad(
        core::BranchId branch, std::string_view moduleId,
        std::vector<core::DiagnosticRecord>& diags) const override;
    [[nodiscard]] DraftProjection summarize(core::BranchId branch) const override;
    [[nodiscard]] DiscardResult discard(core::BranchId branch,
                                        std::string_view moduleId) override;
    [[nodiscard]] std::vector<DraftInfo> list(core::BranchId branch) const override;

private:
    /**
     * @brief 模块 token 白名单校验（§4.1"注册的模块 token"的路径安全面
     *        ——PersistenceFormat.hpp 注释"注册表校验归 DraftService"）。
     *
     * 规则：1～64 个 ASCII 字母/数字/下划线/连字符。排除项的业务含义：
     * 路径分隔符与盘符（moduleId 直接拼磁盘路径——穿越防护）、点（防
     * ".." 穿越＋防与 .draft.json/.new/.bak 后缀体系命名歧义）、空串
     * （文件名退化为纯后缀）。
     *
     * @param moduleId [in] 待校验 token
     *
     * @throws std::invalid_argument 违反白名单（调用方契约违约——
     *         fail-fast，不静默净化）
     */
    static void validateModuleId(std::string_view moduleId);

    /**
     * @brief 写门卫的拒绝载体（返回值轨——checkWriteGuard 的结果形态）。
     *
     * 线程安全：纯值类型。
     */
    struct GuardFailure {
        StoreErrorCode code;  ///< 拒绝稳定码（§4.4.8 封闭集内）
        std::string detail;   ///< 开发诊断明细（"project/draft: ..."）
    };

    /**
     * @brief 写门卫三道（§9.6①②；与 executeCommit 同序同码——差异与
     *        复用口径见文件头）。
     *
     * 执行序：①状态机（lifecycle 锁内：非 Active → ContextClosed＋
     * PRJ-WRITE-AUTHORITY-LOST 用户级诊断）；②锁面（从未持锁＝只读
     * 上下文 → LockHeldByOther＋PRJ-LOCK-HELD）；③StoreLock 权威探测
     * （已释放/失权 → WriteRejected——诊断由 StoreLock 产出）。②③的
     * 用户级诊断同 executeCommit 装配点（diagrec 工厂）。
     *
     * @param action [in] 动作名（进入开发诊断 detail——"save"/"discard"）
     * @return 空＝门卫通过；非空＝拒绝（code/detail——调用方装入返回值
     *         轨的 StoreError）
     */
    [[nodiscard]] std::optional<GuardFailure> checkWriteGuard(
        std::string_view action) const;

    /**
     * @brief 草稿文件编址（§4.1 drafts 行命名规则的唯一推导）：
     *        drafts/<branch-id>/<module>.draft.json。
     *
     * @param branch   [in] 分支身份（toCanonical 作目录名——与打开协议
     *                 ⑤步孤儿扫描、查询端口 listDrafts 同一编址）
     * @param moduleId [in] 模块 token（调用方已过白名单）
     * @return 草稿文件完整路径
     */
    [[nodiscard]] std::filesystem::path draftFilePath(
        core::BranchId branch, std::string_view moduleId) const;

    /**
     * @brief 读轨开放判定（§4.7 同查询端口 requireOpen）：宿主 Closed →
     *        抛 StoreError(ContextClosed)；Draining 不拒（PM-03）。
     *
     * @throws StoreError ContextClosed（上下文已关闭）
     */
    void requireOpen() const;

    /**
     * @brief 开发诊断上报（宿主 sink 的 friend 通道；可空＝丢弃——
     *        §5.0 sink 约定；调用包 try，诊断通道故障不终止业务路径）。
     *
     * @param channel [in] 通道 token（"project/draft" 前缀约定）
     * @param message [in] 自由文本（路径/OS 错误码等明细）
     */
    void reportDev(const std::string& channel, const std::string& message) const noexcept;

    /**
     * @brief 用户级诊断上报（诊断＋diags 双通道——同 RecoveryReport
     *        口径：sink 即时上报＋追加进调用方向量，同源同序）。
     *
     * @param record [in] 已过 core 工厂校验的记录（diagrec 工厂产出）
     * @param diags  [out] 调用方诊断向量（追加语义）
     */
    void reportUser(const core::DiagnosticRecord& record,
                    std::vector<core::DiagnosticRecord>& diags) const;

    /// 宿主存储上下文（身份事实/锁/在途票据/sink 的所有者；非 owning
    /// ——同一上下文体成员，生存期覆盖本对象）。
    ProjectStoreImpl& m_host;
    /// 文件原语门面（默认绑定进程级共享 Win32 实现；无自身状态）。
    win32::AtomicFile m_file;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_DRAFTSERVICEIMPL_HPP
