/**
 * @file   Sources.hpp
 * @brief  runtime 注入最小契约——修订/对象字节只读来源与编译取消令牌
 *         （值传递＋最小注入接口，依赖白名单的实施形态）。
 *
 * 设计依据：
 *   - units/runtime.md §3.3（对 project/modeling/io/execution 能力的注入
 *     边界——本头全部接口/值类型的原文契约）、§5.1/§5.2（CompileRequest
 *     输入面与十段表各注入点失败的错误归属）、§5.5（"全部注入接口约定
 *     并发只读安全"）、§9.3（worker 进程内实现＝随请求物化的载荷存储）
 *   - ARCH §3.5（依赖表只登记 runtime→core——快照编译需要读修订对象/
 *     参数化模型/资源，但不得产生编译依赖，故经本头注入）
 *   - 需求 MDL-06（编译期间不修改项目历史——零写路径）、CON-01（闭包
 *     防混入 CM-0）、TASK-01（协作取消）；任务契约
 *     tasks/foundation/RT-T02.json（acceptance 2）
 *
 * 背景说明（为什么是注入而不是链接）：快照编译客观上需要读 project 的
 * 修订对象、modeling 的参数化模型、io 的资源——但 ARCH §3.5 依赖表禁止
 * runtime 链接这些单元（业务域互链红线 R-1）。解决方案＝runtime 只定义
 * "最小只读接口＋值类型投影"（本头），适配器归 L5 应用壳在装配期统一
 * 提供（NFR-MNT-04：避免各请求方重复适配）；worker 进程内的实现＝
 * "随请求物化的载荷存储"（execution 序列化后装配，§9.3），同样零
 * project 依赖。
 *
 * 来源注入点与错误归属表（§5.2 十段表——acceptance 2"错误归属可定位"
 * 的契约面；下游编译器 RT-T11 按此表转译，测试 SourcesTest 钉住）：
 *   | 注入点                                | 返回形态      | 归属段 | 归属错误码            |
 *   | IRevisionClosureSource::tryRevision   | nullopt       | S1     | UnknownObject（修订不存在）/ContextReleased（上下文已关闭） |
 *   | IRevisionClosureSource::objectInRevision | false      | S2     | StructureInvalid（引用不在闭包——CM-0 防混入，RT-CONT-1） |
 *   | IObjectBytesSource::tryObjectBytes    | nullopt       | S2     | InputInvalid（对象字节不可得，含对象定位） |
 *   | ICompileCancelToken::cancellationRequested | true     | 段边界 | Cancelled（协作取消——非错误路径，UX-03/D-11） |
 *
 * 线程约束：全部注入接口约定并发只读安全（§5.5 可重入表）——实现方
 * 必须保证多线程同时调用不产生数据竞争；接口零写方法（MDL-06 编译期间
 * 不修改项目历史——写路径在类型层即不存在，RT-CONT-2 的类型层证据）。
 *
 * 生命周期/所有权：适配器由 L5 装配期创建并持有（CompileRequest 以
 * 引用/指针携持，编译期不接管所有权——§5.1 输入面：ICompileCancelToken*
 * 可为空＝不可取消）。
 */

#ifndef SDURWS_IRD_RUNTIME_SOURCES_HPP
#define SDURWS_IRD_RUNTIME_SOURCES_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::runtime {

/**
 * @brief 对象字节只读来源（适配 project ②端口 tryObject(oid, cv)）。
 *
 * 语义（§3.3）：给定对象身份＋内容版本，取回该对象的 canonical 字节；
 * nullopt＝对象字节不可得（对象不存在或该版本不可用）——调用方（编译器
 * S2 段）按上方归属表转译 InputInvalid 并就地定位，来源实现不自行决定
 * 错误语义（错误归属唯一归 runtime 编译链）。
 *
 * 实现方约束：并发只读安全（§5.5）；零写方法（类型层纪律，MDL-06）。
 * 返回的字节归调用方所有（值传递——optional<vector<uint8_t>>）。
 */
class IObjectBytesSource {
public:
    virtual ~IObjectBytesSource() = default;

    /**
     * @brief 尝试取回对象在指定内容版本的字节（只读、不抛）。
     * @param objectId [in] 目标对象稳定身份（跨修订不变——ARC-04）
     * @param version  [in] 要求的内容版本（摘要版本戳——CON-01 身份/版本包络）
     * @return 命中＝对象 canonical 字节；nullopt＝不可得（归属表见文件头）
     *
     * 确定性：同一 (objectId, version) 重复调用必须返回相同字节或稳定地
     * 返回 nullopt（来源不可在读取中途变化内容——变化检测归资源侧
     * §5.4/RT-RES-2，对象字节侧以确定性为前提）。
     */
    virtual std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId objectId, core::ContentVersion version) const = 0;
};

/**
 * @brief 修订对象引用条目（与 evidence §4.1.2 ObjectRefEntry 同形）。
 *
 * 值语义纯结构；四字段全等＝条目全等（字段逐一比较——无序容器语义，
 * objectRefs 列表顺序由来源方给出，编译器侧不重排）。
 * 线程安全：纯值。
 */
struct ObjectRefEntry {
    core::ObjectId objectId;             ///< 对象稳定身份（跨修订不变）
    core::ContentVersion contentVersion; ///< 本修订锁定的内容版本
    std::string objectTypeToken;         ///< 对象类型 token（如 "robot-design"——路由到对应解析器）
    core::Digest256 digest;              ///< 对象字节摘要（完整性校验/防混入比对——CM-0）
};

/**
 * @brief 机器人设计对象的类型 token 权威常量（"robot-design"）。
 *
 * 消费点（编译链 S2——RT-T11）：RevisionSummary.objectRefs 中
 * objectTypeToken 命中本常量的对象即 RobotDesign 权威对象（§4.1——
 * "对象字节经注入的 IRobotDesignReader 读取"的规范化对象）；S2 按 token
 * 定位恰一个 robot-design 对象（零个/多个均失败——编译器实现登记）。
 * 常量单点（§3.3 注释的字面落地）——消费方不得另写字面量，避免拼写漂移
 * 造成路由失联（NFR-COR-02 确定性纪律的码面同款）。
 */
inline constexpr const char* kRobotDesignObjectType = "robot-design";

/**
 * @brief 修订只读视图的最小投影（适配 project RevisionView）。
 *
 * "最小"＝只含编译链 S1 锚定所需的字段：修订身份/序号/父修订（闭包
 * 追溯）/分支（归属）＋对象引用清单。S1 段一次读取本投影，之后一切解析
 * 只对该视图进行（§5.2 S1——"一次读取，之后一切解析只对该视图"）。
 * 线程安全：纯值；持 core 强类型身份，无裸引用。
 */
struct RevisionSummary {
    core::RevisionId id;                    ///< 修订身份（一次命令提交＝一个修订，ARC-01）
    std::uint64_t seq = 0;                  ///< 修订序号（单调递增，project 侧分配；无单位）
    std::optional<core::RevisionId> parent; ///< 父修订（nullopt＝分支首修订——闭包追溯用）
    core::BranchId branch;                  ///< 所属方案分支身份
    std::vector<ObjectRefEntry> objectRefs; ///< 本修订可见的对象引用清单（顺序由来源方给出）
};

/**
 * @brief 修订闭包只读来源（适配 project ②端口 revision()/head() ＋闭包校验）。
 *
 * 语义（§3.3）：按修订身份取只读投影＋按 (修订, 对象, 版本) 三元组做
 * 闭包成员判定。nullopt/false 的错误归属见文件头表（S1/S2）——尤其
 * objectInRevision 返回 false 是 CM-0 防混入的判定输入（RT-CONT-1：
 * 返回不属于目标修订的 (oid,cv) 必须 StructureInvalid 拒绝）。
 *
 * 实现方约束：并发只读安全；tryRevision 与 objectInRevision 对同一修订
 * 必须口径一致（同一 (rev,oid,cv) 不得出现"在 objectRefs 内而
 * objectInRevision 为 false"的自相矛盾——否则编译器按防混入拒绝）。
 */
class IRevisionClosureSource {
public:
    virtual ~IRevisionClosureSource() = default;

    /**
     * @brief 尝试取修订只读投影（S1 锚定；只读、不抛）。
     * @param revision [in] 目标修订身份
     * @return 命中＝修订投影；nullopt＝修订不存在（归属表：UnknownObject）
     */
    virtual std::optional<RevisionSummary> tryRevision(core::RevisionId revision) const = 0;

    /**
     * @brief 闭包成员判定：(oid, cv) 是否属于该修订（只读、不抛）。
     * @param revision [in] 目标修订身份
     * @param objectId [in] 待判定对象身份
     * @param version  [in] 待判定内容版本
     * @return true＝在闭包内；false＝不在（归属表：StructureInvalid——CM-0）
     */
    virtual bool objectInRevision(core::RevisionId revision, core::ObjectId objectId,
                                  core::ContentVersion version) const = 0;
};

/**
 * @brief 编译取消令牌（适配 execution 取消信号；协作取消）。
 *
 * 语义（§3.3/§5.5/D-11）：编译器在每段边界与资源逐项循环处轮询本接口；
 * true＝请求取消→编译走 RollingBack→CompileOutcome{Cancelled}（非错误
 * 路径——UX-03 正常取消不产错误诊断）。取消的语义与时窗归 execution
 * （NFR-PERF-02），runtime 只保证轮询密度（每段边界检查）。
 * 注入可为空（CompileRequest 中 ICompileCancelToken* 为空＝不可取消）。
 *
 * 实现方约束：并发只读安全（编译器线程之外，execution 侧可随时置位——
 * 实现内部自行同步，如原子量/互斥）；轮询间状态只增不减（一旦请求取消
 * 不得反悔——取消的最终性由 execution 通道保证）。
 */
class ICompileCancelToken {
public:
    virtual ~ICompileCancelToken() = default;

    /**
     * @brief 查询是否已请求取消（只读、不抛、快速返回——编译线程高频轮询）。
     * @return true＝已请求取消（此后保持 true）；false＝未请求
     */
    virtual bool cancellationRequested() const = 0;
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_SOURCES_HPP
