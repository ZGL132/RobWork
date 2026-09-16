/**
 * @file   DraftService.hpp
 * @brief  草稿服务端口（DraftService，⑤号服务）——未应用草稿的落盘/恢复/
 *         投影/放弃的唯一入口（PRJ-T12 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.4（草稿服务接口原文——SaveResult/DraftService/
 *     DraftProjectionItem 与五方法签名、save/tryLoad/discard 契约表）、
 *     §8.1～§8.6（草稿与项目生命周期：数据结构区别、定时落盘与手动保存、
 *     应用与 StaleRevisionRejected、恢复/损坏诊断/旧草稿保留、多模块汇总
 *     投影、放弃/只读打开/项目切换）、§4.1 drafts 行（drafts/<branch-id>/
 *     <module>.draft.json 命名与"覆盖写（原子替换）＋.bak 保留上一版"）、
 *     §4.4.5（DraftDocument 磁盘格式——归属三元组加载校验）、§9.6～§9.8
 *     （写入口统一门卫/writer 互斥/线程模型）、§9.7（DraftService 在途
 *     保存＝排空引用持有者之一）；
 *   - 需求 PM-04（保存与应用分离——草稿落盘不产生修订；应用＝恰好一个
 *     新修订；草稿基线过期拒绝并保留草稿）、PM-07（只读打开禁编辑——
 *     草稿不可保存/放弃）、PM-08/PM-15（草稿恢复数据面；横幅呈现归 ui）、
 *     PM-03（项目切换排空——在途草稿落盘完成后释放）、NFR-COR-02（同
 *     磁盘状态必得同投影——确定性排序）；
 *   - 任务契约 tasks/foundation/PRJ-T12.json acceptance 1～4（PRJ-TX-6
 *     定时落盘→破坏→重开恢复用例、StaleRevisionRejected 冲突路径＋多模块
 *     投影、生命周期边界、P-PR-1 透传处置）。
 *
 * 背景说明（保存与应用为什么必须分离——PM-04 的结构含义）：
 *   编辑中的模型状态不可信（可能非法、可能半途放弃），不能让"按下保存"
 *   污染不可变修订历史（PA-2）。因此草稿落盘只是 drafts/ 目录内的单文件
 *   原子替换（不走七步事务、不产生修订对象），"应用"才经命令端口提交并
 *   恰好产生一个新修订。本服务只承载"落盘"半边；"应用"半边＝调用方（ui/
 *   域组装）以草稿负载构造命令经 ProjectStore::commands() 提交
 *   （expectedRevision=草稿基线——§8.3），基线过期由命令端口的并发校验
 *   拒绝（stale-revision-rejected），拒绝后草稿文件原样保留。
 *
 * 落盘协议（§5.4 原文序列——单文件原子替换＋旧版轮换，不走七步事务）：
 *   写 <module>.draft.json.new → flush（持久性闸门）→ 旧 current 改名为
 *   .bak（覆盖旧 .bak——.bak 始终是上一版）→ .new 原子就位为 current →
 *   任一步失败清理 .new（绝不留下半写的新文件）。崩溃现场三类残留的处置
 *   归属：.new＝保存崩溃现场，tryLoad 触达时丢弃（§8.4"丢弃 .new、保留
 *   current/.bak 并报告"）；.bak＝上一版，current 损坏时优先恢复源；
 *   current/.bak 的发现与恢复诊断经本服务 tryLoad 承载（打开协议⑤步的
 *   孤儿扫描在工厂侧只报告不处置——PRJ-T08 口径）。
 *
 * 增量落位说明（DTB §5.4 口径，三处）：
 *   1. `DiscardResult`：§5.4 代码块中 discard 返回 SaveResult，但 §3.1
 *      组成表行与任务契约 acceptance 1 均点名 "SaveResult/DiscardResult"
 *      两个类型——放弃的结果面需要承载 SaveResult 表达不了的删除明细
 *      （current/.bak 各自是否实际删除——幂等放弃"本来就没有"时二者皆
 *      false 且 ok=true）。按双锚点落位 DiscardResult（ok/error/file 语义
 *      与 SaveResult 对齐＋两个明细位）；§5.4 签名的该处偏差随本任务在
 *      units/project.md §12 实现口径登记。
 *   2. `DraftProjection`：§5.4 只给元素类型 DraftProjectionItem，未给容器
 *      形态。落位为 {branch, items}（items 按 moduleId 字典序——NFR-COR-02
 *      同状态同输出）。
 *   3. `present` 位的阶段 A 语义：§8.5"按分支列出各模块"的"模块全集"在
 *      阶段 A 无域注册面可枚举（域模块描述符归阶段 B——§8.5 末句"各域
 *      摘要（域提供模块级描述符）"）。落位为**磁盘事实投影**：drafts/
 *      <branch>/ 下每个 <module>.draft.json 一条目——可解析＝present=true
 *      ＋元数据齐备；解析失败（损坏）＝present=false＋空元数据（投影面
 *      保守不虚报可用草稿，与查询端口 listDrafts 跳过口径同源，损坏经
 *      开发诊断保留可见性）。阶段 B 域注册面就位后扩展"已注册但无草稿"
 *      条目（present=false——届时 ui 的两源拼接不变：本投影＝磁盘半源，
 *      域描述符＝注册半源，§8.5 原文口径）。
 *
 * 错误语义（错误二分，AGENTS §3/单元卡 §5.0）：
 *   - 调用方错误 → std::invalid_argument fail-fast：moduleId 为空/含路径
 *     不安全字符（moduleId 直接参与磁盘路径拼装——§4.1 命名规则"注册的
 *     模块 token"，白名单校验归本服务——PersistenceFormat.hpp 注释的
 *     "注册表校验归 DraftService"）；save 的文档归属与上下文不符
 *     （projectId 指向别的项目——装配错乱，落盘即制造跨项目脏数据）。
 *   - 状态/环境错误 → **返回值轨**（save/discard）：SaveResult/
 *     DiscardResult.ok=false＋error 携带 StoreError。草稿保存的典型调用方
 *     是 ui 自动保存定时器（§8.2——定时器归 ui，60 s 默认周期 PM-04-S1
 *     归 R2/WP-04-T20，本服务不实现任何定时器），异常轨会打断定时器链；
 *     门卫拒绝（只读/关闭/失权）与磁盘失败一律进返回值，诊断照发。
 *     读轨（tryLoad/summarize/list）与查询端口同款：上下文 Closed 后抛
 *     StoreError(ContextClosed)；Draining 排空期仍可用（PM-03 关闭对话
 *     框需呈现草稿恢复态）。
 *   - 数据侧错误 → tryLoad 的 try 轨：单文件失败（解析/归属校验）以
 *     诊断＋nullopt 表达并回退 .bak（§8.4），不抛——损坏草稿不是调用方
 *     错误，也不是不可恢复的环境错误，是恢复流程的常规输入。
 *
 * 诊断产出（P-PR-6 链路——码值限于 diagnostics.md §4.6 收编清单，CR-08
 * 不私造）：门卫失败→PRJ-LOCK-HELD（只读上下文）/PRJ-WRITE-AUTHORITY-LOST
 * （关闭/失权），与命令端口同码同装配点（diagrec 工厂）；草稿损坏→
 * PRJ-RECOVERY-ORPHAN-DRAFT（diagnostics.md §8.2 映射行"draft-corrupt →
 * PRJ-RECOVERY-ORPHAN-DRAFT，恢复场景/Info，附加上下文＝模块/分支"）；
 * 落盘 I/O 失败无精确收编码（磁盘满/权限不是"失权"——不乱映射），经
 * 开发诊断通道 reportDev 上报，错误明细由 SaveResult.error 机器可读承载。
 *
 * 线程模型（§9.8）：save/discard 可从任意线程进入（写门卫＋writer 互斥
 * 内部串行——"submit/archive/save 可从任意线程进入（内部转串行）"原文）；
 * tryLoad/summarize/list 并发只读安全（草稿文件原子替换——读到完整或不
 * 存在的文件；与保存并发天然安全）。本接口方法自身无共享可变状态，互斥
 * 由宿主上下文的锁提供（锁序：lifecycle 先于 writer——§9.8 约定）。
 *
 * 生命周期与所有权：实现实例由存储上下文持有（ProjectStore::drafts() 返回
 * 引用与上下文同生命周期——端口无独立生命周期）；上下文 Closed 后写入口
 * 拒绝（返回值轨）、读轨抛 ContextClosed；上下文析构后引用不可再用（常规
 * C++ 生存期纪律）。save/discard 执行期间持宿主在途票据（§9.7——
 * requestClose 的排空等待覆盖在途草稿落盘，"末次草稿 flush 确认"）。
 *
 * P-PR-1 处置（acceptance 4）：DraftDocument.payload 为域所有 canonical
 * 字节——本服务对其只做存储与透传（dump/parse 由 Codec 承载，其中 payload
 * 仅做 UTF-8 健全性检查，不做任何语义解析——CR-02/D-10），SourcedValue/
 * ValueProvenance 等域来源标记随 payload 原样进磁盘、原样读出。消费的
 * core 契约（BranchId/RevisionId/DiagnosticRecord）以 core.md v0.1 为基线
 * （与单元既有消费面同一口径）；core 冻结出 diff 后按影响面增量同步，
 * 不私改 core（knownPitfalls P-PR-1 的处置承诺）。
 */

#ifndef SDURWS_IRD_PROJECT_DRAFTSERVICE_HPP
#define SDURWS_IRD_PROJECT_DRAFTSERVICE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

// =====================================================================
// 结果类型（§5.4 原文形态＋增量落位，见文件头"增量落位说明 1/2"）
// =====================================================================

/**
 * @brief 保存/放弃的结果（§5.4 原文形态）——草稿服务的返回值错误轨。
 *
 * 背景说明（为什么不用异常）：save 的主调用方是 ui 自动保存定时器
 * （§8.2），返回值轨让"落盘失败"成为可轮询的普通状态（会话"未保存"
 * 标记的数据依据＝save 成功与否——§8.2 原文，ui 消费 ok 位），而不是
 * 需要捕获的异常。ok=false 时 error 必有值（StoreError 携带稳定码＋
 * 开发诊断 detail——§5.0 形态）；ok=true 时 error 为空、file＝当前草稿
 * 文件路径（save＝落盘目标；discard＝被放弃的草稿路径，本来就没有时
 * 为空）。
 *
 * 线程安全：纯值类型。
 */
struct SaveResult {
    /// true＝操作达成目标（草稿已落盘/草稿已不存在）；false＝失败（error
    /// 必有值）。PM-07 只读模式与关闭/失权态的拒绝同样经本位表达。
    bool ok = false;
    /// 失败时的稳定错误（ok=true 时为空——"成功不带残余错误"的显式
    /// 不变量，防调用方误读）。
    std::optional<StoreError> error;
    /// 当前草稿文件路径（save＝落盘目标；discard＝被放弃草稿；目标模块
    /// 无草稿的 discard＝空路径）。诊断与呈现用途——内容读取走 tryLoad，
    /// 不承诺调用方直读。
    std::filesystem::path file;
};

/**
 * @brief 放弃草稿的结果（增量落位，见文件头"增量落位说明 1"）——
 *        SaveResult 语义＋删除明细。
 *
 * 背景说明（明细位为什么存在）：放弃＝"该 (branchId, moduleId) 不再有
 * 草稿"的目标态（§5.4 表 discard 后置"删除当前与 .bak"）。目标态可能
 * 已经成立（本来就没有草稿——幂等成功，ok=true 且两位 false），也可能
 * 需要删两个文件（current＋上一版 .bak）；恢复横幅（PM-15）与调试诊断
 * 需要区分这些形态，故显式承载而非让调用方从 ok 猜测。
 *
 * 线程安全：纯值类型。
 */
struct DiscardResult {
    /// true＝放弃达成（current 与 .bak 均已不存在——含"本来就没有"的
    /// 幂等形态）；false＝失败（error 必有值；已删除部分如实反映在
    /// 明细位——半删状态由明细位可见，不回滚）。
    bool ok = false;
    /// 失败时的稳定错误（ok=true 时为空——同 SaveResult 不变量）。
    std::optional<StoreError> error;
    /// 被放弃的当前草稿路径（本来就没有时为空路径）。
    std::filesystem::path file;
    /// 本次调用是否实际删除了当前草稿文件（<module>.draft.json）。
    bool removedCurrent = false;
    /// 本次调用是否实际删除了上一版草稿（<module>.draft.json.bak）。
    bool removedBackup = false;
};

// =====================================================================
// 汇总投影（§5.4 DraftProjectionItem 原文形态＋§8.5 语义）
// =====================================================================

/**
 * @brief 多模块草稿汇总投影条目（§5.4 原文形态）——一个模块在该分支上
 *        的草稿事实。
 *
 * 背景说明（§8.5 的消费方式）：会话级"未应用修改"标记（标题 `*`，PM-11）
 * 由 ui 基于投影中 present 项计算；"跨模块草稿汇总视图"（PM-04）＝本
 * 投影＋各域摘要（域提供模块级描述符——project 不解释模块内容，CR-02/
 * D-10 对投影同样成立：本条目只透传磁盘元数据，绝不解读 payload）。
 *
 * 线程安全：纯值类型。
 */
struct DraftProjectionItem {
    /// 模块 token（<module>.draft.json 的 module 部分；§4.1 命名规则）。
    std::string moduleId;
    /// 该模块在磁盘上存在**可用**草稿（current 可解析且归属校验通过——
    /// 损坏文件条目 present=false，见文件头"增量落位说明 3"）。ui 的
    /// "未应用修改"标记只统计 present 项（§8.5 原文）。
    bool present = false;
    /// 草稿基线修订（present=false 时为全零保留值——无落盘事实可透传）。
    core::RevisionId baseRevision{};
    /// 过期标记：true＝base ≠ 分支当前 tip（应用会被拒——PM-04 stale
    /// 判据）；分支不存在于权威表时保守 true（孤儿草稿——同查询端口
    /// DraftInfo 口径）；present=false 时恒 false（无基线无过期可言）。
    bool stale = false;
    /// 落盘时间，ISO-8601 UTC 文本（present=false 时为空串）。
    std::string savedAtUtc;
    /// 草稿来源 token（autosave/manual/apply-retained——§4.4.5 冻结三值
    /// 的 token 形态；present=false 时为空串）。
    std::string origin;
};

/**
 * @brief 多模块草稿汇总投影（增量落位容器，见文件头"增量落位说明 2"；
 *        §5.4 summarize 的返回类型）。
 *
 * 线程安全：纯值类型。
 */
struct DraftProjection {
    /// 投影所属分支（＝summarize 的入参，回显以便投影独立消费）。
    core::BranchId branch{};
    /// 各模块条目（按 moduleId 字典序——NFR-COR-02 确定性：目录枚举序
    /// OS 相关，同磁盘状态必得同投影）。
    std::vector<DraftProjectionItem> items;
};

// =====================================================================
// DraftService——草稿服务端口契约（§5.4 原文方法集）
// =====================================================================

/**
 * @brief 草稿服务端口（§5.4）——未应用草稿落盘/恢复/投影/放弃的唯一
 *        入口（经 ProjectStore::drafts() 获取实例引用）。
 *
 * 生命周期与所有权：实现实例由存储上下文持有并随其消亡；本接口不提供
 *   任何生命周期管理入口。消费方（ui DraftController/域编辑器/工作流）
 *   只持引用调用（§8.2 定时器归 ui 会话层——本服务只提供落盘动作）。
 *
 * 模块 token 白名单（PersistenceFormat.hpp 注释"注册表校验归 DraftService"
 * 的落位）：moduleId 直接参与磁盘路径拼装（drafts/<branch>/<module>.
 * draft.json），必须是 1～64 个 ASCII 字母/数字/下划线/连字符——排除
 * 路径分隔符、盘符、点（防 .. 穿越/防与 .draft.json/.new/.bak 后缀体系
 * 产生命名歧义）。违约＝调用方错误（invalid_argument fail-fast），不做
 * 静默净化（静默改写会让调用方以为保存到了它指定的名字下）。
 */
class DraftService {
public:
    /// 虚析构：经接口引用消费、实现随存储上下文销毁的常规保障。
    virtual ~DraftService() = default;

    /**
     * @brief 保存草稿（§5.4 save；PM-04"保存仅落 drafts/"——不产生任何
     *        修订，PRJ-TX-6 的核心断言点）。
     *
     * 落盘协议（§5.4 原文序列，writer 互斥内执行——§9.8 变更性文件操作
     * 的串行化点）：①规范化编码（codec::dump——canonical 确定性字节，
     * NFR-COR-02）；②写 <module>.draft.json.new（writeThrough——写穿透
     * ＋flush 持久性闸门）；③旧 current 存在则改名 .bak（REPLACE_EXISTING
     * ——.bak 轮换为上一版，任务约束§五.6"D-12 模块粒度单文件＋.bak
     * 轮换"）；④.new 原子就位为 current（读者见旧或新完整内容之一）；
     * ⑤任一步失败删除 .new（"清理失败的 .new"——绝不留半写文件）。
     *
     * 门卫（§9.6 写入口统一防线，拒绝走返回值轨）：上下文非 Active
     * （Draining/Closed）→ ContextClosed＋PRJ-WRITE-AUTHORITY-LOST；只读
     * 上下文（PM-07——显式只读或降级）→ LockHeldByOther＋PRJ-LOCK-HELD；
     * 写权威探测失败（§9.6②）→ WriteRejected。门卫通过后持在途票据
     * （§9.7——requestClose 排空等待在途保存完成）。
     *
     * doc.branchId 决定落盘分支目录（drafts/<branchId>/）；分支是否存在于
     * 权威表**不做**校验（§5.4 save 前置仅"上下文 Active"；对未知分支的
     * 落盘会成为孤儿草稿——由打开协议⑤步扫描报告，调用方纪律保证只对
     * 活动分支保存；读取侧对未知分支保守标记 stale）。
     *
     * @param doc [in] 草稿文档（schemaVersion/savedAtUtc/origin 由调用方
     *            决定——autosave/manual 同一入口，§8.2；projectId 必须与
     *            本上下文一致，否则 invalid_argument）
     * @return 保存结果（ok/error/file 语义见 SaveResult）
     *
     * @throws std::invalid_argument doc.projectId 与本上下文不符／
     *         doc.moduleId 违反模块 token 白名单（调用方契约违约——
     *         fail-fast，不落半套数据）
     *
     * 复杂度：O(负载字节量)。
     *
     * 线程安全：任意线程可调（门卫＋writer 互斥内部串行）。
     */
    [[nodiscard]] virtual SaveResult save(const DraftDocument& doc) = 0;

    /**
     * @brief 加载草稿（§5.4 tryLoad；PM-04 恢复/PM-15 横幅的数据面）。
     *
     * 执行序：①.new 残留处置（存在即删除——§8.4"丢弃 .new、保留
     * current/.bak 并报告"，删除事实经开发诊断通道）；②读 current：
     * 不存在→nullopt（无草稿是常态不是错误）；③解析＋归属三元组校验
     * （projectId＝本上下文/branchId＝参数/moduleId＝参数——§4.4.5
     * "加载时与路径和 project.json 校验"，不一致＝DraftCorruptDetected）；
     * ④③任一失败→draft-corrupt 用户级诊断（PRJ-RECOVERY-ORPHAN-DRAFT，
     * 经 sink 与 diags 双通道——同 RecoveryReport 口径）并回退 .bak：
     * .bak 可解析且归属通过→返回 .bak 内容（diags 仍含 current 损坏
     * 记录）；.bak 也失败→追加诊断→nullopt。
     *
     * 上下文 Closed 后抛 ContextClosed（读轨拒绝——§4.7 同查询端口）；
     * Draining 排空期可用（PM-03 关闭对话框的草稿恢复态呈现）。
     *
     * @param branch   [in] 分支身份（brn- 规范文本）
     * @param moduleId [in] 模块 token（白名单同类契约——违约 fail-fast）
     * @param diags    [out] 本次加载产出的用户级诊断记录（追加语义——
     *                 不清空调用方传入的向量；与 sink 上报同源同序）
     * @return 草稿文档值拷贝（current 可用＝current 内容；current 损坏
     *         且 .bak 可用＝.bak 内容；皆不可得＝nullopt）
     *
     * @throws std::invalid_argument moduleId 违反白名单
     * @throws StoreError ContextClosed（上下文已关闭）
     *
     * 线程安全：并发只读安全（与保存并发：文件原子替换——读到完整或
     * 不存在的文件）。
     */
    [[nodiscard]] virtual std::optional<DraftDocument> tryLoad(
        core::BranchId branch, std::string_view moduleId,
        std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 多模块汇总投影（§5.4 summarize/§8.5）。
     *
     * 执行序：①权威 tip 快照（writer 锁内——stale 判据数据源，INV-M3
     * 纪律同查询端口）；②锁外扫描 drafts/<branch>/（单文件原子替换
     * 保证读到完整文件——与保存并发安全）；③每磁盘草稿文件装配条目
     * （可解析→present=true＋透传元数据；解析失败→present=false＋开发
     * 诊断——文件头"增量落位说明 3"）；④按 moduleId 字典序输出。
     *
     * 上下文 Closed 后抛 ContextClosed；Draining 排空期可用。
     *
     * @param branch [in] 分支身份（brn- 规范文本；目录不存在＝空投影
     *               ——无草稿是常态不是错误）
     * @return 投影（items 按 moduleId 字典序；空目录＝空 items）
     *
     * @throws StoreError ContextClosed（上下文已关闭）
     *
     * 线程安全：并发只读安全（权威快照的一致性窗口在 writer 锁内）。
     */
    [[nodiscard]] virtual DraftProjection summarize(core::BranchId branch) const = 0;

    /**
     * @brief 放弃草稿（§5.4 discard；PM-15 恢复横幅"放弃"的存储侧）。
     *
     * 目标态＝该 (branchId, moduleId) 的 current 与 .bak 均不存在；.new
     * 残留一并清理（放弃的完备语义——用户明确不要的数据不因崩溃残留
     * 复活；§5.4 表仅点名 current/.bak，.new 清理为本任务实现口径，
     * 登记于 units/project.md §12）。写操作——门卫与 save 同三道
     * （§8.6"放弃＝写操作，只读模式拒绝＋诊断"）；拒绝走返回值轨
     * （同 save）。幂等：目标已成立（本来就没有）＝ok=true 且两位
     * 明细 false。
     *
     * @param branch   [in] 分支身份（brn- 规范文本）
     * @param moduleId [in] 模块 token（白名单同类契约——违约 fail-fast）
     * @return 放弃结果（ok/error/file/removedCurrent/removedBackup 语义
     *         见 DiscardResult）
     *
     * @throws std::invalid_argument moduleId 违反白名单
     *
     * 线程安全：任意线程可调（门卫＋writer 互斥内部串行）。
     */
    [[nodiscard]] virtual DiscardResult discard(core::BranchId branch,
                                                std::string_view moduleId) = 0;

    /**
     * @brief 草稿清单（§5.4 list；§8.4"打开时 listDrafts＋tryLoad"的
     *        list 半边——恢复入口）。
     *
     * 扫描语义与查询端口 listDrafts 同源（§5.2）：drafts/<branch>/ 下
     * 以 ".draft.json" 结尾的当前有效草稿（.new/.bak 残留不列入——
     * §8.4）；stale 按权威元数据中该分支 tip 判定；个别草稿解析失败→
     * 跳过＋开发诊断（清单面保守不虚报可用草稿）；目录不存在＝空表。
     * 与查询端口的分工：本方法是草稿服务的恢复面入口（与 tryLoad 同
     * 生命周期语义），查询端口是跨单元只读视图面——两处各自落位、
     * 语义同源（QueryPort.hpp DraftInfo 注释的既定口径）。
     *
     * 上下文 Closed 后抛 ContextClosed；Draining 排空期可用。
     *
     * @param branch [in] 分支身份（brn- 规范文本）
     * @return 草稿条目列表（按 moduleId 字典序——NFR-COR-02）
     *
     * @throws StoreError ContextClosed（上下文已关闭）
     *
     * 线程安全：并发只读安全。
     */
    [[nodiscard]] virtual std::vector<DraftInfo> list(core::BranchId branch) const = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_DRAFTSERVICE_HPP
