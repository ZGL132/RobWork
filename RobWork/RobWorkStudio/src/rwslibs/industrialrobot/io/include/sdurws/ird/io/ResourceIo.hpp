/**
 * @file   ResourceIo.hpp
 * @brief  资源导入服务与外部源检测——ResourceSnapshot/ResourceContentId、
 *         ExternalRefProbe 事实三态、IResourceReader（文件层读取：open/
 *         snapshot/identify/dependencyTree）、IResourceSnapshotter（段②
 *         检测 probe＋段③固化执行 solidifyToStaging）与 IRuntimeResource
 *         Adapter（runtime 资源读取桥——P-RT-6/P-IO-2 裁决落点）。
 *
 * 设计依据：
 *   - units/io.md §6.1~§6.6（交接总则/URDF·Xacro include 边界/网格规模
 *     预检/依赖树/变化检测与"缺失≠不可行"）、§8.1~§8.6（三段边界承接/
 *     读取快照/缺失变化检测/固化流程 io 执行侧/引用关系图/accessVersion
 *     与 ResourceBytes 生命周期五条裁决——P-RT-6 关闭）、§9.6~§9.8
 *     （三个接口的契约表——"签名均为实现建议，实现期允许等价调整、语义
 *     不变"）、§4.1~§4.5（路径角色/SafePath/防护流程②③④⑤⑥⑦/预算）、
 *     §2.5（四类错误正交——资源事实不产业务结论）、§3.1（公共头表
 *     ResourceIo.hpp 行）
 *   - 需求 NFR-REL-04（外部网格/目录缺失或变化可检测——检测实现责任方）、
 *     NFR-SEC-01/02（P-1 一次性读取例外＋预算管辖）、PM-01（三段边界：
 *     段①③执行＋段②检测）、CON-03（固化复制，发布与引用保护归
 *     project）、MDL-19（展开护栏文件层——展开引擎归 modeling 阶段 B）
 *   - 任务契约 tasks/foundation/IO-T05.json（≙WP-11-T06）acceptance
 *     1~5（V15~V17/V24~V27＋三段边界＋P-IO-1/P-IO-2 处置）
 *
 * 背景说明（本头在 io 单元中的位置——为什么"读取"与"检测"分两个接口）：
 *   IResourceReader 是**无状态文件层读取服务**（§9.6：并发安全，句柄各
 *   自独立），产出"已验证字节＋快照三元组"；IResourceSnapshotter 是
 *   **段②/段③协作面**（§9.8：probe 并发安全、solidify 会话级），消费
 *   project 注入的引用记录并执行固化中转复制。两者共享同一 SafePath＋
 *   BudgetGuard 防护核（SA-14 统一入口防护）与同一 SHA-256 摘要实现
 *   （SA-12：core ContentDigester——io 不引入第二种摘要算法）。
 *
 * 路径不作身份（SP-5/§1.3 目标 3/§8.2）：ResourceSnapshot.finalPath 仅
 * 追溯提示；资源内容身份＝contentDigest（对字节的 SHA-256），不含路径、
 * 不含 mtime——同内容不同路径＝同身份（V27 断言面）。本头全部输出不产
 * 出任何对象 ID（身份由 core/project 分配）。
 *
 * 资源事实与业务结论正交（§2.5/§6.6/§10.8）：probe 的 Missing/Changed
 * 与 dependencyTree 的缺失清单都是**资源事实**——本头任何接口不产出、
 * 不暗示"工程不可行/模型不可用"结论（判定归 evidence 门禁与业务单元）；
 * 用户重关联流程归 PM-09/project 命令。
 *
 * P-IO-1 处置（acceptance 5——ARCH §3.5 未登记 io↔project/io↔runtime
 * 编译边）：本头对 project/runtime 均**零编译依赖**——
 *   - IProjectBytesSource/IExternalRefSource 是 io 自有注入接口（§9.7，
 *     project 在 L5 装配期实现并注入），io 不链接 project 目标、不引用
 *     project 头（§2.3 非目标 6：不直读项目存储——对象库字节只能经
 *     IProjectBytesSource 注入到达）；
 *   - IRuntimeResourceAdapter 是 io 自有的桥接契约（§9.7 桥接说明）：
 *     runtime 真正的 IRuntimeResourceProvider 适配器由 L5 装配层把本
 *     接口的 ResourceReadResult/ResourceBytesView 转译为 runtime 的
 *     ResourceBytes（P-IO-1 裁决为补登直连边时，转译层收敛为薄壳，本头
 *     零改动——IO-D02"接口契约与裁决结果无关"）。
 *
 * P-IO-2 处置（acceptance 5——io.md §8.6 五条裁决，本头为 IRuntimeResource
 * Adapter 的生命周期承载）：ResourceBytesView.data 指向的缓冲区自
 * tryResourceBytes 返回起至少有效至同一 provider 实例的下一次调用，实际
 * 保证**至 provider 析构**（裁决第 1 条：强于最低要求）；调用方同步消费、
 * 不跨调用长期持有（第 2 条纪律）；并发 tryResourceBytes 安全——Recorded
 * 每次调用返回独立稳定缓冲（第 3 条），Solidified 命中不可变缓存（第 4
 * 条允许以 {resourceId, accessVersion} 缓存）；Recorded 资源绝不跨调用
 * 缓存——每次重读＋重算 digest（第 4 条，runtime S10 复查语义成立的前
 * 提）；未来改值拷贝语义兼容（第 5 条）。
 *
 * 线程安全：IResourceReader/IResourceSnapshotter::probe/IProjectBytes
 * Source/IExternalRefSource 实现为无状态并发只读安全；IResourceSnapshotter::
 * solidifyToStaging 与 IRuntimeResourceAdapter 的会话态（中转区互斥/缓存
 * 与稳定区）由实现内部加锁（§9.8/§8.6.3）。io 不创建线程（§9.13）。
 * 确定性（NFR-COR-01/02）：同文件同快照（digest 为纯字节变换）；依赖树
 * 遍历序与节点/边输出序稳定（折叠相对键字典序）；诊断 params 键序固定。
 */

#ifndef SDURWS_IRD_IO_RESOURCEIO_HPP
#define SDURWS_IRD_IO_RESOURCEIO_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // core::Digest256/ContentVersion——SHA-256 唯一摘要算法（SA-12）
#include <sdurws/ird/core/Identity.hpp> // core::ObjectId——资源对象身份（§8.5 引用键；project 分配）
#include <sdurws/ird/io/Budget.hpp>     // IBudgetGuard/BudgetScopeId——预算挂钩（§4.5/§9.2）
#include <sdurws/ird/io/IoFwd.hpp>      // IoString/IoResult/IoCancelToken/IoProgressCallback（§9.0）
#include <sdurws/ird/io/IoError.hpp>    // IoError/IoErrorCode——错误轨道（§4.2.5 四分类/§2.5 正交）
#include <sdurws/ird/io/SafePath.hpp>   // PathRole/ISafePathResolver——路径角色与规范化（§4.1/§9.1）

namespace sdurws::ird::io {

// =====================================================================
// 读取块大小（§4.4⑤：读取循环逐块流式——块即预算/取消检查点粒度）
// =====================================================================

/**
 * 资源读取/复制的流式块大小：64 KiB。来源说明：io 卡未钉死块值（§4.4
 * 只要求"每块检查点"）；64 KiB 在 NTFS 常规簇与 ReadFile 系统调用开销
 * 之间取衡，且让 V17 的"复制中途改源"注入窗口足够窄（块数多、注入点
 * 确定）。修订随单元卡增量修订，不影响任何外部契约。
 */
inline constexpr std::size_t kResourceReadBlockBytes = 64u * 1024u;

// =====================================================================
// ResourceContentId / ResourceSnapshot（§3.1 表行；§8.2 原文承载）
// =====================================================================

/**
 * @brief 资源内容身份（§3.1 公共头表 ResourceIo.hpp 行登记实体）。
 *
 * 语义（§8.2"资源内容身份"行）：内容身份＝对资源字节的 SHA-256 摘要，
 * **不含路径、不含 mtime**——同内容不同路径＝同身份（runtime.md §8.6
 * "路径变化而内容不变→身份不变"同源；V27 断言面）。本类型只是摘要值
 * 的强类型包装，不携带任何路径成分（SP-5：SafePath 输出不产出对象 ID；
 * 本类型同样不以路径参与相等性）。
 *
 * 生命周期/所有权：纯值类型，调用方所有；并发只读安全。
 */
struct ResourceContentId {
    /// 摘要原始字节（SHA-256；全零＝空保留值——core Digest256 纪律）。
    core::Digest256 digest{};

    /// 非全零（全零＝空，不作身份——core 保留值纪律）。
    bool isValid() const noexcept;

    /**
     * 64 位小写十六进制文本（诊断 params/日志呈现用）。注意：这只是
     * 摘要的呈现形式，不引入第二种摘要算法（SA-12——十六进制编码不是
     * 哈希）；core::detail::formatDigest 是 core 内部设施（tag 化），
     * io 侧按 io 诊断参数键惯例直接呈现裸 hex。
     */
    std::string toHex() const;

    /// 字节精确相等（身份判据——digest 即身份）。
    bool operator==(const ResourceContentId& o) const noexcept { return digest == o.digest; }
    bool operator!=(const ResourceContentId& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 读取快照（§8.2 原文四字段——变化检测基准三元组＋实体路径）。
 *
 * 字段语义（§8.2 注释原文逐条）：
 *   - finalPath：实体路径（weakly_canonical 解析后的最终实体——P-1 跟随
 *     符号链接后按实体路径记录，§4.2.3 P-1 行）；**仅追溯提示，不作身份**。
 *   - sizeBytes：本次读取的实读字节总数（快照事实；预筛用）。
 *   - mtimeUtc：修改时间，Windows NT FILETIME（UTC，1601-01-01 起
 *     100ns 计数）——**快速预筛（非权威）**：变化检测的权威判据是
 *     contentDigest（IO-D10；mtime 仅用于"必疑"预筛与提示性诊断）。
 *   - contentDigest：SHA-256（权威判据；core ContentDigester——§8.2
 *     "摘要算法唯一"）。
 *
 * 快照稳定性（§8.2 第三条）：同一未变文件重复读取 digest 必须相同
 * （V27 断言面）；读取期间文件被写则本次 digest 反映混合窗口——由固化
 * 复制期间的前后双快照检测兜底（§8.4 步骤 5，V17）。
 *
 * 生命周期/所有权：纯值类型，调用方所有；并发只读安全。
 */
struct ResourceSnapshot {
    std::filesystem::path finalPath;    ///< 实体路径（weakly_canonical；追溯提示——不作身份）
    std::uint64_t sizeBytes = 0;        ///< 实读字节总数（单位：字节）
    std::uint64_t mtimeUtc = 0;         ///< 修改时间（NT FILETIME，100ns/1601 UTC；预筛非权威）
    core::Digest256 contentDigest{};    ///< SHA-256 摘要（权威判据；core ContentDigester）

    /// 内容身份视图（digest 的强类型包装——§8.2 资源内容身份）。
    ResourceContentId contentId() const;

    /**
     * 内容相同判定（变化检测的判据形态——IO-D10"权威判据＝digest"）。
     * 仅比较 contentDigest；size/mtime/finalPath 差异**不**参与（§8.3
     * "digest 相同视为未变，size/mtime 不同仅提示"——防 touch 误报）。
     */
    bool sameContentAs(const ResourceSnapshot& o) const noexcept
    {
        return contentDigest == o.contentDigest;
    }
};

// =====================================================================
// ResourceKind（§6.1"格式识别（魔数/扩展名）"的产出；§6.3 网格族）
// =====================================================================

/**
 * @brief 资源格式种类（§6.3 格式识别：魔数/扩展名；枚举序＝识别族稳定
 *        序，只允许表尾追加并走单元卡增量修订——IoErrorCode 同款纪律）。
 *
 * 阶段 A 范围（§13.3"ResourceKind 扩展（DAE 完整/新格式）留待阶段 B"）：
 * 二进制/ASCII STL、OBJ、DAE（识别即可，DAE 内嵌引用解析阶段 B）、
 * URDF/Xacro XML、通用 XML（R2 预留通道的文件层形态）、MTL 材质库、
 * 纹理字节资源。识别失败→IO-FORMAT-MESH-UNKNOWN（§6.3"附首 16 字节
 * 十六进制摘要，脱敏无虞"）。
 */
enum class ResourceKind : std::uint8_t {
    Unknown,       ///< 未识别（对外接口不返回此值——识别失败走 IO-FORMAT-MESH-UNKNOWN）
    BinaryStl,     ///< 二进制 STL（84 字节头含三角数——可即时计数预检，§6.3）
    AsciiStl,      ///< ASCII STL（流式统计 facet 计数——§6.3）
    WavefrontObj,  ///< Wavefront OBJ（流式统计 v/f 行；mtllib→Material 边，§6.3/§6.5）
    ColladaDae,    ///< COLLADA DAE（阶段 A 仅识别；内嵌引用解析阶段 B——§13.3）
    UrdfXml,       ///< URDF XML（mesh 引用边，§6.2/§6.5）
    XacroXml,      ///< Xacro XML（include/mesh 引用边，§6.2/§6.5）
    GenericXml,    ///< 通用 XML（.xml/.wc.xml——WorkCell R2 预留通道的文件层形态 §6.4；阶段 A 仅识别＋依赖树 include/mesh 边抽取，语义归 modeling）
    MaterialLib,   ///< Wavefront MTL 材质库（map_Kd→Texture 边，§6.5）
    TextureBytes,  ///< 纹理/字节资源（PNG/JPG/…——大小预算＋摘要，§6.3"按字节资源处理"）
};

// =====================================================================
// ResourceStreamHandle（§9.6 open 产物——流式读取句柄）
// =====================================================================

/**
 * @brief 句柄实现承载（ResourceIo.cpp 内定义的不完整类型——公共头只
 *        暴露 opaque 前向声明，Win32 句柄与元数据不出公共面，§1.4）。
 *
 * 外部代码无法命名完整类型、无法构造或解引用——句柄的装配只能经
 * ResourceStreamHandle::adopt（实现翻译单元内部使用），伪造面为零。
 */
struct ResourceStreamImpl;

/**
 * @brief 流式读取句柄（§9.6"返回流式读取句柄（预算挂钩）"——RAII 关闭，
 *        §9.6 生命周期行"句柄会话级（RAII 关闭）"）。
 *
 * 行为契约（§9.6 契约表）：
 *   - open 成功＝句柄已通过 SP-4/SP-6（reparse 拒绝打开＋final-path
 *     复核——P-4/P-5 角色；P-1 跟随链接并按最终实体路径记录）；
 *   - 句柄字节流归句柄；snapshot/树归调用方（§9.6 所有权行）；
 *   - 并发安全（§9.6 线程约束行"并发安全（句柄各自独立）"）——单个
 *     句柄的 read 非并发（读取位置是会话态）。
 *
 * 共享模式说明（变化检测的语义前提）：源读取句柄以 FILE_SHARE_READ |
 * FILE_SHARE_WRITE 打开——§8.4 复制期间变化检测（V17）的存在前提就是
 * "复制窗口内源可被改写"；若拒绝共享写，该问题会转移为 LOCK-CONFLICT，
 * 与 §8.4 检测语义相悖（测试经进度检查点定点注入改写——V17）。
 *
 * 移动-only：实现承载唯一所有（unique_ptr 不完整类型——析构/移动在
 * ResourceIo.cpp 定义），拷贝会使双句柄指向同一 OS 句柄导致双重关闭
 * ——禁拷贝。
 */
class ResourceStreamHandle {
public:
    /// 空句柄（不绑定 OS 资源；read 返回 IO-FORMAT-INTERNAL——防御性）。
    ResourceStreamHandle() = default;
    ~ResourceStreamHandle();

    ResourceStreamHandle(ResourceStreamHandle&& other) noexcept;
    ResourceStreamHandle& operator=(ResourceStreamHandle&& other) noexcept;
    ResourceStreamHandle(const ResourceStreamHandle&) = delete;
    ResourceStreamHandle& operator=(const ResourceStreamHandle&) = delete;

    /// 是否绑定已打开的 OS 句柄。
    bool isOpen() const noexcept;

    /// 最终实体路径（weakly_canonical——打开句柄上复核所得；仅追溯提示）。
    const std::filesystem::path& finalPath() const noexcept;

    /// 打开时 stat 的文件大小（字节；提示性——实读数以各 read 累计为准）。
    std::uint64_t sizeBytes() const noexcept;

    /**
     * @brief 读取下一块（≤maxBytes；返回 0＝EOF）。
     *
     * @param dst      [out] 目标缓冲（非 null；容量 ≥ maxBytes——调用方
     *                 契约，违约即缓冲越界风险，实现以 null 检查兜底
     *                 IO-FORMAT-INTERNAL）
     * @param maxBytes [in]  本块上限（字节；0＝IO-FORMAT-INTERNAL 防御）
     *
     * @return 实读字节数（0＝EOF）；失败＝IO-RES-\*（读面 OS 错误四分类，
     *         §4.2.5）——句柄不持令牌，取消由调用方在块循环间轮询
     *         （本方法内无检查点）
     */
    IoResult<std::size_t> read(void* dst, std::size_t maxBytes);

    /**
     * @brief 实现侧装配入口（ResourceIo.cpp 内部使用——接管不完整类型
     *        的实现承载；外部无法构造 ResourceStreamImpl，伪造面为零）。
     *
     * @param impl [in] 已装配的实现承载（非 null——空承载＝防御性内部
     *             错误形态，实现侧不传入）
     * @return 接管所有权的句柄
     */
    static ResourceStreamHandle adopt(std::unique_ptr<ResourceStreamImpl> impl);

private:
    std::unique_ptr<ResourceStreamImpl> m_impl;     ///< 唯一所有权；移动转移、析构关闭
};

// =====================================================================
// ResourceOpenSpec（§9.6 原文承载——角色由调用方声明，io 不推断）
// =====================================================================

struct ResourceOpenSpec {
    /// 路径角色：P-1（用户源——一次性读取例外区，NFR-SEC-01）或 P-5/P-6
    /// （项目资源区/授权中转——受 SafePath 管辖，§4.1）。其余角色非法
    /// （→IO-FORMAT-INTERNAL 防御性拒绝——资源读取通道只有这三类角色）。
    PathRole role = PathRole::UserSource;

    /// 解析基点：P-5＝项目根（资源区 objects|catalog 的父——§4.3.3），
    /// P-6＝中转根（.staging/tmp——§4.1）；P-1 必须为空（§9.1 base 与
    /// 角色匹配契约）。
    std::filesystem::path base;
};

// =====================================================================
// 依赖树（§6.5——节点/边/环/缺失的资源图）
// =====================================================================

/**
 * @brief 依赖边种类（§6.5"边 = {include | mesh | material | texture |
 *        workcell-include}"；枚举序＝卡面列举序，表尾追加纪律同款）。
 */
enum class ResourceEdgeKind : std::uint8_t {
    Include,         ///< xacro:include／include（§6.2 include 解析——受 IncludeDepth/环检测管辖）
    Mesh,            ///< \<mesh filename="…"\>（URDF/Xacro 几何引用，§6.5）
    Material,        ///< OBJ mtllib（材质库引用，§6.5"边"行 material）
    Texture,         ///< MTL map_Kd（纹理引用，§6.5"边"行 texture）
    WorkcellInclude, ///< WorkCell XML include（R2 通道预留——§6.4，阶段 A 不产出此边）
};

/**
 * @brief 依赖树节点（§6.5"节点 = ResourceSnapshot{path(导入根相对),
 *        size, mtime, digest}"）。
 *
 * relPath 为导入根内相对键（正斜杠、折叠小写——稳定序与查重键；SP-5：
 * 相对键仅寻址/呈现，不作身份）。缺失叶（引用目标不存在）：exists=false
 * 且 snapshot=nullopt——节点仍入树（缺失清单的载体），并汇聚为
 * IO-RES-MISSING 失败（§6.5"节点缺席 → IO-RES-MISSING（缺失清单）"）。
 */
struct ResourceNode {
    std::string relPath;                        ///< 导入根相对键（正斜杠、折叠小写）
    bool exists = false;                        ///< false＝缺失叶（V26 缺失清单成员）
    std::optional<ResourceSnapshot> snapshot;   ///< 存在时的读取快照（摘要/大小/实体路径）
};

/**
 * @brief 依赖树边（§6.5——from 经 kind 引用 to）。
 */
struct ResourceEdge {
    std::string fromRel;        ///< 引用方相对键
    std::string toRel;          ///< 被引用方相对键（缺失叶也在边中——缺失事实入图）
    ResourceEdgeKind kind = ResourceEdgeKind::Include;
};

/**
 * @brief 资源依赖树（§6.5 产物——io 产出、modeling 消费做字段映射与草稿
 *        模型——§10.5）。
 *
 * 稳定序（§9.6 确定性行"依赖树遍历序＝字典序稳定"）：nodes 按 relPath
 * 折叠字典序；edges 按 (fromRel, toRel, kind) 字典序。rootRel 为入口
 * 文档的相对键。
 */
struct ResourceDependencyTree {
    std::string rootRel;                ///< 入口文档相对键
    std::vector<ResourceNode> nodes;    ///< 全部节点（含缺失叶）——折叠键字典序
    std::vector<ResourceEdge> edges;    ///< 全部边——(from,to,kind) 字典序
};

// =====================================================================
// IResourceReader（§9.6——无状态文件层读取服务）
// =====================================================================

/**
 * @brief 外部/项目内资源文件层读取接口（§9.6 原文四方法＋两处登记等价
 *        调整，见下）。
 *
 * 等价调整（§9.0"签名均为实现建议……实现期允许等价调整，语义不变"；
 * 正式登记随 io.md §15.5 v0.8）：
 *   1. snapshot/dependencyTree 增补尾随参数 `BudgetScopeId budgetScope
 *      = {}`——§9.3 CSV 通道同款（IO-T03 v0.5 先例）：调用方可传已收紧
 *      的 scope（多阶段共享 ledger——§4.5.2），缺省 0＝自开产品默认子
 *      scope（null guard 同理自管）。深度/网格计数类维度（IncludeDepth/
 *      MeshFaceCount 等）的生效限额从 scope 账本快照读取（BudgetGuard
 *      的 charge 是累计入账语义，深度不是累计量——比较判定在读取流程，
 *      限额来源仍是预算 scope，§4.4③"预算开户先行"）。
 *   2. dependencyTree 的 importRoot 参数承载**入口文档**（用户显式选择
 *      的 .urdf/.xacro/.xml 文件，P-1 角色）；include 解析的管辖根＝其
 *      父目录——§6.2"以导入根（用户选择的文件所在目录或显式声明的搜
 *      索根）为基解析"的直接落点（卡面 §9.6 参数名 importRoot 的语义
 *      澄清，不改变调用形态）。
 *
 * 防护序（§4.4 防护流程，逐通道强制）：② SafePath 规范化（角色规则）→
 * ③ 预算预检（先拒超限文件再打开——"③在④前"）→④ 打开（P-4/P-5 拒
 * 绝打开 reparse point＋final-path 复核 SP-6）→⑤ 流式读取（每块：预算
 * 入账＋取消检查点合并——"一次分支两查"）→⑥ 摘要（core ContentDigester）
 * →⑦ 快照产出。
 *
 * 契约要点（§9.6 契约表，逐行冻结）：
 *   - 后置：open 成功＝句柄过 SP-4/SP-6；snapshot 成功＝三元组完整；
 *     失败＝无部分产物（IoResult 值轨道默认构造）。
 *   - 错误：IO-SEC-PATH-*（族）、IO-SEC-SYMLINK、IO-SEC-BUDGET-*（族）、
 *     IO-RES-{NOT-FOUND,ACCESS-DENIED,READONLY,LOCK-CONFLICT}、
 *     IO-FORMAT-XML-CYCLE、IO-FORMAT-XML-SYNTAX、IO-FORMAT-MESH-UNKNOWN、
 *     IO-RES-MISSING、IO-CANCELLED（§9.6 错误类型行＋XML 语法码表尾
 *     追加——v0.8 登记）。
 *   - 副作用：只读文件系统＋诊断上报；无写（§9.6 副作用行）。
 *   - 非法调用：以 snapshot.digest 之外的任何东西（路径/mtime）作内容
 *     身份；用本接口读取 .rwdesign 内部对象（须经 §9.7 项目字节源注入）。
 */
class IResourceReader {
public:
    virtual ~IResourceReader() = default;

    /**
     * @brief 打开＋final-path 复核，返回流式读取句柄（§9.6 原文签名）。
     *
     * 预算挂钩（§9.6"返回流式读取句柄（预算挂钩）"的落点）：单文件大小
     * 预检（stat 入账 SingleFileBytes——"先拒超限文件再打开"）在 open 内
     * 完成；句柄逐块读取的 TotalBytes 入账由调用方循环驱动（句柄自身不
     * 持 guard——预算 scope 归读取会话）。
     *
     * @param path   [in] 资源路径（UTF-16 宽字符路径；P-1 任意本机/UNC
     *               路径——跟随链接，快照按实体路径）
     * @param spec   [in] 角色与基点（ResourceOpenSpec 契约见上）
     * @param budget [in] 预算守卫（null＝自管内部 guard，产品默认规格——
     *               §9.3 probe 同款等价调整）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝规范化/预检/
     *               打开各步前）
     *
     * @return 成功＝RAII 句柄；失败＝IO-SEC-PATH-*（族）、IO-RES-* 四分类、
     *         IO-SEC-BUDGET-FILE（预检）、IO-CANCELLED
     */
    virtual IoResult<ResourceStreamHandle>
        open(const std::filesystem::path& path, const ResourceOpenSpec& spec,
             IBudgetGuard* budget, IoCancelToken* cancel) = 0;

    /**
     * @brief 全读＋摘要，产出读取快照（§9.6/§8.2——变化检测基准）。
     *
     * 步骤：②规范化→③SingleFileBytes 预检（stat 大小先拒后开）→④打开
     * （SP-6 复核）→⑤分块读取（每块：TotalBytes 入账＋取消检查点）→
     * ⑥finalize→⑦快照（finalPath＝实体路径、sizeBytes＝实读总数、
     * mtimeUtc＝打开句柄 FILETIME、contentDigest）。
     *
     * 网格规模护栏（§6.3 规模预检行——identify 命中网格族时启用）：
     *   - 二进制 STL：84 字节头三角数×50＋84 与 SingleFileBytes 预检比对
     *     （超限即拒**不读体**）；声明面/顶点数与 MeshFaceCount/
     *     MeshVertexCount 限额比对——超限 IO-SEC-BUDGET-MESH（**读前**
     *     触发——V16"谎报三角形数即拒"）。实际大小复核（打开后按 stat
     *     反推真实面数）触发同码（读中复核）。
     *   - ASCII STL/OBJ：流式统计 facet／v/f 计数，累计超 MeshFaceCount/
     *     MeshVertexCount 限额即中止——IO-SEC-BUDGET-MESH（**读中**触发
     *     ——V16"真实超限"）。
     *
     * @param path        [in] 资源路径
     * @param spec        [in] 角色与基点
     * @param budget      [in] 预算守卫（null＝自管内部 guard）
     * @param cancel      [in] 取消令牌（null＝不可取消；检查点＝每块）
     * @param budgetScope [in] 已开预算 scope（缺省 0＝自开自关子 scope
     *                    ——等价调整 1）
     *
     * @return 成功＝快照三元组完整；失败＝§9.6 错误类型行（无部分产物）
     */
    virtual IoResult<ResourceSnapshot>
        snapshot(const std::filesystem::path& path, const ResourceOpenSpec& spec,
                 IBudgetGuard* budget, IoCancelToken* cancel,
                 BudgetScopeId budgetScope = {}) = 0;

    /**
     * @brief 格式识别（魔数/扩展名——§6.3；const，无取消点）。
     *
     * 只读首部字节（≤84 字节：二进制 STL 头三角数所需上限）＋扩展名
     * 嗅探。识别失败（扩展名与魔数均不在已登记格式族）→
     * IO-FORMAT-MESH-UNKNOWN，params 携带 head-hex（首 16 字节十六进制
     * 摘要——§6.3"脱敏无虞"）。
     *
     * @param path [in] 目标路径（不做存在性强校验——不可达→IO-RES-\*
     *             四分类）
     *
     * @return 识别结果（ResourceKind；Unknown 不对外返回）
     */
    virtual IoResult<ResourceKind> identify(const std::filesystem::path& path) const = 0;

    /**
     * @brief XML 依赖树（§6.5——include/mesh/material/texture 边＋循环
     *        检测＋缺失清单）。
     *
     * 遍历语义（§6.2/§6.5）：
     *   - 入口＝importRoot（入口文档，P-1 规范化）；管辖根＝其父目录
     *     （等价调整 2）——include 目标必须落在管辖根内，越界＝
     *     IO-SEC-PATH-ESCAPE（§6.2"导入根自身即临时管辖根"）。
     *   - 边抽取：XML 文档（expat 解析——P-IO-3 冻结选型）取局部名
     *     include（属性 filename|file|href）→Include 边、mesh（属性
     *     filename|file）→Mesh 边；OBJ 的 mtllib→Material 边、MTL 的
     *     map_Kd→Texture 边（引用相对**所在文件目录**解析，同样受管辖
     *     根约束）；WorkcellInclude 边 R2 预留不产出（§6.4）。
     *   - 循环：include 图有环→IO-FORMAT-XML-CYCLE，params/detail 携带
     *     **环路径清单**（V15 观测点）；遍历中即时终止（先于缺失汇总
     *     ——遍历序确定性）。
     *   - 深度：include 链深度（入口=1）超过 scope 生效 IncludeDepth
     *     限额→IO-SEC-BUDGET-INCLUDE（比较型三要素——V15"17 层深度链"
     *     触发面：默认 16）。
     *   - 缺失：引用目标不存在→缺失叶入树（exists=false）继续遍历，全
     *     图汇总后 IO-RES-MISSING 失败，params 携带 missing-count、
     *     detail 携带缺失清单（V26——资源事实，§2.5；无任何工程结论）。
     *   - 文件层护栏：每文件 SingleFileBytes 预检＋FileCount 入账＋每块
     *     TotalBytes＋每节点取消/进度检查点（progress：done＝已处理节点
     *     数、total=0 未知、stage 固定英文短语）。
     *
     * @param importRoot  [in] 入口文档路径（.urdf/.xacro/.xml——P-1）
     * @param budget      [in] 预算守卫（null＝自管内部 guard）
     * @param cancel      [in] 取消令牌（null＝不可取消；检查点＝每节点）
     * @param progress    [in] 进度回调（缺省空——§9.0 回调纯数据）
     * @param budgetScope [in] 已开预算 scope（缺省 0＝自开自关子 scope）
     *
     * @return 成功＝ResourceDependencyTree（稳定序）；失败＝
     *         IO-FORMAT-XML-CYCLE、IO-FORMAT-XML-SYNTAX、IO-SEC-*（族）、
     *         IO-RES-MISSING、IO-CANCELLED
     */
    virtual IoResult<ResourceDependencyTree>
        dependencyTree(const std::filesystem::path& importRoot,
                       IBudgetGuard* budget, IoCancelToken* cancel,
                       IoProgressCallback progress = {},
                       BudgetScopeId budgetScope = {}) = 0;
};

/// 读取服务指针别名（§9.11 IoRuntime::resourceReader() 访问器产物形态）。
using IResourceReaderPtr = std::shared_ptr<IResourceReader>;

/**
 * @brief 创建资源读取服务（IoRuntime 装配入口的实现侧工厂——§9.11
 *        resourceReader() 访问器产物）。
 *
 * @param resolver [in] SafePath 解析器（缺省空＝内部以产品规则集自建
 *                 ——makeSafePathResolver()）；注入共享实例可实现与
 *                 其他通道一致的规则面
 * @return 无状态并发安全读取服务（进程级共享由调用方持有）
 */
IResourceReaderPtr makeResourceReader(ISafePathResolverPtr resolver = {});

// =====================================================================
// 外部引用记录与检测事实（§8.3/§8.5——段②协作面）
// =====================================================================

/**
 * @brief 外部引用记录（§8.5 ExternalResourceRecord 的 io 侧读取形态；
 *        持久层归 project DraftService——io 经 IExternalRefSource 注入
 *        读取，不直读项目存储——§2.3 非目标 6）。
 *
 * PM-01 段②语义（ARCH §6.6 R4）：外部引用记录＝{绝对路径＋内容哈希}——
 * **不以裸路径充当项目资源引用**（路径仅寻址与追溯；身份是哈希）。本
 * 结构即该二元组＋id＋提示性大小。
 *
 * 生命周期/所有权：纯值类型；IExternalRefSource 按值返回，调用方所有。
 */
struct ExternalRefRecord {
    IoString externalRefId;             ///< 引用记录 id（project 命名空间；io 透传不解释）
    std::filesystem::path absPath;      ///< 外部源绝对路径（P-1 读取；路径不作身份）
    core::Digest256 recordedDigest{};   ///< 登记时内容摘要（SHA-256；检测判据）
    std::uint64_t recordedSizeBytes = 0;///< 登记时大小（字节；快速预筛提示——非权威）
};

/**
 * @brief 外部源检测事实三态（§8.3/§9.8 上报形态 ExternalRefProbe::
 *        Ok|Missing|Changed）。
 *
 * 正交纪律（§2.5/§6.6）：三态全部是**资源事实**——Missing 不产生任何
 * "工程不可行"结论（evidence 门禁按 CON-03 判证据不足；用户重关联归
 * PM-09/project 命令）。
 */
enum class ExternalRefState : std::uint8_t {
    Ok,       ///< 未变（digest 相同——size/mtime 不同仅提示性注记，§8.3"防 touch 误报"）
    Missing,  ///< 实体路径不可达（NOT_FOUND/ACCESS_DENIED 细分记录于 ProbeDetail 注记）
    Changed,  ///< 重算 digest 与记录不符（IO-D10 权威判据）
};

/**
 * @brief probe 细节产出（§9.8 out 参数载体——比较型诊断的原材料）。
 *
 * 生命周期/所有权：纯值类型；调用方传入 optional 容器，io 仅在命中
 * Changed/Ok 时填充 actual（Missing 时留空——目标不可得无快照可产）。
 */
struct ProbeDetail {
    ResourceSnapshot actual;            ///< 实际读取快照（Ok/Changed 时填充；Missing 时空）
    std::vector<std::string> notes;     ///< 事实注记（稳定英文短语：os-error 细分/预筛提示——不产业务结论）
};

// =====================================================================
// 固化执行结果（§9.8——§8.4 io 执行侧产物；发布归 project）
// =====================================================================

/**
 * @brief 固化中转复制结果（§9.8 原文四字段——project 侧组装
 *        SolidifyResult 的输入，project.md §5.7 已冻结形状）。
 *
 * 生命周期/所有权：纯值类型；中转副本文件在成功时**保留**于 stagingDir
 * 供 project 入对象库（§8.4 步骤 7——发布与 objects 写入归 project，
 * io 不写 objects/——§9.8 副作用行"不写 objects/"）；project 消费后的
 * 中转清理归 TempArea（IO-T06/project 编排——§8.4 步骤 8 成功清理段）。
 */
struct SolidifyStagingResult {
    ResourceSnapshot before;                 ///< 前置快照 A（§8.4 步骤 2——复制前全读）
    ResourceSnapshot after;                  ///< 复制后快照 B（步骤 4——重读源文件）
    core::Digest256 copiedDigest{};          ///< 副本字节复算摘要（步骤 6——传输完整性）
    bool sourceChangedDuringCopy = false;    ///< 步骤 5 判定：A/B digest 或 size 不同
};

// =====================================================================
// IResourceSnapshotter（§9.8——段②检测＋段③固化执行）
// =====================================================================

/**
 * @brief 快照/探测/固化执行接口（§9.8 原文两方法＋两处登记等价调整，
 *        见下）。
 *
 * 等价调整（§9.0 条款；正式登记随 io.md §15.5 v0.8）：
 *   1. probe 返回类型由裸 ExternalRefState 改为 IoResult<ExternalRefState>
 *      ——资源事实三态走值轨道，探测过程的**环境失败**（取消 IO-CANCELLED
 *      /预算 IO-SEC-BUDGET-\*）走错误轨道；裸枚举无法区分"目标是 Missing
 *      事实"与"探测本身被取消"——事实与状态混同会诱导调用方把取消误报
 *      为资源缺失（§2.5 正交纪律）。事实语义不变。
 *   2. solidifyToStaging 增补尾随参数 BudgetScopeId budgetScope = {}（与
 *      §9.6 同款 scope 共享通道——§8.4 步骤 1"三段共用 ledger"）；
 *      budgetBytes=0 语义＝未声明调用方预算（按产品默认生效——§4.5.2
 *      "budgetBytes 与产品默认取小"的 0 值退化为不收紧）。
 *
 * 契约要点（§9.8 契约表，逐行冻结）：
 *   - 前置：probe 的 record.recordedDigest 非全零（全零＝调用方契约
 *     违约，返回 Missing＋注记——§1.4 非抛出约束下的防御性事实上报）；
 *     solidify 的 stagingDir 为 project 授权中转位（P-6 规范通过）。
 *   - 后置：solidify 成功＝中转副本完整＋sourceChangedDuringCopy=false
 *     ＋digests 一致；变化检出＝中转已清理＋失败返回（V17"无部分副本"）。
 *   - 副作用：读源＋写中转（P-6 唯一写点）＋失败清理；**不写 objects/**
 *     （发布归 project——N-7/§9.8 副作用行）。
 *   - 非法调用：期待 io 判定"是否允许正式报告"（归 evidence）；跳过
 *     staging 直接写 objects/（卡行禁止项——直接创建 CanonicalModel/
 *     objects 写入禁止）。
 */
class IResourceSnapshotter {
public:
    virtual ~IResourceSnapshotter() = default;

    /**
     * @brief 段②检测（§8.3——Missing/Changed/Ok 纯事实）。
     *
     * 算法（§8.3 表逐行）：实体路径不可达→Missing（NOT_FOUND/
     * ACCESS_DENIED 细分记录于 ProbeDetail 注记——不降级误报，§4.2.5 四
     * 分类分码在 IoError 轨道，此处以注记承载细分）；可达→全读重算
     * digest（权威判据——IO-D10；size 预筛只作"必疑"提示，不短路 digest
     * 比对）→不符 Changed／相符 Ok。
     *
     * @param record [in] 外部引用记录（recordedDigest 全零＝违约，见上）
     * @param budget [in] 预算守卫（null＝自管内部 guard——重读受
     *                SingleFileBytes 上界约束，§9.7 同款）
     * @param cancel [in] 取消令牌（null＝不可取消；检查点＝每块）
     * @param out    [in,out] 细节产出容器（缺省 null＝不要细节）
     *
     * @return 值轨道＝三态事实；错误轨道＝IO-CANCELLED/IO-SEC-BUDGET-\*
     *         （等价调整 1——环境失败不是资源事实）
     */
    virtual IoResult<ExternalRefState>
        probe(const ExternalRefRecord& record, IBudgetGuard* budget,
              IoCancelToken* cancel, std::optional<ProbeDetail>* out = nullptr) const = 0;

    /**
     * @brief 段③固化执行——io 执行侧（§8.4 步骤 1~6；project 编排/发布）。
     *
     * 步骤（§8.4 原文）：
     *   1. 预算开户：budgetBytes 与产品默认取小（0＝不收紧）；三段共用
     *      ledger（调用方 scope 或自开子 scope）。
     *   2. 前置快照 A＝snapshot(source)（P-1 例外区——stat＋SHA-256 全读）。
     *   3. 复制到 stagingDir（P-6；io 只写 project 授权中转位——目录不
     *      存在则创建；目标名＝源文件名）。
     *   4. 复制后快照 B＝snapshot(source)（重读源文件）。
     *   5. 比对 A/B：digest 或 size 不同→sourceChangedDuringCopy 置位→
     *      清理中转→IO-RES-CHANGED 失败（params 携带 recorded(before)/
     *      actual(after) 摘要比较对——V17 观测面）。
     *   6. 副本字节复算 digest（与 A 一致——传输完整性）；不符同视为
     *      复制窗口变化（同步骤 5 处置）。
     *   7~8. （project 侧）入对象库/发布/成功清理——归 project 编排，
     *      本接口不执行（§10.1"io 产物是命令输入"）。
     *
     * 失败清理（§9.8 后置行）：任何步失败→删除本接口创建的中转副本与
     * 中转目录（V17"中转目录不存在/无部分副本"）——stagingDir 是
     * solidify-\<id\> 会话目录（§7.5），io 按会话属主清理；清理失败保留
     * 残留并附 IO-PACK-CLEANUP-FAILED 注记级错误（残留不可误识别——
     * §7.6 同源纪律）。
     *
     * @param source      [in] 外部源文件（P-1——用户显式选择/重关联后路径）
     * @param stagingDir  [in] project 授权中转目录（.staging/tmp/
     *                    solidify-\<id\>/——P-6）
     * @param budgetBytes [in] 调用方预算上限（字节；0＝未声明→产品默认）
     * @param budget      [in] 预算守卫（null＝自管内部 guard）
     * @param cancel      [in] 取消令牌（null＝不可取消；复制每块检查点，
     *                    取消清理中转——§9.8 取消行为行）
     * @param progress    [in] 进度回调（done/total＝已复制/总字节；stage
     *                    固定英文短语——V17 经此检查点定点注入）
     * @param budgetScope [in] 已开预算 scope（缺省 0＝自开自关——等价调整 2）
     *
     * @return 成功＝SolidifyStagingResult（副本保留待 project 入库）；
     *         失败＝IO-RES-CHANGED、IO-RES-*（四分类）、IO-SEC-*（族）、
     *         IO-CANCELLED（中转已清理或残留已登记）
     */
    virtual IoResult<SolidifyStagingResult>
        solidifyToStaging(const std::filesystem::path& source,
                          const std::filesystem::path& stagingDir,
                          std::uint64_t budgetBytes,
                          IBudgetGuard* budget, IoCancelToken* cancel,
                          IoProgressCallback progress = {},
                          BudgetScopeId budgetScope = {}) = 0;
};

/// 快照/固化服务指针别名（§9.11 IoRuntime::snapshotter() 访问器产物形态）。
using IResourceSnapshotterPtr = std::shared_ptr<IResourceSnapshotter>;

/**
 * @brief 创建快照/固化服务（IoRuntime 装配入口的实现侧工厂——§9.11
 *        snapshotter() 访问器产物）。
 *
 * @param resolver [in] SafePath 解析器（缺省空＝内部以产品规则集自建）
 * @return probe 并发安全；solidifyToStaging 会话级（中转区互斥——§9.8
 *         线程约束行，实现内部加锁串行化）
 */
IResourceSnapshotterPtr makeResourceSnapshotter(ISafePathResolverPtr resolver = {});

// =====================================================================
// runtime 资源读取桥（§9.7——P-IO-1 注入面＋P-IO-2 生命周期裁决落点）
// =====================================================================

/**
 * @brief 对象库字节视图（§9.7 原文 ProjectBytesView——tryObjectBytes 的
 *        产出载体）。
 *
 * 缓冲归注入源（project 侧）所有：对象库内容不可变（§8.5 固化副本），
 * 注入源可安全返回指向其稳定存储的视图；调用方（adapter）同步消费并把
 * 需要跨调用保留的字节拷入自身缓存（视图本身不构成跨调用持有契约——
 * §8.6 调用方纪律同源）。digest 为 project 侧已校验的副本摘要（§9.7
 * "含摘要校验后的不可变副本"）。
 */
struct ProjectBytesView {
    const std::uint8_t* data = nullptr;   ///< 对象字节（归注入源；同步消费）
    std::size_t size = 0;                 ///< 字节数（单位：字节）
    core::Digest256 digest{};             ///< 副本摘要（project 侧已校验——SHA-256 唯一算法）
};

/**
 * @brief 对象库只读字节注入源（§9.7 原文——io 定义、project 在 L5 装配
 *        期实现并注入；io 零 project 编译依赖——P-IO-1 处置）。
 *
 * 注入契约（io 自有接口的权限内细则——P-PR-5 待裁决不影响本面）：
 *   - contentVersion 为全零（空保留值）＝由 project 侧解析当前
 *     materialized 版本（对象库不可变性保证取到即固化版本——§8.5）；
 *     非零＝按指定版本取。
 *   - 未命中（resourceId 不是本上下文的已固化对象——可能为 Recorded
 *     外部引用）＝IoError{IO-RES-NOT-FOUND}（adapter 据此转投
 *     IExternalRefSource 路由，§9.7 路由注释）；其他错误码原样上抛。
 *
 * 所有权：注入源由 project/L5 持有；io 不接管、不释放（借用的裸指针
 * 须在 adapter 生存期内有效——§9.0 所有权行）。
 */
class IProjectBytesSource {
public:
    virtual ~IProjectBytesSource() = default;

    /**
     * @brief 取对象库只读字节（§9.7 原文签名）。
     *
     * @param objectId       [in] 资源对象身份（project 分配——§8.5）
     * @param contentVersion [in] 内容版本（全零＝当前 materialized 版本
     *                       ——注入契约见上）
     *
     * @return 字节视图（data/size/digest；缓冲归注入源——调用方同步
     *         消费）；失败＝IO-RES-NOT-FOUND（未命中）或其他 io 码
     */
    virtual IoResult<ProjectBytesView> tryObjectBytes(
        const core::ObjectId& objectId, const core::ContentVersion& contentVersion) const = 0;
};

/**
 * @brief 草稿外部引用记录注入源（§9.7 原文——project DraftService 持久
 *        层的 io 侧读取面；P-IO-1 注入式协作）。
 *
 * id 文本形态由 project 注入侧解释（io 透传 canonical 文本，不做前缀
 * 拼接/剥离——R-4）；未命中＝IoError{IO-RES-NOT-FOUND}（adapter 判
 * Missing 事实的依据）。
 */
class IExternalRefSource {
public:
    virtual ~IExternalRefSource() = default;

    /**
     * @brief 按引用 id 取外部引用记录（§9.7 原文签名）。
     *
     * @param externalRefId [in] 引用记录 id（adapter 传 resourceId 的
     *                      canonical 文本）
     *
     * @return 记录值；失败＝IO-RES-NOT-FOUND（非记录 id）或其他 io 码
     */
    virtual IoResult<ExternalRefRecord> tryRecord(const IoString& externalRefId) const = 0;
};

/**
 * @brief runtime 侧字节视图（§9.7"返回 runtime::ResourceBytes{data,
 *        size,digest}（生命周期=§8.6 裁决）"的 io 侧载体）。
 *
 * 为什么不是 runtime 类型：P-IO-1 处置——ARCH §3.5 未登记 io→runtime
 * 编译边，io 公共头不得引用 runtime 类型；L5 装配层把本视图（连同
 * status）转译为 runtime::ResourceBytes（转译是纯机械包装，P-IO-1 补登
 * 直连边后收敛为薄壳，本头零改动——IO-D02）。生命周期按 §8.6 五条裁决
 * 执行（详见类头 IRuntimeResourceAdapter 注）。
 */
struct ResourceBytesView {
    const std::uint8_t* data = nullptr;    ///< 字节缓冲（归 provider——调用方同步消费）
    std::size_t size = 0;                  ///< 字节数（单位：字节）
    core::Digest256 digest{};              ///< 内容摘要（SHA-256；入 CanonicalModel 身份——CON-05）
    std::uint32_t accessVersion = kAccessVersion;   ///< 读取契约版本（§8.6——当前 1）
};

/**
 * @brief 读取失败四分（runtime.md §8.6 四分的 io 侧镜像；L5 转译为
 *        runtime 码——io 原始码嵌 error.detail）。
 *
 * Ok 之外的映射（§9.7 错误类型行＋§8.3"不可达细分"）：
 *   - Missing＝资源不可达（NOT-FOUND/ACCESS-DENIED/READONLY/LOCK 等环境
 *     不可达均归此——§8.3 Missing 行）或引用记录/对象均未命中；
 *   - Changed＝Recorded 路径重算 digest 与记录不符（S10 复查语义）；
 *   - Budget＝读取触预算上限（SingleFileBytes/TotalBytes）。
 */
enum class ResourceReadStatus : std::uint8_t {
    Ok,       ///< 字节就绪（bytes 有效）
    Missing,  ///< 不可达/未命中（细分见 error）
    Changed,  ///< Recorded 内容与记录不符（重算 digest 判据）
    Budget,   ///< 预算上限（error 携带三要素）
};

/**
 * @brief tryResourceBytes 的完整产出（状态＋视图＋io 原始错误）。
 */
struct ResourceReadResult {
    ResourceReadStatus status = ResourceReadStatus::Missing;   ///< 四分状态
    ResourceBytesView bytes;    ///< status==Ok 时有效（生命周期＝§8.6 裁决）
    IoError error;              ///< status!=Ok 时携带 io 原始码（Ok 时为默认值）
};

/**
 * @brief runtime 资源读取桥接口（§9.7 桥接说明的 io 侧契约——
 *        IRuntimeResourceProvider 的 io 实现核；P-RT-6/P-IO-2 落点）。
 *
 * 路由（§9.7 注释原文）：
 *   tryResourceBytes(resourceId)：
 *     ├─ Solidified → IProjectBytesSource（缓存 {resourceId,accessVersion}
 *     │               →字节＋digest——不可变保证缓存安全，§8.6.4）
 *     └─ Recorded   → IExternalRefSource → P-1 读取（每次重读＋重算
 *                     digest，绝不跨调用缓存——§8.6.4，S10 复查前提）
 *
 * Solidified 判定＝IProjectBytesSource 未命中（IO-RES-NOT-FOUND）时转投
 * IExternalRefSource；两边都未命中＝Missing。Recorded 读取重算 digest 与
 * record.recordedDigest 不符＝Changed（bytes 不返回——内容已不可信，
 * 重关联/固化流程接管，§8.3）。
 *
 * 生命周期五条裁决的执行承诺（§8.6，acceptance 5——测试逐条钉住）：
 *   1. 缓冲自返回起**至少**有效至同一实例下一次调用；实际保证**至 provider
 *      析构**（Recorded 缓冲入稳定区不再回收；Solidified 缓存条目随实例
 *      存续）。
 *   2. 调用方纪律：同步消费、不跨调用长期持有（裁决原文；文档面约束）。
 *   3. 并发只读安全：内部互斥仅护稳定区/缓存的插入；返回的缓冲发布后
 *      只读，并发视图无数据竞争（Recorded 每次独立缓冲；Solidified 共享
 *      不可变缓冲——"绝不复用仍在暴露中的缓冲"的可判形式：缓冲一经
 *      暴露永不改写、永不释放至析构）。
 *   4. Recorded 绝不缓存：每次调用重读＋重算 digest（§8.5——保证 runtime
 *      S10 摘要复查能发现替换）；Solidified 允许缓存（不可变）。
 *   5. 值拷贝兼容：未来改值拷贝语义不弱于本裁决（无需消费方变更）。
 *
 * 线程约束：并发只读安全（§9.7 线程约束行）；取消：不接受取消令牌
 * （runtime 契约——§9.7 取消行为行）；会话级生命周期（随项目存储上下文；
 * 析构释放全部暴露缓冲——§8.6 生效域终点）。
 */
class IRuntimeResourceAdapter {
public:
    virtual ~IRuntimeResourceAdapter() = default;

    /**
     * @brief 按资源 id 取已验证字节（§9.7 路由语义——见类注）。
     *
     * 前置（§9.7）：两个注入源已绑定；resourceId 为 project 已分配对象
     * 或已登记外部引用。后置：成功＝字节已过 SafePath（Recorded 路径）/
     * 来源不可变（Solidified）＋预算＋digest 复算一致（Recorded 对比记录
     * ——不符即 Changed）。
     *
     * @param resourceId [in] 资源对象 id（core::ObjectId——引用键）
     *
     * @return 四分状态＋视图＋io 原始错误（ResourceReadResult）
     */
    virtual ResourceReadResult tryResourceBytes(const core::ObjectId& resourceId) = 0;
};

/// 桥接适配器指针别名（§9.11 makeRuntimeResourceAdapter() 桥接产物形态）。
using IRuntimeResourceAdapterPtr = std::shared_ptr<IRuntimeResourceAdapter>;

/**
 * @brief 创建 runtime 资源读取桥（IoRuntime 桥接产出——§9.11
 *        makeRuntimeResourceAdapter()）。
 *
 * @param projectBytes [in] 对象库字节注入源（project 实现——借用；可空
 *                     ＝Solidified 通道禁用，全部走 Recorded）
 * @param externalRefs [in] 外部引用记录注入源（project 实现——借用；可
 *                     空＝Recorded 通道禁用，未命中对象即 Missing）
 * @param resolver     [in] SafePath 解析器（缺省空＝内部自建——Recorded
 *                     路径 P-1 规范化用）
 *
 * @return 并发只读安全适配器（§8.6.3）；两注入源在其生存期内须有效
 *         （io 不接管所有权——§9.0）
 */
IRuntimeResourceAdapterPtr makeRuntimeResourceAdapter(IProjectBytesSource* projectBytes,
                                                      IExternalRefSource* externalRefs,
                                                      ISafePathResolverPtr resolver = {});

} // namespace sdurws::ird::io

#endif // SDURWS_IRD_IO_RESOURCEIO_HPP
