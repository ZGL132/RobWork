/**
 * @file   QueryPortImpl.hpp
 * @brief  查询端口实现（QueryPortImpl）——§5.2 IProjectQueryPort 契约的
 *         实现载体：视图组装/闭包约束/对象缓存消费/读写隔离（私有实现头，
 *         不出 include/——R-2 纪律；§3.4 布局"查询……实现"的落位）。
 *
 * 设计依据：
 *   - units/project.md §5.2（查询端口契约表——head/tryRevision/revision/
 *     currentMetadata/metadataAt/branchTips/branchHistory/tryObject/object/
 *     listDrafts/listRuns/runDir 的前置/后置/错误语义）、§4.5（闭包规则与
 *     INV-M3 权威纪律）、§4.6（对象读取惰性＋缓存＋首读校验；引用存在性）、
 *     §4.7（只读查询线程与生命周期契约——本类是其直接实现载体）、§6.3
 *     （hasUnresolvedPayload 语义）、§12 PRJ-T09 行（视图/闭包约束/缓存/
 *     隔离）；
 *   - 任务契约 tasks/foundation/PRJ-T09.json acceptance 1～4。
 *
 * 背景说明（实现形态——为什么薄、为什么依赖宿主）：
 *   本类**不持有任何项目状态**：修订/元数据的语义载体是宿主（存储上下文）
 *   持有的 RevisionIndex（会话索引——闭包外修订不注册，§7.3/PRJ-TX-9，
 *   "闭包外不可见"由此结构性成立）；对象读取直接消费 ObjectStore（LRU
 *   缓存/首读校验在其内部——§4.6"缓存互斥内部实现"）；权威元数据取自
 *   宿主的 m_authoritative（executeCommit 成功路径"自磁盘读回"更新的
 *   地面事实）。本类只做三件事：①生命周期门卫（Closed 拒绝）；②与提交
 *   的一致性互斥（writer 锁内取 HEAD/权威快照——挡住"已注册未切 HEAD"
 *   的提交中窗口，§4.5.1 结论④）；③视图组装（清单→RevisionView，含
 *   command.json 摘要/逆命令读取）。
 *
 * 一致性窗口与锁序（§9.8；高危信息——线程约束）：
 *   - 锁序遵守宿主约定：m_lifecycleMutex 先于 m_writerMutex，不反向；
 *   - head/tryRevision/revision/metadataAt/currentMetadata/branchTips/
 *     branchHistory 在 writer 锁内完成"权威/HEAD 锚点＋首级索引读"的
 *     快照，锁外完成视图组装——组装只读**不可变数据**（已注册清单/记录
 *     一经注册不可变——PA-2；revisions/<rev>/command.json 发布后就位
 *     只增不改——§4.1），锁外读安全且不放大 writer 锁持有时长；
 *   - tryObject/object/listDrafts/listRuns/runDir 不取 writer 锁：对象
 *     文件不可变（内容寻址只增——§4.6）、草稿/manifest 原子替换或原子
 *     发布（读到完整或不存在的文件——§5.4/§10.1 原语保证），与写并发
 *     天然安全；
 *   - 由此实现"查询端口全部方法线程安全（并发只读）"（§4.7）：多查询
 *     线程可并发进入，互斥仅覆盖与提交的一致性窗口，不承诺查询间并行
 *     加速（§9.8"调用方线程：查询端口并发只读"的线程安全语义）。
 *
 * 错误语义（错误二分，AGENTS §3/§5.0；完整契约见 QueryPort.hpp 各方法）：
 *   - 调用方错误 → std::invalid_argument fail-fast（revision/runDir 的
 *     全零身份、branchHistory 的未知分支——§5.2 表"未知分支抛"，封闭集
 *     不私扩稳定码）；
 *   - 状态错误 → StoreError(ContextClosed)（§4.7——仅 Closed 拒绝；
 *     Draining 排空期查询仍可用，PM-03 关闭对话框的呈现数据源）；
 *   - 数据侧错误 → StoreError(StoreCorrupt)（清单结构违约/metadataRef
 *     悬挂/HEAD 悬挂/command.json 缺失损坏/引用缺失——PM-02 读校验
 *     同语义）；环境残余（存在却不可读）按 ObjectStore/文件层的稳定码
 *     透传（AccessDenied 等），不吞不改码；
 *   - tryObject 的 noexcept 边界：一切异常（含 ContextClosed/损坏/全零
 *     身份）降级为 nullopt＋开发诊断 reportDev（不静默——损坏可见性经
 *     开发通道保留；强语义走 object()，§5.2 二分的 noexcept 面表达，
 *     实现口径登记于 units/project.md §12 v0.11）。
 *
 * P-PR-1 处置（acceptance 4）：视图携带的全部身份类型（RevisionId/
 *   BranchId/ObjectId/ContentVersion/RunId/TaskIdentity）来自 core 公共
 *   契约头（Identity.hpp/Digest.hpp——v0.1 基线），零 core 修改、零本地
 *   第二身份形态；core 冻结出 diff 后按影响面增量同步（单元卡 §15.3）。
 *
 * 生命周期与所有权：由 ProjectStoreImpl 构造体末尾创建、随宿主消亡
 *   （unique_ptr 成员，声明序在全部部件之后——析构先于部件，宿主引用
 *   在析构中不触碰部件，安全）；本类无可变自身状态（m_host/m_commandKnown
 *   构造后不变），多线程共享安全。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_QUERYPORTIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_QUERYPORTIMPL_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

/// 前向声明：宿主存储上下文（完整定义于 ProjectStoreImpl.hpp——本头被
/// QueryPortImpl.cpp 单点消费，_friend 访问收敛于实现文件）。
class ProjectStoreImpl;

/**
 * @brief §5.2 IProjectQueryPort 的实现（final——不再派生；消费者只面向
 *        公共抽象 IProjectQueryPort，本类型不出公共头——R-2）。
 *
 * 生命周期与所有权：仅由 ProjectStoreImpl 构造创建（unique_ptr 持有，
 *   经 ProjectStore::query() 以接口引用交付）；宿主引用非 owning，其
 *   生存期覆盖本对象（同一上下文体的成员，构造序保证）。
 */
class QueryPortImpl final : public IProjectQueryPort {
public:
    /**
     * @brief 命令类型判据注入点（hasUnresolvedPayload 判定；§5.2 增量
     *        落位说明 3）。
     *
     * 语义：commandType 属于当前会话已注册/可受理集合时返回 true。
     * 注册表（HandlerRegistry）归命令服务（PRJ-T10）——T10 落位时注入
     * 注册表查询；本阶段为空（注册表尚未装配），hasUnresolvedPayload
     * 按 §5.2 字段默认值语义置 false（"无法判定"≠"已知未知"——不虚报
     * unresolved，见 QueryPort.hpp 文件头口径）。
     *
     * 线程约束：判据函数可能在多查询线程并发调用——实现方（T10 注入的
     * 注册表查询）须线程安全；函数对象本身构造后不变。
     */
    using CommandKnownFn = std::function<bool(std::string_view commandType)>;

    /**
     * @brief 构造查询端口实现（仅宿主可达——构造于宿主装配末尾）。
     *
     * @param host     [in] 宿主存储上下文（非 owning 引用；提供部件
     *                 访问/锁/权威快照——生存期覆盖本对象）
     * @param knownFn  [in] 命令类型判据（可空＝注册表未装配——见
     *                 CommandKnownFn 注释）
     */
    QueryPortImpl(ProjectStoreImpl& host, CommandKnownFn knownFn);

    // ---- IProjectQueryPort（§5.2；逐方法契约见公共头注释） ----

    [[nodiscard]] RevisionView head() const override;
    [[nodiscard]] std::optional<RevisionView> tryRevision(
        core::RevisionId id) const override;
    [[nodiscard]] RevisionView revision(core::RevisionId id) const override;
    [[nodiscard]] ProjectMetadataView currentMetadata() const override;
    [[nodiscard]] std::optional<ProjectMetadataView> metadataAt(
        core::RevisionId id) const override;
    [[nodiscard]] std::vector<BranchTip> branchTips() const override;
    [[nodiscard]] std::vector<RevisionView> branchHistory(
        core::BranchId branch, std::uint32_t maxCount) const override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept override;
    [[nodiscard]] std::vector<std::uint8_t> object(
        core::ObjectId oid, core::ContentVersion cv) const override;
    [[nodiscard]] std::vector<DraftInfo> listDrafts(
        core::BranchId branch) const override;
    [[nodiscard]] std::vector<RunInfo> listRuns(
        core::RevisionId revision) const override;
    [[nodiscard]] std::filesystem::path runDir(core::RunId run) const override;

private:
    /**
     * @brief 生命周期门卫（§4.7）：宿主 Closed → ContextClosed。
     *
     * Draining（关闭排空中）不拒——查询是只读操作，PM-03 关闭对话框
     * 场景需要在排空期呈现项目状态（与写入口的 Draining 拒绝语义区分，
     * §5.1 表"context-closing 并入 ContextClosed"仅约束写路径）。
     *
     * @throws StoreError ContextClosed
     */
    void requireOpen() const;

    /// noexcept 版开放判定（tryObject 专用——拒绝态以 nullopt 表达）。
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief 视图组装：清单 → RevisionView（锁外执行——只读不可变数据）。
     *
     * 执行序：身份/分支/引用集直拷 → 在 objectRefs 中定位 metadataRef
     * 的完整引用形态（§4.4.2"必含"约束；缺失＝清单结构违约 →
     * StoreCorrupt——防御性，正常装载路径不可达）→ 读 command.json
     * （parse；缺失/损坏＝数据侧 StoreCorrupt）→ 摘要/逆命令/未知命令
     * 标志装配。
     *
     * @param manifest [in] 会话索引中的修订清单（已按闭包纪律过滤）
     * @return 修订视图值快照
     *
     * @throws StoreError StoreCorrupt（清单结构违约/command.json 缺失
     *         或损坏——数据侧）
     */
    [[nodiscard]] RevisionView assembleView(const RevisionManifest& manifest) const;

    /// 宿主存储上下文（部件/锁/权威快照的所有者；非 owning——同一
    /// 上下文体成员，生存期覆盖本对象）。
    ProjectStoreImpl& m_host;
    /// 命令类型判据（可空——注册表未装配阶段；构造后不变）。
    CommandKnownFn m_commandKnown;

    /**
     * @brief 开发诊断上报（私有静态——friend 地位访问宿主 sink 的唯一
     *        通道；自由函数无法访问宿主私有成员）。
     *
     * 背景：容错路径（tryObject 的 noexcept 边界、草稿/manifest 单条目
     * 损坏跳过）经本通道保留开发可见性——用户级稳定码产出权归打开/写
     * 路径（打开期恢复报告），查询期不重复产码（码表收编清单无"查询期
     * 损坏"码——不私造，CR-08）。sink 可空＝丢弃（§5.0 约定）；调用包
     * try（noexcept 调用方不可因诊断通道故障终止）。
     *
     * @param host    [in] 宿主上下文（读取其诊断 sink 注入位）
     * @param message [in] 开发诊断消息（"project/query" 通道）
     */
    static void reportDevQuietly(const ProjectStoreImpl& host,
                                 const std::string& message) noexcept;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_QUERYPORTIMPL_HPP
