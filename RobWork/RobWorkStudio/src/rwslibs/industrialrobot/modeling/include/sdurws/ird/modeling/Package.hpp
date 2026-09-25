/**
 * @file   Package.hpp
 * @brief  规范模型包导出/导入（IModelPackagePort）——MDL-20 的"自有工件
 *         保真回读"通道：ZIP 封装＋manifest（来源标识/对象清单/摘要）＋
 *         根/部件对象 canonical 字节＋Solidified 资源副本＋命名位姿；
 *         导出原子写出、失败恢复先前输出；导入仅识别本软件工件；另含
 *         WorkCell/DWC XML 外供导出编排面（卡 §6.8）。
 *
 * 设计依据：
 *   - units/modeling.md §6.8（规范模型导出/导入 roundtrip：包形态＝ZIP 封装
 *     ＋manifest.json〔producer=ird-modeling、schemaVersion、对象清单与
 *     digest〕＋根/部件对象 canonical 字节＋Solidified 资源副本＋命名位姿；
 *     原子写出经 io AtomicFile——导出失败保证项目状态不变且旧输出文件不被
 *     破坏；导入仅识别本软件导出的规范工件，其他文件→稳定诊断引导至
 *     MDL-18/R2 通道；WC/DWC XML 外供＝数据源为 RuntimeSnapshot 只读视图、
 *     modeling 仅编排写出〔io AtomicWriter〕不改造内容）、§3.3（公共头表
 *     Package.hpp 行——T13）、§9.4.9（IModelPackagePort 行签名要点：
 *     exportPackage/importPackage；非本软件工件→引导 MDL-18/R2 稳定诊断）、
 *     §9.6（资源引用关系图——Recorded 只在外部引用记录层、Solidified 指向
 *     闭包内 resource 对象）、§3.4（纯函数服务确定性总约定）
 *   - units/io.md §7.1（ZIP 容器层契约——本单元导入回读经 IZipChannel 消费
 *     IO-T04 落位面）、§4.6（IAtomicFileWriter 原子写出协议）、§9.10（两设施
 *     接口）、§5.9.3（canonical JSON——manifest 字节的确定性编码与摘要）、
 *     §10.5（与 modeling 接入：MDL-20 原子写出与自有工件读取；包格式为
 *     modeling 自有工件，与 .rwpack 传输封装不同物——不混用其格式契约）
 *   - 需求 MDL-20（规范模型包导出/导入 roundtrip、导出失败恢复先前输出）、
 *     AT-28/V-29（roundtrip 逐项一致；文件层观测）、MDL-15/17（命名位姿在
 *     语义保留范围）、CON-01/PA-2（canonical 字节为权威——包内逐字节携带，
 *     不重编码）、NFR-COR-01/02（确定性：同闭包＋同目标→同包字节）、
 *     NFR-MNT-01（零 Qt）、SA-12（SHA-256 唯一摘要算法——经 core
 *     ContentDigester）
 *   - 任务契约 tasks/foundation/WP-13-T13.json acceptance 1~5；
 *     knownPitfalls P-MDL-8 处置（io Package/AtomicFile 面按 io.md v0.9
 *     现状消费——本头消费的 IZipChannel/IAtomicFileWriter/Json 设施均为
 *     io 已落位公共面，冻结后如签名漂移按卡 R-MDL-1 增量同步）
 *
 * 背景说明（三个通道的边界，勿混）：
 *   ① 规范模型包（本头，MDL-20）：modeling 自有工件——承载建模对象
 *      canonical 字节的保真回读通道，格式契约由本单元登记（manifest 常量
 *      见下）；与 io .rwpack 项目传输封装（io.md §7）是不同物，不复用其
 *      rwpack.json/payload 布局。
 *   ② MDL-18 WorkCell 反向导入（Import.hpp mapWorkCellXml）：R2/阶段 D
 *      通道——本头的 importPackage 遇到非本软件工件时以
 *      MDL-IMPORT-PACKAGE-UNKNOWN 稳定诊断**引导**用户走该通道，绝不
 *      代替其解析（R1 NotImplemented 边界不受本任务影响）。
 *   ③ WC/DWC XML 外供导出（exportWorkCellXml）：会话级文件操作——数据源
 *      是 runtime 快照的只读视图（消费已应用修订的编译产物，零修订、零
 *      失效、不私设第二编译路径），modeling 只编排"取只读数据→序列化→
 *      经 io AtomicWriter 原子落盘"，不改内容、不回写项目。
 *
 * 导出原子性的承载（MDL-20"恢复先前输出"的文件层语义）：完整 ZIP 字节在
 * 内存装配后，经 io 临时区（ITempAreaManager PackExport 会话——§7.5）暂存
 * 并以 IZipChannel 重开自检（清单哈希全绿——V-29 预提交校验），随后经
 * IAtomicFileWriter "prepare（目标旁暂存）→写入→commit（Windows 同卷原子
 * 替换）"落盘；任何一步失败即清理暂存——目标路径零接触，先前输出完整保
 * 留。替换是 Windows 同卷原子操作（io.md §4.6），不存在"半截包"可观测态。
 *
 * ZIP 容器实现说明（为什么 modeling 自写容器字节而不用 libzip 写侧）：
 * 包写侧只需 STORED（无压缩）条目的最小 APPNOTE 容器（本地头＋中央目录
 * ＋EOCD，CRC-32 容器校验，UTF-8 名标志位，固定时间戳——确定性），百行
 * 以内且完全在本单元文件权属内；libzip 写侧属 io 私有实现（Package.cpp），
 * io 公共面无 ZIP 写出器，而给 modeling 新增 libzip 链接属未登记第三方
 * 依赖（DTB §4 通道，所有者裁决）——故本实现自写容器字节，读侧（导入回
 * 读与导出自检）统一消费 io IZipChannel（libzip 解析，IO-T04 落位面），
 * 形成"写已知、读权威"的闭环：本单元产出的容器必须被 libzip 逐字节读回
 * 验收。CRC-32 是 ZIP 容器格式自带的传输校验（APPNOTE 规范字段），不是
 * 内容身份——内容身份唯一是 SHA-256（manifest 条目 sha256 与
 * contentDigest，经 core ContentDigester，SA-12 不受影响）。
 *
 * 线程安全：ModelPackagePort 无共享可变状态、可重入、多线程并发调用安全
 * （卡 §3.4 总约定 1）。确定性：不读环境变量/时钟/locale/文件系统随机性
 * （manifest 的 createdAtUtc 由调用方传入，服务自身不取时钟——§9.0 确定注
 * 口径）；同闭包＋同目标→同包字节；数值文本经 std::to_chars 最短往返
 * （locale 无关）。全 SI（m/rad/kg/N·m）；角度 rad（AGENTS §2.5）。
 */

#ifndef IRD_MODELING_PACKAGE_HPP
#define IRD_MODELING_PACKAGE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // core::DiagnosticRecord（diags 输出参数元素类型）
#include <sdurws/ird/core/Digest.hpp>       // core::Digest256（manifest 摘要/内容身份）
#include <sdurws/ird/io/AtomicFile.hpp>     // io::ReplacePolicy（导出替换策略——IO-T06 落位面）
#include <sdurws/ird/modeling/CanonicalBridge.hpp>  // ObjectClosureView（导出数据源——闭包域字节源）
#include <sdurws/ird/modeling/Codec.hpp>    // ObjectVariant/kCurrentFormatVersion（对象编解码）
#include <sdurws/ird/modeling/Errors.hpp>   // ModelingError（错误面）
#include <sdurws/ird/modeling/Import.hpp>   // ValidatedSource（导入输入契约——io 产物组合）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // RobotDesign（根对象值模型）

// exportWorkCellXml 的参数类型（ACC5——声明面仅 const& 依赖，前置声明即可；
// 完整类型由实现 TU 与调用方各自 include runtime/Snapshot.hpp，公共头不
// 拖入 runtime 重 include——CanonicalBridge 消费 Description 同款克制面）。
namespace sdurws::ird::runtime {
class RuntimeSnapshot;   // 运行时快照（§9.1——WC/DWC 只读视图的持有者）
}

namespace sdurws::ird::modeling {

// =====================================================================
// 包格式常量（本单元登记的格式契约——manifest 字段值与条目路径规则；
// 修改即包格式破坏性变更，走单元卡增量修订）
// =====================================================================

/// 包格式标识（manifest.formatId——导入侧第一道门：非本格式即拒）。
inline constexpr std::string_view kModelPackageFormatId = "ird-model-package";

/// 来源标识（manifest.producer——§6.8"manifest 来源标识"登记值；仅本软件
/// 导出的工件才被 importPackage 接受，MDL-20）。
inline constexpr std::string_view kModelPackageProducer = "ird-modeling";

/// 包格式 schemaVersion（manifest.schemaVersion；当前 1。大于本值＝未来
/// 版本包→SchemaVersionUnsupported 值面拒绝——升级程序；不产诊断，该码
/// 语义是"对象 schema 主版本"，包格式版本不入 MDL-READINESS-SCHEMA-
/// UNSUPPORTED 语义，见实现注）。
inline constexpr std::int64_t kModelPackageSchemaVersion = 1;

/// manifest 在包内的条目名（包内唯一固定名；其余条目全部在对象/资源目录下）。
inline constexpr std::string_view kModelPackageManifestEntry = "manifest.json";

// =====================================================================
// 导出：目标与结果（§9.4.9 exportPackage 的参数/返回承载）
// =====================================================================

/**
 * @brief 规范包导出目标（§6.8"导出"的调用方输入）。
 *
 * 生命周期/所有权：纯值类型，调用方所有。
 */
struct PackageExportTarget {
    /// 包文件发布目标（如 <name>.irdbundle；父目录必须已存在——io 原子
    /// 写出器不代建目录，io.md §4.6 前置）。路径敏感值：不入诊断明文。
    std::filesystem::path targetFile;

    /// 替换策略（缺省 OverwriteAtomic＝导出默认——用户已选定目标路径，
    /// io.md §9.9 PackageExportOptions::replace 缺省同源；仍是原子替换，
    /// 失败时先前输出保留）。NeverOverwrite＝目标已存在即拒绝。
    io::ReplacePolicy replace = io::ReplacePolicy::OverwriteAtomic;

    /// manifest.createdAtUtc（ISO-8601 文本，调用方传入——服务不取时钟，
    /// NFR-COR-02 确定性口径；空串＝manifest 省略该字段）。仅作记录，
    /// 不参与任何判定（io.md §9.0 确定性注同源口径）。
    std::string createdAtUtcIso8601;
};

/**
 * @brief 规范包导出结果（§9.4.9 行"ExportOutcome"的落位命名——Import.hpp
 *        已有 ImportOutcome（URDF 映射报告载体），本头以 Package 前缀避免
 *        同命名空间冲突，登记于单元卡 §15 增量修订）。
 *
 * ok=false 时仅 error 有意义；ok=true 时计数与摘要字段有效。
 */
struct PackageExportOutcome {
    /// 是否成功（true＝包已原子就位且预提交自检通过）。
    bool ok = false;

    /// 失败承载（ok=false 时有效；ok=true 时为缺省值）。
    ModelingError error{};

    /// 包内条目数（含 manifest.json 自身——对象/资源条目数＋1）。
    std::uint64_t entryCount = 0;

    /// 包内条目未压缩字节累计（含 manifest；与 manifest entries size 和一致）。
    std::uint64_t totalBytes = 0;

    /// manifest canonical 字节的 SHA-256（io digestCanonicalJson 同源——
    /// 与包内 manifest 自校验闭环：导入侧重算同值）。
    core::Digest256 manifestDigest{};
};

// =====================================================================
// roundtrip 逐项核对清单（V-20 核对形态——"逐项清单入导入报告"）
// =====================================================================

/**
 * @brief roundtrip 逐项核对条目（§6.8"roundtrip 后逐项验证一致……逐项
 *        清单入导入报告，对照 §10 行 V-20"的条目形态）。
 *
 * 五组语义（§6.8 行文逐组）：authority＝权威参数化（模式＋DH/显式逐关节）；
 * physics＝物性（连杆质量/质心/惯量/材料＋工具物性）；resource＝资源引用
 * （Recorded/Solidified 状态与 contentDigest）；collision＝碰撞规则（连杆
 * 碰撞几何引用＋场景对象碰撞画像提示）；pose＝命名位姿（位姿集逐条目）。
 *
 * valueText 为稳定文本（数值 std::to_chars 最短往返、不经 locale；摘要为
 * 小写 hex）——同一语义对象两侧产出逐字节相同文本，清单整体相等⇔五组
 * 逐项一致（diff=空的机器判据，NFR-COR-02）。
 *
 * ★ 保真范围＝Codec 编码权威语义（§4.8 canonical 序列化承载的全部字段）；
 * LinkEntry.selfCollisionHints 为导入中间产物、不入编码权威（§4.3-B 表行
 * 原文"设计使然，非缺陷"），故不在清单也不在保真范围。
 */
struct PackageCheckItem {
    std::string group;      ///< 五组词表：authority|physics|resource|collision|pose
    std::string path;       ///< 对象内定位（如 "root.joints[0].dh.theta"、"tool[<oid>].mass"）
    std::string valueText;  ///< 值稳定文本（数值附单位：m/rad/kg/kg·m²；摘要为小写 hex）

    bool operator==(const PackageCheckItem& o) const noexcept
    {
        return group == o.group && path == o.path && valueText == o.valueText;
    }
    bool operator!=(const PackageCheckItem& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 构造根＋部件对象集的 roundtrip 逐项核对清单（导出侧与导入侧
 *        共用的唯一实现——NFR-MNT-04 无重复判定；同构对象→同清单）。
 *
 * 纯函数；线程安全；确定性。参数语义：
 * @param design [in] 根对象（导出侧＝闭包解码产物；导入侧＝包内根解码产物）
 * @param parts  [in] 部件对象集（ToolDefinition/SceneObject/PoseSet/
 *               DrivetrainDesign 的 variant；顺序不限——清单按对象身份
 *               字典序产出，集合语义）
 * @return 逐项清单（组序 authority→physics→resource→collision→pose；组内
 *         按对象/字段稳定序——确定性）
 */
std::vector<PackageCheckItem> roundtripChecklist(const RobotDesign& design,
                                                 const std::vector<ObjectVariant>& parts);

// =====================================================================
// 导入：结果（§9.4.9 行"ImportOutcome"的落位承载——命名见
// PackageExportOutcome 注）
// =====================================================================

/**
 * @brief Solidified 资源副本（§6.8"Solidified 资源副本"的导入承载）。
 *
 * 包内逐字节携带固化资源对象的 canonical 字节（解码非本单元职责——
 * 资源对象不在建模五对象表，字节对 modeling 不透明）；导入侧经清单哈希
 * 校验后原样交还调用方，由调用方（向导域/L5）按 ObjectId+ContentVersion
 * 重新登记进项目闭包（CON-03 固化引用的回读面）。
 */
struct SolidifiedResourceCopy {
    std::string resourceId;              ///< 资源清单键（根对象 resourceManifest 内唯一）
    core::ObjectId objectId;             ///< 固化 resource 对象身份（SolidifiedRef 同源）
    std::vector<std::uint8_t> bytes;     ///< resource 对象 canonical 字节（包内原样——digest 已验）

    bool operator==(const SolidifiedResourceCopy& o) const noexcept
    {
        return resourceId == o.resourceId && objectId == o.objectId && bytes == o.bytes;
    }
    bool operator!=(const SolidifiedResourceCopy& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 规范包导入报告（导入报告承载逐项清单——§6.8/AT-28/V-20）。
 *
 * 导入不产生修订、不落数据库：全部产物是值（草稿装配输入），对象身份在
 * 命令 prepare 阶段才再分配（§6.7 同款边界——本报告携带的是包内字节解出
 * 的原身份，供调用方比对/登记决策）。
 */
struct PackageImportReport {
    /// 根对象（包内 robot-design 条目解码产物）。
    RobotDesign design;

    /// 部件对象解码产物（ToolDefinition/SceneObject/PoseSet/DrivetrainDesign；
    /// 按包内条目路径字典序——确定性序）。
    std::vector<ObjectVariant> parts;

    /// Solidified 资源副本（包内 resources/ 条目原样字节——见类型注）。
    std::vector<SolidifiedResourceCopy> solidifiedResources;

    /// 逐项核对清单（与 roundtripChecklist 同构同源——调用方将其与导出侧
    /// 清单整体比对即得"五组逐项一致/diff=空"的机器结论）。
    std::vector<PackageCheckItem> checkItems;
};

/**
 * @brief 规范包导入结果（ok=false 时仅 error 有意义）。
 */
struct PackageImportOutcome {
    /// 是否成功（true＝包通过来源/schema/清单哈希三道校验并完整解码）。
    bool ok = false;

    /// 失败承载（ok=false 时有效）。
    ModelingError error{};

    /// 导入报告（ok=true 时有效；失败无草稿——PM-01 同款边界）。
    PackageImportReport report{};
};

// =====================================================================
// IModelPackagePort——接口（§9.4.9 行原文的落位签名）
// =====================================================================

/**
 * @brief 规范模型包导出/导入端口（MDL-20；io ZipChannel/AtomicFile 承载
 *        ——§9.4.9 行原文；无状态纯函数服务，卡 §3.4 总约定 1）。
 *
 * 诊断纪律（与 CanonicalBridge 同款口径）：diags 为追加式输出参数；本
 * 端口仅在两个已登记映射码的失败轨上产诊断——导出环境/写入失败→
 * MDL-EXPORT-FAILED（§9.5 T13 行：项目状态不变，检查目标路径/预算后重试）；
 * 非本软件工件→MDL-IMPORT-PACKAGE-UNKNOWN（§9.5 T13 行：使用 MDL-18/R2
 * 通道）。其余失败（闭包缺对象/清单哈希破损/解码失败等）经返回值错误面
 * 携带，不产诊断（Errors.hpp 阶段纪律：无已登记映射码＝不得产诊断）。
 */
class IModelPackagePort {
public:
    virtual ~IModelPackagePort() = default;

    /**
     * @brief 导出规范模型包（§9.4.9 行原文签名；会话级文件操作——不产生
     *        修订、不触碰项目状态，§6.8）。
     *
     * 流程（§6.8＋io §7.2/§4.6 同构）：① 从闭包取根/部件对象 canonical
     * 字节＋Solidified 资源对象副本（逐引用解引用，缺失→RefMissing）；
     * ② 装配 manifest（canonical JSON，含对象清单与 contentDigest）；
     * ③ 装配 ZIP 容器字节（条目按路径字典序）；④ io 临时区暂存→IZipChannel
     * 重开自检（清单哈希全绿）→io AtomicFile 原子替换到目标。任一步失败→
     * 清理暂存（目标零接触，旧输出完好——V-29）＋MDL-EXPORT-FAILED 诊断
     * （环境/写入失败时）。
     *
     * @param closure [in] 修订闭包字节源（只读；调用期存活——非 owning；
     *                须含 robot-design 根对象）
     * @param target  [in] 发布目标（父目录必须已存在）
     * @param diags   [out] 诊断输出（追加不清空；见类注诊断纪律）
     * @return ok＝包已原子就位（计数/摘要有效）；err＝ModelingError
     *         （码面：RefMissing——闭包缺根/缺被引对象｜ExportFailed——
     *         原子写出链路任一环失败；detail 携带 io 侧定位，未经脱敏
     *         不得直达用户文案）
     *
     * 纯函数（除目标文件副作用）；线程安全；确定性（同闭包＋同目标→
     * 同包字节——createdAtUtc 以调用方传入值为准）。
     */
    virtual PackageExportOutcome
        exportPackage(const ObjectClosureView& closure, const PackageExportTarget& target,
                      std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 导入回读规范模型包（§9.4.9 行原文签名；仅识别本软件导出的
     *        规范工件——manifest 来源标识＋schema 校验，§6.8）。
     *
     * 流程：① 经 io openZipChannel 打开已验证包文件（ValidatedSource 的
     * entrySnapshot.finalPath——io open/snapshot 产物；modeling 不自行读
     * 文件，SA-14）；② 读 manifest 并校验 formatId/producer/schemaVersion
     * （任一不符→PackageUnknown＋MDL-IMPORT-PACKAGE-UNKNOWN 诊断，引导
     * MDL-18/R2 通道）；③ 归档条目集与 manifest 清单精确比对＋逐条目
     * SHA-256 复算（IZipChannel verifyManifestEntries）；④ 逐对象条目
     * Codec 解码装配报告＋逐项清单。任何"能打开但不是本软件工件"的字节
     * 都不得被解释（fail-closed）。
     *
     * @param source [in] io 已验证输入（ValidatedSource——io open/snapshot/
     *               dependencyTree 产物组合，Import.hpp 同款契约；本端口消
     *               费 entrySnapshot.finalPath 定位包文件，bytes/dependencyTree
     *               由调用方链路携带、本端口不重复解析——包内容唯一经
     *               ZipChannel 读取，避免双源分歧）
     * @param diags  [out] 诊断输出（追加不清空；见类注诊断纪律）
     * @return ok＝导入报告（含逐项清单）；err＝ModelingError（码面：
     *         PackageUnknown——非本软件工件/容器不可读｜
     *         SchemaVersionUnsupported——包格式版本高于本程序支持｜
     *         MalformedPayload——清单/哈希/容器结构破损或对象解码失败｜
     *         RefMissing——包结构引用的对象条目缺失）
     *
     * 纯函数；线程安全；确定性（同包同判定同报告序）。
     */
    virtual PackageImportOutcome
        importPackage(const ValidatedSource& source,
                      std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/**
 * @brief IModelPackagePort 无状态产品实现（卡 §3.4 总约定 1——Codec/
 *        TemplateFactory 同款"接口＋final 实现"落位形态；可默认构造）。
 */
class ModelPackagePort final : public IModelPackagePort {
public:
    PackageExportOutcome
        exportPackage(const ObjectClosureView& closure, const PackageExportTarget& target,
                      std::vector<core::DiagnosticRecord>& diags) const override;

    PackageImportOutcome
        importPackage(const ValidatedSource& source,
                      std::vector<core::DiagnosticRecord>& diags) const override;
};

// =====================================================================
// WorkCell/DWC XML 外供导出（卡 §6.8 第二条；ACC5——集成模式 TU 承载）
// =====================================================================

/**
 * @brief WC/DWC XML 外供导出目标（§6.8"另可导出 WorkCell/DWC XML 供外部
 *        查看"的调用方输入）。
 *
 * 两个字段独立可选（空路径＝跳过该面）；DWC 面要求快照已编译 DWC 能力
 * （capabilities().hasDynamicWorkCell——否则该面按 ExportFailed 失败）。
 */
struct WorkCellExportTarget {
    /// WorkCell XML 发布目标（空＝不导出 WC 面；父目录必须已存在）。
    std::filesystem::path wcTargetFile;
    /// DWC XML 发布目标（空＝不导出 DWC 面；父目录必须已存在）。
    std::filesystem::path dwcTargetFile;
    /// 替换策略（同 PackageExportTarget::replace 口径——缺省原子覆盖）。
    io::ReplacePolicy replace = io::ReplacePolicy::OverwriteAtomic;
};

/**
 * @brief WorkCell/DWC XML 外供导出（§6.8 第二条的编排面；ACC5）。
 *
 * 数据源＝RuntimeSnapshot 的只读视图（runtime WorkCellConstView/
 * DynamicWorkCellConstView——消费已应用修订的编译产物）：零修订、零失效、
 * 不私设第二编译路径。modeling 仅编排"取只读数据→确定性 XML 序列化→
 * 经 io AtomicWriter 原子落盘"，不改造内容（视图全部字段逐项原样写入，
 * 数值 std::to_chars 最短往返）。XML 为 modeling 自有的外供查看表示
 * （元素命名镜像 RobWork WorkCell/DWC 概念——frame/device/body/bounds，
 * 便于外部工具阅读），非 RobWork wc.xml 格式（框架写侧 sdurw_loaders 属
 * "原则不使用、启用须登记"表行——DTB §4.6，本任务不引入；导入通道的
 * "自行实现 XML 处理"同款设计哲学见该行原文）。
 *
 * 会话级文件操作：不产生修订、不触碰项目状态；失败经 io AtomicFile 原子
 * 语义保证目标不变（先前输出完好——V-29 同款文件层观测）。
 *
 * 实现落位：本函数定义于 src/PackageWorkCell.cpp——消费 runtime 编译产物
 * 非模板类（WorkCell/Device/Body 符号），仅集成模式编译（runtime CMake
 * 同款 TARGET sdurw_kinematics gating；冒烟模式声明可用、无 TU 消费，
 * §15.4 v0.10 同款分工）。
 *
 * @param snapshot [in] 已编译运行时快照（只读消费；WC 能力必须就绪——
 *                 Compiled 态前置由调用方保证，WC 缺失＝调用方契约违约，
 *                 视图构造侧 fail-fast 语义兜底）
 * @param target   [in] 发布目标（两面独立可选——见类型注）
 * @param diags    [out] 诊断输出（追加不清空；环境/写入失败→
 *                 MDL-EXPORT-FAILED，诊断纪律同导出）
 * @return ok＝全部请求面已原子就位；err＝ModelingError（码面：
 *         ExportFailed——视图能力缺失/原子写出链路失败）
 *
 * 线程安全：纯函数（除目标文件副作用）；确定性（同快照＋同目标→同 XML
 * 字节；不读时钟/locale）。
 */
PackageExportOutcome exportWorkCellXml(const runtime::RuntimeSnapshot& snapshot,
                                       const WorkCellExportTarget& target,
                                       std::vector<core::DiagnosticRecord>& diags);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PACKAGE_HPP
