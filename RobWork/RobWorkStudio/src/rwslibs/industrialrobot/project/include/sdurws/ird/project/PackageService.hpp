/**
 * @file   PackageService.hpp
 * @brief  另存为与项目包服务（ISaveAsService/IPackageService）——另存为
 *         （三处改写复制协议）、选择性复制（引用完整性）与项目包导出/
 *         导入（一致数据视图＋消费窄编解码接口）的公共契约面（PRJ-T18
 *         落位，§3.1 预留兑现）。
 *
 * 设计依据：
 *   - units/project.md §5.8（另存为/包/升级——SaveAsOptions/
 *     PackageExportOptions 冻结签名零偏差：字段名/类型/顺序逐字锚定；
 *     ISaveAsService/IPackageService 为该节点名的服务实体）、§8.8（另存
 *     为协议：同卷临时位完整复制→闭包校验→换新 projectId〔对象/修订/
 *     分支 ID 原样保留〕→改写点仅三处〔project.json.projectId、
 *     HEAD.projectId、drafts 逐文件 projectId〕→按 PM-02 打开协议进入；
 *     results 已归档 RunManifest 的 taskIdentity 保持原五元组不改写；
 *     取消/失败清理临时位不留半成品）、§8.9（选择性复制与引用完整性：
 *     被勾选 reports 引用的 run 不在勾选集→该报告不复制＋诊断列出；
 *     drafts 独立可复制；对象库与修订历史无条件全量复制）、§8.10（包
 *     导入导出：导出＝开始时快照一致数据视图〔D-17〕＋后台进度可取消；
 *     导入＝解包临时目录全量校验→打开协议校验→整体 rename 发布，失败/
 *     取消不留目标目录）、§3.1（组成表：PackageService.hpp 行——本头即
 *     其阶段 B 落位）、§10.4（与 io 的分工——2026-10-12 O-09 裁决后
 *     口径）、§4.1/§4.2（.rwdesign 目录总表/project.json 静态标识——
 *     另存为整体重写 project.json 的契约出处）、§8.7（打开协议服务侧
 *     ——发布后进入的协议）、D-16（另存为＝全量复制＋三处改写）、
 *     D-17（导出一致视图＝HEAD 闭包快照＋不可变对象＋单文件原子替换）；
 *   - units/io.md §7（.rwpack 导入导出协议——**只读语义参考**：IPackage-
 *     Exporter 六步/IPackageImporter 九步的编排语义、§7.1 包格式树、
 *     ISnapshotFileSource 一致数据视图注入面。按 O-09 裁决的注入式协作
 *     形态，本头定义的是 **project 侧消费窄接口**——io 侧另有其冻结的
 *     接口面，本单元不 include 其任何头、不链接其任何目标，双向零编译
 *     依赖；io 实现由 L5 应用壳装配期注入，真实装配不属本任务）；
 *   - 需求 PM-05（另存为/项目包存储侧）、AT-13（失败不留半成品）、
 *     AT-20（复制后引用一致）、NFR-SEC-01/NFR-SEC-02（包通道的路径/
 *     预算防护——由编解码实现侧承担，见 IPackageCodec 注释）、D-17；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 1～4。
 *
 * 背景说明（为什么消费窄接口定义在 project 而实现在 io——O-09 裁决后
 *   的形态，本头零 io 依赖的机制依据）：ARCH §3.5 依赖表从未登记
 *   project→io 编译边（表外同层边＝构建失败），而 §8.10 的包导入导出
 *   需要"ZIP 编解码＋逐字节哈希还原校验＋预算/路径穿越防护"能力（io.md
 *   §7 冻结契约将其划归 io）。按 O-09 裁决（2026-10-12，P-PR-5/P-IO-1
 *   消账——§15.3），协作形态＝**注入式**：project 在自己的公共头定义
 *   消费窄接口（本头 IPackageCodec——导出/导入两向，按 §8.10 流程所需
 *   形状裁剪：一致数据视图快照源消费、逐字节哈希还原校验、预算/穿越
 *   防护由编解码实现侧承担），io 侧提供实现（io.md §7 同语义），由 L5
 *   应用壳装配期注入。双向零编译期依赖——本头不 include 该单元的任何
 *   公共头（该路径前缀即 LinkageContractTest 扫描面，此处不以字面量
 *   书写），sdurws_ird_project 不链接对侧目标（契约测试持续钉住）。
 *
 * 背景说明（另存为的"三处改写"纪律——为什么服务不触碰元数据对象）：
 *   ProjectMetadataRecord（项目显示名/分支表）是**内容编址**的对象
 *   （objects/<oid>/<cv>），且经 supersedes/committedBy 谱系与修订绑定
 *   （INV-M2/M3）。D-16 冻结另存为的改写点仅三处——绕过命令路径改写
 *   元数据会伪造谱系事实（PA-2 不可变历史）。因此 SaveAsOptions.
 *   newDisplayName 在本落位中的语义＝新副本的显示名意图，其**唯一合法
 *   落位途径＝新副本上的领域改名命令**（PM-11-S2，R2——内置元数据命令
 *   族处理器实体随其增量任务落位，§6.5/P-PR-9 口径）；本服务不消费该
 *   字段做磁盘改写（实现口径登记，DTB §5.4——复制期零元数据触碰）。
 *
 * 线程模型：两个服务实现均为**服务级操作互斥**（同一实例的并发调用
 *   串行——内部互斥，SolidifyService 先例同款）；不同实例可并行（各自
 *   的临时位独立命名，互不碰撞）。值类型全部可任意拷贝（无共享状态）。
 */

#ifndef SDURWS_IRD_PROJECT_PACKAGESERVICE_HPP
#define SDURWS_IRD_PROJECT_PACKAGESERVICE_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

// ProjectStore 完整类型定义于 ProjectStore.hpp（本头只前向声明——结果
// 结构持有其 unique_ptr；特殊成员函数 out-of-line 定义于实现文件，保证
// 仅包含本头的翻译单元也能安全移动/析构结果对象——OpenStoreResult 先例
// 同款纪律）。
class ProjectStore;

// =====================================================================
// 进度与取消回调（§5.8 PackageExportOptions 冻结字段类型名的落位）
// =====================================================================

/**
 * @brief 进度回调（§5.8 PackageExportOptions.onProgress 的元素类型）。
 *
 * 背景：按文件粒度上报（io.md §7.2 导出协议"进度回调每文件"同口径——
 *   包导出的耗时单位是逐文件读出/写入，按文件计数最贴调用方感知）。
 *
 * @param filesDone  [in] 已完成文件数（无单位——纯计数；从 0 起）
 * @param filesTotal [in] 快照清单文件总数（无单位——纯计数；导出开始
 *                   时快照冻结，导出期间不变——D-17）
 *
 * 线程约束：在服务操作线程上同步回调（不跨线程投递）；回调内不得调用
 *   本服务的任何方法（重入死锁）。可空＝不关心进度。
 */
using ProgressCallback = std::function<void(std::uint64_t filesDone, std::uint64_t filesTotal)>;

/**
 * @brief 取消回调（§5.8 PackageExportOptions.onCancel 的元素类型）。
 *
 * 背景：取消＝正常路径非错误（PM-05——"取消/失败清理临时位，不留半
 *   成品"；io.md §7.4"取消……状态 Canceled（非诊断）"）。服务在每个
 *   检查点（逐文件）查询本回调，返回 true 即中止：已完成的临时产物被
 *   清理，目标路径零写入。可空＝不提供取消面（操作不可取消）。
 *
 * @return true＝请求取消；false＝继续
 *
 * 线程约束：在服务操作线程上同步查询（回调实现方若从其他线程置位取消
 *   旗标，须自行保证其可见性——如原子量）；回调内不得调用本服务的任何
 *   方法。
 */
using CancelCallback = std::function<bool()>;

// =====================================================================
// 另存为（§5.8 冻结签名锚定——SaveAsOptions 字段集零偏差；§8.8/§8.9 协议）
// =====================================================================

/**
 * @brief 另存为选项（§5.8 冻结签名原文：{targetDir, copyResults,
 *        copyReports, copyDrafts, newDisplayName}——字段名/类型/顺序
 *        零偏差）。
 *
 * 背景说明（逐字段语义与落位口径）：
 *   - targetDir：新项目目录（.rwdesign）。**必须不存在**（另存为不覆盖
 *     既有目录——与包导入发布的 PM-05 口径一致；已存在＝调用方错误
 *     fail-fast，向导应先行校验）。父目录必须已存在（同卷临时位的落点
 *     ——§8.8"同卷临时位"的机制前提）。
 *   - copyResults/copyReports/copyDrafts：三棵可选树的勾选（§8.9——
 *     对象库与修订历史无条件全量复制，不受勾选影响；勾选记忆归 ui，
 *     PM-05）。
 *   - newDisplayName：新副本的显示名意图。**本落位不消费该字段做磁盘
 *     改写**（D-16 三处改写纪律——元数据对象内容编址＋谱系不变量禁止
 *     绕过命令路径触碰；落位途径＝发布后的领域改名命令 PM-11-S2/R2，
 *     见文件头"背景说明"）。
 *
 * 线程安全：纯值类型。
 */
struct SaveAsOptions {
    /// 新项目目录（.rwdesign；必须不存在；父目录必须已存在）。
    std::filesystem::path targetDir;
    /// 勾选：复制 results（完整运行——D-13 有 manifest 的运行，整 run
    /// 目录原子出现；RunManifest 的 taskIdentity 保持原五元组不改写）。
    bool copyResults = false;
    /// 勾选：复制 reports（§8.9 引用完整性过滤——见 skippedReports）。
    bool copyReports = false;
    /// 勾选：复制 drafts（独立可复制——逐文件重写 projectId）。
    bool copyDrafts = false;
    /// 新副本显示名意图（本落位不消费——见结构体注释与文件头背景说明）。
    std::string newDisplayName;
};

/**
 * @brief 另存为结果——发布与打开的事实载体（§3.1 点名的结果类型族之
 *        一；§5.8 未给字段级定义，本落位按流程产物最小集定义）。
 *
 * 字段纪律（成败与"不伪造"）：
 *   - ok==true 当且仅当新目录已发布且按 PM-02 打开协议成功进入（store
 *     非空）；newProjectId 为 core 分配的新身份（与源 projectId 必不
 *     相同）；
 *   - ok==false 时 store 为空（不伪造上下文）、newProjectId 为全零保留
 *     值（core 身份纪律：全零＝未设置）；error 携带失败原因（环境/状态
 *     错误——见下）；目标目录零残留（PM-05——发布前失败一律清理临时
 *     位；**发布后打开失败**的例外见 error 字段注释）；
 *   - skippedReports：§8.9 引用完整性处置清单（因引用的 run 不在勾选
 *     集而**未复制**的报告 id——机器可读面；开发级诊断经 sink 同步
 *     上报，稳定码不为此私造——CR-08：码值分配权威在 diagnostics
 *     StableCodeRegistry，现收编清单无对应码）。
 *
 * 错误轨口径（实现口径登记，DTB §5.4）：调用方契约违约（目标已存在/
 *   父目录缺失/路径空）＝fail-fast 异常（std::invalid_argument——
 *   createNew 先例同款）；状态与环境错误（源上下文已关闭/磁盘/闭包
 *   校验失败）＝返回值轨（ok=false＋error——另存为是长操作，返回值轨
 *   便于向导呈现，DraftService 写轨同款取舍）。
 *
 * 特殊成员函数 out-of-line（实现文件内定义——unique_ptr<ProjectStore>
 * 成员的删除器实例化需要完整类型，OpenStoreResult 先例同款）。
 *
 * 线程安全：纯值类型（once-constructed 不可变使用）。
 */
struct SaveAsResult {
    /// 析构（实现于 SaveAsServiceImpl.cpp——ProjectStore 完整类型可见处）。
    ~SaveAsResult();
    /// 默认构造（空结果——测试占位/移动目标）。
    SaveAsResult();
    /// 移动构造/赋值（unique_ptr 所有权转移；实现同上 out-of-line。
    /// 不标注 noexcept：optional<StoreError> 的移动受 runtime_error
    /// 实现的移动性约束，强制 noexcept 会令缺省定义被删除）。
    SaveAsResult(SaveAsResult&& other);
    SaveAsResult& operator=(SaveAsResult&& other);

    /// true＝新目录已发布并按 PM-02 打开协议进入（store 非空）。
    bool ok = false;
    /// 新项目的存储上下文（ok==true 时非空——调用方接管，语义同
    /// OpenStoreResult.store；注意其 writable 位按打开协议实际结果）。
    std::unique_ptr<ProjectStore> store;
    /// core 分配的新项目身份（prj- 规范文本；ok==false 时全零保留值）。
    core::ProjectId newProjectId{};
    /// §8.9 引用完整性：因引用 run 不在勾选集而未复制的报告 id 清单
    /// （确定性排序——NFR-COR-02；ok==false 时为空）。
    std::vector<std::string> skippedReports;
    /// 失败原因（ok==false 时必有值；ok==true 时 nullopt）。发布后打开
    /// 失败的特例：目标已是完整项目（发布即交付），**不删除目标**——
    /// error 如实返回，调用方可直接打开该目录重试进入（实现口径登记：
    /// 删除已发布项目＝数据损失风险，PM-05 的"不留半成品"指发布前的
    /// 临时位）。
    std::optional<StoreError> error;
    /// 新项目打开协议的恢复诊断快照（⑤步产出——与 RecoveryReport.
    /// diagnostics 同源；健康复制应为空）。开发级明细经 sink 上报，
    /// 不入本字段。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 另存为服务（§5.8 服务实体；§8.8/§8.9 协议的编排者）。
 *
 * 执行序（§8.8 协议逐步；任一步失败即转入清理路径——临时位整树删除，
 *   目标零写入）：
 *   1. 前置校验（调用方错误 fail-fast——见 @throws）；
 *   2. 快照捕获（D-17 同源机制：HEAD 字节一次读入＋HEAD 闭包文件清单
 *      冻结——对象/修订不可变，后台写入并存仍一致；.staging/lock 永不
 *      入清单）；
 *   3. 建同卷临时位（目标父目录旁 ".saveas-<8hex>.tmp/"——§8.8"同卷
 *      临时位"）；
 *   4. 完整复制（逐文件 write-through 持久性闸门——既有 AtomicFile
 *      原语，不绕过事务原语私写）；
 *   5. 闭包校验（对副本执行 PM-02 打开协议②③⑤——失败＝源存储损坏
 *      或复制失真，清理后如实返回）；
 *   6. 换新 projectId（core 生成；对象/修订/分支 ID 全部原样保留——
 *      随机全局唯一，跨副本引用语义不变）＋三处改写（project.json.
 *      projectId、HEAD.projectId、drafts 逐文件 projectId）；
 *   7. 发布（临时位整体 rename 到目标——同卷原子；目标已存在的竞态按
 *      发布失败处置）；
 *   8. 按 PM-02 打开协议进入新目录（Writable 请求——可能按 PM-07 降
 *      级只读，以返回 store 的 writable 位为准）。
 *
 * results 的 RunManifest.taskIdentity 保持原五元组不改写（§8.8——历史
 *   运行事实；读取侧不校验 manifest 的 projectId 归属，新归档才以新
 *   projectId 校验）。
 *
 * 源上下文权限：另存为是**读轨操作**——只读源上下文（PM-07）照常可另
 *   存为；源上下文 Closed 后拒绝（服务消费②端口取数，已关闭即不可用
 *   ——状态错误走返回值轨）。
 *
 * 获取方式：createSaveAsService() 工厂（实现类型不出公共头——R-2）。
 *
 * 生命周期与所有权：实例由装配方 unique_ptr 持有；对源上下文为非
 *   owning 引用——**服务实例的生存期不得长于源存储上下文**。
 *
 * 线程模型：服务级互斥（并发 saveAs 串行——临时位命名唯一性不依赖
 *   互斥，互斥收敛的是快照捕获的读轨一致性语义）；与源上下文的命令
 *   提交可并存（D-17 快照一致性保证副本自洽——并发提交的内容落在
 *   快照之后，不入本副本）。
 */
class ISaveAsService {
public:
    /// 虚析构：工厂返回 unique_ptr 多态销毁的常规保障。
    virtual ~ISaveAsService() = default;

    /**
     * @brief 执行另存为（§8.8/§8.9 协议编排——执行序见类注释）。
     *
     * @param options [in] 另存为选项（SaveAsOptions 字段约束）
     *
     * @return 另存为结果（成败字段纪律见 SaveAsResult 注释；取消面：
     *         冻结 SaveAsOptions 无取消回调字段——另存为无中途取消入口，
     *         "取消/失败清理临时位"的取消半区由"无入口即不可发生"结构
     *         性成立，失败半区由清理路径承担）
     *
     * @throws std::invalid_argument 调用方契约违约：targetDir 为空／
     *         已存在／父目录不存在（向导应先行校验的场景——fail-fast
     *         不产稳定码）
     *
     * 线程安全：服务级互斥（并发调用串行）。
     */
    [[nodiscard]] virtual SaveAsResult saveAs(const SaveAsOptions& options) = 0;
};

/**
 * @brief 另存为服务工厂（L5/测试装配入口；实现类型不出公共头——R-2）。
 *
 * @param source      [in] 源存储上下文（非 owning——快照取数面；生存期
 *                    覆盖返回的服务实例）
 * @param eventBus    [in] 事件总线（非 owning，可空＝新项目上下文跳过
 *                    事件发布——OpenStoreRequest.eventBus 同口径；新
 *                    上下文的打开需要独立的总线注入，源上下文不外泄其
 *                    总线指针——装配侧显式供给）
 * @param diagnostics [in] 诊断 sink（非 owning，可空＝丢弃诊断——同上
 *                    口径；§8.9 引用完整性等开发级诊断经此上报）
 *
 * @return 服务实例（独占持有）
 *
 * 线程安全：装配期调用。
 */
[[nodiscard]] std::unique_ptr<ISaveAsService> createSaveAsService(
    ProjectStore& source,
    core::IDomainEventBus* eventBus = nullptr,
    IDiagnosticsSink* diagnostics = nullptr);

// =====================================================================
// 项目包（§8.10——导出/导入两向；消费窄接口 O-09 注入式协作）
// =====================================================================

/**
 * @brief 包内文件条目（一致数据视图快照清单的元素——D-17）。
 *
 * 背景说明（packPath 的编址契约）：包内树逐字节镜像 .rwdesign 布局，
 *   统一以 "payload/" 前缀＋正斜杠分隔承载（io.md §7.1 包格式树的
 *   payload/ 镜像规则同语义——传输封装，非第二权威格式）。relPath 为
 *   ASCII（身份 token 与固定名），跨平台编址无歧义。
 *
 * 线程安全：纯值类型。
 */
struct PackFileEntry {
    /// 包内相对路径（"payload/" 前缀＋正斜杠；ASCII）。
    std::string packPath;
    /// 快照时点字节数（单位＝字节；HEAD/对象/修订不可变故恒准确——
    /// drafts 的极窄替换窗口按"读回完整版本"语义处置，见
    /// ISnapshotFileSource::read 注释）。
    std::uint64_t sizeBytes = 0;

    bool operator==(const PackFileEntry& o) const noexcept
    {
        return packPath == o.packPath && sizeBytes == o.sizeBytes;
    }
    bool operator!=(const PackFileEntry& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 一致数据视图快照源（D-17）——project 实现、编解码实现消费的
 *        注入面（io.md §7.2 ISnapshotFileSource 同语义的 project 侧
 *        契约面；导出六步的取数通道）。
 *
 * 背景说明（D-17 一致性的机制——为什么是"枚举快照"而非"目录扫描"）：
 *   导出开始时冻结清单＝HEAD 闭包文件清单＋所选树清单：对象/修订不可
 *   变 ⇒ 清单内文件内容在导出期间恒定；drafts/manifest 单文件原子替换
 *   ⇒ 读到的必是某次完整版本；.staging/lock 永不入清单 ⇒ "文件存在≠
 *   已提交"的结构性保证。后台写入与导出并存因此安全（§8.10/PM-05）。
 *
 * 所有权与生命周期：实现对象由 project 服务内部持有（导出单次调用的
 *   生存期）；编解码实现仅持引用，不管理其生存期。
 *
 * 线程约束：单次导出调用内被编解码实现串行调用（io.md §9.9"单次调用
 *   单线程"同口径）。
 */
class ISnapshotFileSource {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ISnapshotFileSource() = default;

    /**
     * @brief 快照清单（导出开始时冻结——D-17；同一导出内多次调用返回
     *        全等清单）。
     *
     * @return 条目列表（按 packPath 字典序——确定性，NFR-COR-02）
     */
    [[nodiscard]] virtual std::vector<PackFileEntry> enumerate() const = 0;

    /**
     * @brief 读取清单内文件字节（稳定版本语义）。
     *
     * 稳定版本语义（io.md §7.2"source 语义：同 key 重读返回稳定版本"
     * 同口径）：drafts 被原子替换的极窄窗口内，本方法返回**当前完整
     * 版本**（旧版或新版之一——单文件原子替换保证无半文件）；不拼接
     * 不混合。
     *
     * @param packPath [in] 清单内的 packPath（"payload/" 前缀；清单外
     *                 路径＝编解码实现违约——fail-fast，"清单外文件一
     *                 概不读"是双方共同契约）
     * @param outBytes [out] 成功时接收文件完整字节（深拷贝）
     *
     * @return true＝读取成功；false＝文件不可读（drafts 被放弃删除等
     *         ——编解码实现应中止导出并清理，**不产出混合版本包**，
     *         io.md §7.2"仍失败→导出失败清理"同口径）
     *
     * @throws std::invalid_argument packPath 无 "payload/" 前缀或不在
     *         快照清单内（编解码实现契约违约——fail-fast）
     *
     * 线程约束：单次导出内串行调用。
     */
    virtual bool read(const std::string& packPath, std::vector<std::uint8_t>& outBytes) = 0;
};

/**
 * @brief 包导出选项（§5.8 冻结签名原文：{targetFile, includeResults,
 *        includeReports, includeDrafts, onProgress, onCancel}——字段名/
 *        类型/顺序零偏差）。
 *
 * 线程安全：纯值类型（回调为可拷贝的 std::function）。
 */
struct PackageExportOptions {
    /// 包目标文件（.rwpack；必须不存在或将被原子覆盖——导出默认原子
    /// 覆盖语义由编解码实现承载：失败/取消时目标不变〔无先前包不被
    /// 破坏〕；父目录必须已存在——调用方契约）。
    std::filesystem::path targetFile;
    /// 勾选：results 树（完整运行；五元组原样——§8.8 同纪律）。
    bool includeResults = false;
    /// 勾选：reports 树（§8.9 引用完整性过滤——引用 run 不在所选
    /// results 集（未勾选 results＝空集）的报告不入快照，开发诊断列出）。
    bool includeReports = false;
    /// 勾选：drafts 树（当前有效草稿——.new/.bak 残留不入清单）。
    bool includeDrafts = false;
    /// 进度回调（每文件——见 ProgressCallback；可空）。
    ProgressCallback onProgress;
    /// 取消回调（每文件检查点——见 CancelCallback；可空）。
    CancelCallback onCancel;
};

/**
 * @brief 包导出结果（流程产物最小集——§5.8 未给字段级定义，本落位
 *        定义；成败纪律同 SaveAsResult）。
 *
 * 字段纪律：ok==true 当且仅当目标包文件已就位（编解码实现的原子替换
 *   成功）；canceled==true 表示用户取消（正常路径非错误——error 必为
 *   nullopt、diagnostics 必为空）；编解码实现侧失败（损坏/预算/穿越/
 *   磁盘）＝ok=false＋diagnostics 透传（io 域码——project 不解释不改
 *   写，§5.7"透传"同纪律；不私造 PRJ 码、不伪造 StoreError 映射——
 *   实现口径登记，DTB §5.4：SolidifyServiceImpl 先例"不借用 §6.3 拒
 *   绝词表表达 io 环境事实"同款）。导出全程**零写源项目**（结构性）。
 *
 * 线程安全：纯值类型。
 */
struct PackageExportResult {
    /// true＝目标包文件已就位。
    bool ok = false;
    /// true＝用户取消（正常路径——error nullopt、diagnostics 空）。
    bool canceled = false;
    /// 导出文件数（快照清单大小——进度对照/报告面；ok==true 时有效）。
    std::uint64_t fileCount = 0;
    /// project 自身失败的原因（状态/环境——返回值轨口径同 SaveAsResult；
    /// 编解码实现侧失败不入本字段——其事实由 diagnostics 透传承载）。
    std::optional<StoreError> error;
    /// 编解码实现透传诊断（io 域码——project 不解释；仅 !ok&&!canceled
    /// 时可能非空）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 编解码实现的单次操作结果（IPackageCodec 两向共用的结果形状；
 *        project 侧窄接口的内部载体——不承载发布/打开语义，那些是
 *        服务编排的事）。
 *
 * 线程安全：纯值类型。
 */
struct PackageCodecOutcome {
    /// true＝编解码半区成功（导出＝目标包就位；导入解包＝临时树校验
    /// 通过且就位）。
    bool ok = false;
    /// true＝检查点取消（临时产物已由实现自行清理或留待服务清理——
    /// 见 IPackageCodec 方法注释的责任划分）。
    bool canceled = false;
    /// 处理文件数（观测面）。
    std::uint64_t fileCount = 0;
    /// 失败诊断（io 域码——实现自产，project 透传不解释）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 包编解码消费窄接口（O-09 注入式协作的 project 侧契约面）——
 *        project 定义、io 实现（io.md §7 导出六步/导入九步中 io 半区
 *        的同语义承载）、L5 应用壳装配期注入。
 *
 * 背景说明（接口形状为什么只有两个方法、责任如何切分）：§8.10 对 io
 *   的消费恰好两段——**导出执行**（一致视图逐文件读出→清单/压缩→
 *   自检→原子替换到目标；io.md §7.2 六步）与**导入解包**（包文件→
 *   临时树全量校验展开：版本/路径穿越/预算/逐字节哈希还原/结构镜像/
 *   文件层引用——io.md §7.3 步③～⑦的文件半区）。打开协议校验与 rename
 *   发布是 project 的事（§7.7 责任切分表"N-7：io 不发布"），不经本
 *   接口。逐字节哈希还原校验与预算/路径穿越防护由编解码实现承担
 *   （NFR-SEC-01/02——io 的 SafePath/BudgetGuard 管辖区，io.md §4）。
 *
 * 诊断码口径：实现自产的失败诊断使用其域码（io.md §9.12 收编 token
 *   ——如 IO-SEC-PATH-ESCAPE/IO-PACK-HASH-MISMATCH/IO-SEC-BUDGET-
 *   EXPAND）；project 透传不解释、不映射、不私造 PRJ 码（CR-08/
 *   §5.7 透传纪律）。取消是状态不是诊断（canceled 位承载）。
 *
 * 所有权与生命周期：实现对象由装配方（L5/测试）持有；注入服务的指针
 *   非 owning，生存期覆盖服务实例。
 *
 * 线程约束：单次方法调用内串行（同 io.md §9.9 会话/调用级约束）；
 *   同一实例的并发调用由实现方定义（服务级互斥不外溢到 codec——并发
 *   导出/导入应经不同服务实例或装配方保证）。
 */
class IPackageCodec {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~IPackageCodec() = default;

    /**
     * @brief 导出执行（io.md §7.2 六步的同语义承载：临时区→逐文件
     *        读出＋摘要→清单/包描述 canonical 写出→压缩→完整性自检→
     *        原子替换到目标）。
     *
     * 契约（实现方义务）：失败/取消发生在替换前→目标不变（无先前包
     * 不被破坏）、临时产物清理；替换成功后的清理失败→包已正确、仅
     * 开发级诊断。零写源项目（只经 consistentView 读）。
     *
     * @param consistentView [in] 一致数据视图（D-17——project 快照；
     *                      清单外文件一概不读）
     * @param targetFile     [in] 包目标（.rwpack；父目录须已存在——
     *                      调用方契约）
     * @param onCancel       [in] 取消回调（每文件检查点；可空）
     * @param onProgress     [in] 进度回调（每文件；可空）
     *
     * @return 操作结果（成败/取消/诊断——见 PackageCodecOutcome）
     */
    [[nodiscard]] virtual PackageCodecOutcome exportPack(
        ISnapshotFileSource& consistentView,
        const std::filesystem::path& targetFile,
        const CancelCallback& onCancel,
        const ProgressCallback& onProgress) = 0;

    /**
     * @brief 导入解包执行（io.md §7.3 步③～⑦文件半区的同语义承载：
     *        包版本/条目名预检〔穿越/重复/符号链接〕→流式展开＋预算
     *        →逐文件哈希还原校验→结构镜像核对→文件层引用核对）。
     *
     * 契约（实现方义务）：stagingDir 由 project 预建（**同卷临时位**
     * ——发布＝rename 的机制前提）；成功后 stagingDir 内为已验证的
     * .rwdesign 镜像树；失败/取消后 stagingDir 的内容态不作承诺——
     * 目录清理归 project 统一承担（实现口径登记，DTB §5.4：单一清理
     * 责任方，避免双所有者竞态）。
     *
     * @param packFile   [in] 包文件（.rwpack；存在可读——调用方契约）
     * @param stagingDir [in] 已存在的空临时目录（与发布目标同卷）
     * @param onCancel   [in] 取消回调（每条目检查点；可空）
     * @param onProgress [in] 进度回调（每条目；可空）
     *
     * @return 操作结果（成败/取消/诊断——见 PackageCodecOutcome）
     */
    [[nodiscard]] virtual PackageCodecOutcome unpackInto(
        const std::filesystem::path& packFile,
        const std::filesystem::path& stagingDir,
        const CancelCallback& onCancel,
        const ProgressCallback& onProgress) = 0;
};

/**
 * @brief 包导入结果（§3.1 点名类型——§5.8/§8.10 流程产物最小集）。
 *
 * 字段纪律：ok==true 当且仅当包已发布到目标目录并按 PM-02 打开协议
 *   成功进入（store 非空；importedProjectId＝包内项目身份——**导入
 *   不改写 projectId**，与另存为的换新机制相反：项目包是项目的整体
 *   搬运，身份保持）；canceled==true＝用户取消（正常路径——目标零
 *   写入、临时区已清理、error 必为 nullopt）；编解码/打开协议失败＝
 *   ok=false：目标目录从未写入（发布只发生在全量校验后——PM-05"失
 *   败不留目标目录"的结构性保证）、临时区已清理、error 携带打开协议
 *   失败（如 StoreCorrupt/FormatLegacy——PM-06 语义原样）或发布环境
 *   失败；编解码侧失败事实由 diagnostics 透传（同 PackageExportResult
 *   口径）。发布后打开失败的特例同 SaveAsResult（目标保留不删除）。
 *
 * 特殊成员函数 out-of-line（实现文件内定义——OpenStoreResult 先例）。
 *
 * 线程安全：纯值类型。
 */
struct PackageImportResult {
    /// 析构（实现于 PackageServiceImpl.cpp——ProjectStore 完整类型可见处）。
    ~PackageImportResult();
    /// 默认构造（空结果——测试占位/移动目标）。
    PackageImportResult();
    /// 移动构造/赋值（unique_ptr 所有权转移；实现同上 out-of-line。
    /// 不标注 noexcept——理由同 SaveAsResult）。
    PackageImportResult(PackageImportResult&& other);
    PackageImportResult& operator=(PackageImportResult&& other);

    /// true＝包已发布并按 PM-02 打开协议进入（store 非空）。
    bool ok = false;
    /// true＝用户取消（正常路径非错误）。
    bool canceled = false;
    /// 导入项目的存储上下文（ok==true 时非空——调用方接管）。
    std::unique_ptr<ProjectStore> store;
    /// 包内项目身份（prj- 规范文本；导入不改写——ok==false 时全零
    /// 保留值；以打开后的存储上下文 projectId 为地面事实，本字段是
    /// 其快照）。
    core::ProjectId importedProjectId{};
    /// 失败原因（打开协议/发布环境——ok==false 且 !canceled 时通常
    /// 有值；编解码侧失败可仅以 diagnostics 承载，见结构体注释）。
    std::optional<StoreError> error;
    /// 诊断（编解码透传＋新项目打开协议恢复诊断快照）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 项目包服务（§5.8 服务实体；§8.10 导出/导入的编排者）。
 *
 * 导出执行序（§8.10/D-17）：
 *   1. 前置校验（调用方错误 fail-fast——见 @throws）；
 *   2. 快照捕获（与另存为同源机制：HEAD 字节＋闭包清单冻结；§8.9
 *      引用完整性过滤在清单装配期完成）；
 *   3. 编解码导出（IPackageCodec::exportPack——进度/取消逐文件；
 *      取消即清理临时输出，目标不变）。
 *   导出零写源项目（结构性）。
 *
 * 导入执行序（§8.10/PM-05——失败或取消不留目标目录、清理临时区）：
 *   1. 前置校验（调用方错误 fail-fast）；
 *   2. 建同卷临时位（目标父目录旁 ".rwpack-import-<8hex>.tmp/"——发布＝
 *      rename 的机制前提）；
 *   3. 编解码解包＋全量校验（IPackageCodec::unpackInto——预算/穿越/
 *      逐字节哈希还原由实现承担，NFR-SEC-01/02）；
 *   4. 打开协议全量校验（PM-02②③⑤ 于临时目录——ReadOnly，不取锁；
 *      失败＝包内容未通过项目校验，清理后如实返回）；
 *   5. 整体发布（临时位 rename 到目标——同卷原子；目标占用二次收口）；
 *   6. 按打开协议进入目标目录（Writable 请求）。
 *
 * 与源项目的关系：导入建立**独立的新项目上下文**（不消费构造时绑定
 *   的源上下文——工作流的项目切换编排在 project 之外，PM-03）；源
 *   上下文指针仅导出消费（可为空＝本服务实例只用于导入——欢迎页无
 *   打开项目的导入场景）。
 *
 * 获取方式：createPackageService() 工厂（实现类型不出公共头——R-2）。
 *
 * 生命周期与所有权：实例由装配方 unique_ptr 持有；source/codec/总线/
 *   sink 均**非 owning**——生存期由装配方保证覆盖服务实例。
 *
 * 线程模型：服务级互斥（exportPackage/importPackage 串行——临时位与
 *   codec 消费面的单份语义）。
 */
class IPackageService {
public:
    /// 虚析构：工厂返回 unique_ptr 多态销毁的常规保障。
    virtual ~IPackageService() = default;

    /**
     * @brief 导出项目包（§8.10/D-17——执行序见类注释）。
     *
     * @param options [in] 导出选项（PackageExportOptions 字段约束）
     *
     * @return 导出结果（成败/取消/诊断纪律见 PackageExportResult）
     *
     * @throws std::invalid_argument 调用方契约违约：编解码实现未注入
     *         ／源上下文未绑定（构造时 source==nullptr 的实例不可导出）
     *         ／源上下文为空指针违约在构造期已排除——此处指 options.
     *         targetFile 为空／父目录不存在／扩展名非 .rwpack（§5.8
     *         冻结注释".rwpack"的形态契约）
     *
     * 线程安全：服务级互斥（并发调用串行）。
     */
    [[nodiscard]] virtual PackageExportResult exportPackage(
        const PackageExportOptions& options) = 0;

    /**
     * @brief 导入项目包（§8.10/PM-05——执行序见类注释；失败/取消不留
     *        目标目录、清理临时区）。
     *
     * @param packFile   [in] 包文件（.rwpack；必须存在且为普通文件）
     * @param targetDir  [in] 发布目标目录（必须不存在——不覆盖既有目录；
     *                    父目录必须已存在）
     * @param onProgress [in] 进度回调（每条目；可空）
     * @param onCancel   [in] 取消回调（每条目检查点；可空）
     *
     * @return 导入结果（成败/取消/诊断纪律见 PackageImportResult）
     *
     * @throws std::invalid_argument 调用方契约违约：编解码实现未注入／
     *         packFile 为空/不存在/非普通文件／targetDir 为空/已存在/
     *         父目录不存在
     *
     * 线程安全：服务级互斥（并发调用串行）。
     */
    [[nodiscard]] virtual PackageImportResult importPackage(
        const std::filesystem::path& packFile,
        const std::filesystem::path& targetDir,
        const ProgressCallback& onProgress = {},
        const CancelCallback& onCancel = {}) = 0;
};

/**
 * @brief 项目包服务工厂（L5/测试装配入口；实现类型不出公共头——R-2）。
 *
 * @param source      [in] 源存储上下文（非 owning；**可空**＝本实例
 *                    只用于导入——exportPackage 届时 fail-fast；导出
 *                    用例必须绑定非空源）
 * @param codec       [in] 包编解码实现（非 owning——io 实现经 L5 装配
 *                    注入；测试注入 fake。不得为空——空＝装配违约，
 *                    构造期 fail-fast）
 * @param eventBus    [in] 事件总线（非 owning，可空——导入项目上下文
 *                    的打开注入，OpenStoreRequest.eventBus 同口径）
 * @param diagnostics [in] 诊断 sink（非 owning，可空＝丢弃诊断）
 *
 * @return 服务实例（独占持有）
 *
 * @throws std::invalid_argument codec 为空（装配违约尽早暴露）
 *
 * 线程安全：装配期调用。
 */
[[nodiscard]] std::unique_ptr<IPackageService> createPackageService(
    ProjectStore* source,
    IPackageCodec* codec,
    core::IDomainEventBus* eventBus = nullptr,
    IDiagnosticsSink* diagnostics = nullptr);

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_PACKAGESERVICE_HPP
