/**
 * @file   SolidifyServiceImpl.hpp
 * @brief  外部资源固化服务实现（SolidifyServiceImpl）——§5.7 服务实体
 *         的实现载体：io 源安全读取编排、固化/重关联命令装配与提交、
 *         副本字节交接（pending 槽）与结果映射；附带固化/重关联命令
 *         处理器（SolidifyResourceHandler——经 HandlerRegistry 注册，
 *         §6.5 装配协议）。私有实现头，不出 include/——R-2 纪律。
 *
 * 设计依据：
 *   - units/project.md §5.7（固化流程四段：io 安全读取→前后哈希比对
 *     →canonical 字节经对象库随命令提交发布→state=materialized 引用
 *     保护；责任划分"io＝解析与安全读取、缺失/变化检测实现；project
 *     ＝存储发布与引用保护"）、§6.5（处理器注册与注入——"project 即
 *     处理器"命令族的注册面；L5 装配期注册进 HandlerRegistry）、
 *     §6.4（命令载荷序列化与版本策略——payload 为域所有 canonical
 *     字节，project 不解释）、§6.1（命令生命周期——提交零修订失败面）、
 *     §4.6（对象库编址——oid 复用＋新 cv＝替换版本）、§10.4/O-09
 *     （io↔project 注入式协作——本头经公共窄接口消费，零 io 头）；
 *   - 需求 CON-03（不可变副本＋引用保护——发布经命令事务）、PM-09/
 *     AT-21（重关联＝显式命令提交产生新修订）、PM-01（转正式固化）、
 *     NFR-REL-04（复制期间源变化可检测——透传 io 检测结论）、EVI-01
 *     （判据数据面在公共头——本头不涉判定，N-9）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1～2。
 *
 * 背景说明（本类在单元内的位置——为什么不是存储上下文的第六端口）：
 *   §5.1 ProjectStore 的五端口访问器（query/commands/drafts/archive/
 *   undoRedo）已随 PRJ-T14 全部挂载冻结；§5.7 未给 ProjectStore 增加
 *   端口。固化服务是**装配侧服务**（与命令处理器同族）：由 L5/测试经
 *   createSolidifyService() 构造，命令经既有①端口提交，处理器经既有
 *   公共装配通道 handlerRegistry()（UI-T39 落位）注册——零存储上下文
 *   结构变更。
 *
 * 实现口径登记（DTB §5.4——单元卡 §5.7 未定义判据，五项，详见各私有
 *   方法/自由函数注释）：
 *   ①编排分解＝读取在命令外、发布在命令内（§5.7"随命令提交发布"＋
 *     io.md §8.4 步骤 7 的时序原文；读取失败/变化＝命令未提交零修订
 *     ——不借用 §6.3 拒绝词表表达 io 环境事实）；
 *   ②副本字节交接＝服务内 pending 槽（mutex 保护），处理器 prepare 期
 *     按载荷绑定三元组（refId＋hash＋size）取走——同线程提交链无竞态，
 *     绑定校验为防御面（不匹配＝invalid-payload 拒绝，生产路径不可达）；
 *   ③对象类型 token＝"ExternalResource"（域登记的稳定 token，§4.6/
 *     §4.4.2——固化/重关联命令族归 project 所有，token 由本单元登记）；
 *   ④载荷 v1＝行式 canonical 编码（magic＋定序键值行；自由文本字段
 *     hex 编码保证 ASCII 与 round-trip——§4.8 project 自有格式纪律的
 *     域内实例；编码/解析见 .cpp encodeSolidifyPayload/
 *     tryParseSolidifyPayload）；
 *   ⑤域内拒绝零用户级诊断码（载荷结构/状态/pending 绑定违约的
 *     Rejected(invalid-payload) 不产 DiagnosticRecord——§4.6 收编清单
 *     无对应码，CR-08 不私造；机器可读面＝status.rejection，开发观察
 *     面＝命令服务 S3 既有 dev 留痕）；io 产出诊断按 §5.7 原样透传。
 *
 * 线程模型（高危信息）：
 *   - m_opMutex：solidify/relink 服务级操作互斥（pending 槽为单份服务
 *     状态——并发固化若不串行会互相覆盖交接面）；锁序 m_opMutex →
 *     m_pendingMutex，不反向；
 *   - m_pendingMutex：仅保护 m_pending 槽；命令处理器 prepare（命令
 *     槽内、m_opMutex 持有中重入）只取本锁——与 m_opMutex 无死锁；
 *   - probe 直通源（无共享状态——源实现按 io.md §9.8 并发安全）。
 *
 * 所有权与生命周期：由 createSolidifyService 经 unique_ptr 交付装配方；
 *   store/source 两个引用/指针全部非 owning——生存期由装配方保证覆盖
 *   本对象（注册进存储上下文注册表的处理器持本类回指针，故本对象
 *   必须先于存储上下文析构，或至少覆盖期间全部固化/重关联提交）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_SOLIDIFYSERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_SOLIDIFYSERVICEIMPL_HPP

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/ResourceRefs.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

/**
 * @brief 固化/重关联命令处理器（§5.3.2 ICommandHandler 实现——"project
 *        即处理器"命令族的 PRJ-T17 落位）。
 *
 * 职责（prepare 阶段，命令槽内执行）：解析载荷 v1（结构性违约→
 *   RejectedInvalidInput）→ 记录状态复核（state==Recorded）→ 按载荷
 *   绑定三元组从服务 pending 槽取副本字节（防御校验）→ 产出计划
 *   （ObjectWrite{target, "ExternalResource", 副本字节}）。**不调用
 *   外部源**——读取已在服务层完成（实现口径①），处理器只做发布装配
 *   （CON-03：副本经命令事务入对象库）。
 *
 * 可逆性：不声明逆命令（§6.9 可逆性是处理器声明的事实）——对象库只增
 *   （PA-2），固化无"撤销"表达；已固化内容的更正走重关联新修订。
 *
 * 线程约束：prepare 只在命令执行槽内被调用（§6.1）；持有的服务回指针
 *   非 owning，生存期由装配纪律保证（见 SolidifyServiceImpl 头注释）。
 */
class SolidifyResourceHandler final : public ICommandHandler {
public:
    /**
     * @brief 构造处理器（仅服务可达——构造期注册进注册表）。
     *
     * @param owner       [in] 宿主服务（非 owning——pending 槽提供方；
     *                    生存期覆盖本处理器在注册表中的存续）
     * @param commandType [in] 注册 token（kSolidifyCommandType 或
     *                    kRelinkCommandType——两类命令共用本实现，差异
     *                    仅 token 与摘要文案，PM-09 历史留痕面）
     * @param summaryPrefix [in] 命令摘要前缀（"固化外部资源"/"重关联
     *                      外部资源"——随修订持久化的人读摘要，PM-12-S1
     *                      历史浏览展示面）
     */
    SolidifyResourceHandler(class SolidifyServiceImpl* owner,
                            std::string commandType,
                            std::string summaryPrefix);

    [[nodiscard]] std::string commandType() const override
    {
        return m_commandType;
    }

    /// 载荷版本（实现口径④的 v1——格式演进时递增并按 §6.4 受理集合
    /// 纪律显式扩面，本版本只受理 1）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override
    {
        return 1;
    }

    PrepareOutcome prepare(HandlerContext& ctx,
                           const CommandEnvelope& envelope,
                           const RevisionView& baseSnapshot,
                           CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& diags) override;

private:
    /// 宿主服务（pending 槽提供方——非 owning，见构造注释）。
    SolidifyServiceImpl* m_owner;
    /// 注册 token（构造注入——solidify/relink 两实例）。
    std::string m_commandType;
    /// 命令摘要前缀（构造注入——随计划 summary 持久化）。
    std::string m_summaryPrefix;
};

/**
 * @brief §5.7 固化服务契约的实现（final——不再派生；消费者只面向公共
 *        抽象与工厂，实现细节不外泄——R-2）。
 *
 * 生命周期与所有权：createSolidifyService() 构造（注册两处理器后交付
 *   unique_ptr）；store/source 非 owning（见文件头）。构造期注册失败
 *   （token 重复）异常透传装配方（fail-fast——装配错误不过夜）。
 */
class SolidifyServiceImpl final : public SolidifyService {
public:
    // 命令处理器取 pending 槽的窄通道（friend 收敛——处理器不持服务
    // 私有成员之外的任何依赖）。
    friend class SolidifyResourceHandler;

    /**
     * @brief 构造固化服务并注册命令处理器（仅工厂可达）。
     *
     * @param store  [in] 存储上下文（非 owning——commands() 与
     *               handlerRegistry() 装配通道的消费面）
     * @param source [in] 外部资源源（非 owning，可空＝未装配——调用
     *               solidify/relink/probe 即 std::logic_error，与
     *               attachCompilePort"构造默认空＋装配后注入"先例同
     *               口径；生产装配在构造前就绪，可空性为测试/分步装配
     *               保留——装配违约 fail-fast，不静默吞）
     *
     * @throws std::invalid_argument token 重复注册（重复创建服务）
     * @throws std::logic_error 存储上下文未开放装配通道（基类默认实现）
     */
    SolidifyServiceImpl(ProjectStore& store, IExternalResourceSource* source);

    // ---- SolidifyService 契约（公共头注释为准） ----

    [[nodiscard]] SolidifyResult solidify(const SolidifyRequest& request,
                                          core::BranchId branch,
                                          const ExternalResourceRecord& record) override;
    [[nodiscard]] SolidifyResult relink(const SolidifyRequest& request,
                                        core::BranchId branch,
                                        const ExternalResourceRecord& newRecord) override;
    [[nodiscard]] ExternalRefProbe probe(const ExternalResourceRecord& record) override;

private:
    /**
     * @brief pending 副本交接条目（服务→处理器的单份在途字节；实现
     *        口径②）。
     *
     * 绑定三元组（refId/hash256/sizeBytes）与命令载荷逐字段对应——
     * 处理器据此校验"取到的字节就是本命令载荷声明的副本"（防御面：
     * 生产路径由服务级互斥保证一一对应，校验拦截实现缺陷而非用户输入）。
     *
     * 线程安全：纯值类型（实例受 m_pendingMutex 保护）。
     */
    struct PendingRead {
        std::string refId;         ///< 绑定键：记录 id（载荷 ref= 同源）
        std::string hash256;       ///< 绑定键：副本 cv- 规范文本（载荷 hash= 同源）
        std::uint64_t sizeBytes = 0;  ///< 绑定键：副本字节数（单位＝字节）
        std::vector<std::uint8_t> bytes;  ///< 副本字节（io 交接——只搬运不解释）
    };

    /**
     * @brief solidify/relink 共用编排体（两命令仅 token 与摘要文案
     *        不同——PM-09 语义标记面）。
     *
     * @param request   [in] 请求（前置校验通过）
     * @param branch    [in] 目标分支
     * @param record    [in] 待固化记录（state==Recorded）
     * @param commandType [in] 命令 token（kSolidifyCommandType/
     *                    kRelinkCommandType）
     * @param summaryPrefix [in] 命令摘要前缀（"固化外部资源"/"重关联
     *                      外部资源"——历史浏览展示面）
     *
     * @return 固化结果（执行序见公共头 solidify() 注释 1～4 步）
     */
    [[nodiscard]] SolidifyResult submitSolidify(const SolidifyRequest& request,
                                                core::BranchId branch,
                                                const ExternalResourceRecord& record,
                                                const char* commandType,
                                                const char* summaryPrefix);

    /// 前置校验（调用方错误 fail-fast 的统一入口；违约即抛
    /// std::invalid_argument——逐项语义见公共头 @throws）。
    void validateRequest(const SolidifyRequest& request,
                         const ExternalResourceRecord& record) const;

    /// 存储上下文（命令端口与注册表——非 owning，见文件头）。
    ProjectStore& m_store;
    /// 外部资源源（非 owning，可空＝未装配——装配违约 fail-fast）。
    IExternalResourceSource* m_source;
    /// 服务级操作互斥（solidify/relink 串行——pending 槽单份状态；
    /// 锁序首位）。
    std::mutex m_opMutex;
    /// pending 槽互斥（处理器 prepare 重入面——锁序末位，见文件头）。
    std::mutex m_pendingMutex;
    /// pending 副本交接槽（submitSolidify 置入 → 处理器 prepare 取走
    /// → submitSolidify 清残留；有值窗口＝单次提交在途）。
    std::optional<PendingRead> m_pending;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_SOLIDIFYSERVICEIMPL_HPP
