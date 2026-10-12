/**
 * @file   StoreClone.hpp
 * @brief  存储快照与复制共享层（StoreClone）——另存为（§8.8/§8.9）与
 *         包导出（§8.10/D-17）共用的一致数据视图捕获、同卷临时位复制、
 *         三处改写点重写与发布原语的内部实现（私有实现头，不出
 *         include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §8.8（另存为协议：同卷临时位完整复制→闭包校验
 *     →换新 projectId→改写点仅三处→按 PM-02 打开协议进入；取消/失败
 *     清理临时位）、§8.9（选择性复制与引用完整性：对象库与修订历史无
 *     条件全量复制；results 整 run 目录＋manifest 原子出现；reports 引
 *     用 run 不在勾选集→不复制＋诊断列出；drafts 独立可复制）、
 *     §8.10/D-17（导出一致视图＝开始时快照 HEAD 闭包＋所选树清单——
 *     对象/修订不可变＋drafts 单文件原子替换⇒后台写入并存仍一致；
 *     .staging/lock 永不入清单）、§4.1（.rwdesign 目录总表——复制集
 *     白名单与命名规则的唯一权威）、§4.5（提交闭包双通道——快照闭包
 *     行走的语义权威）、§7.2（Windows 文件原语——write-through 持久
 *     性闸门/publishNew 只增发布，不绕过事务原语私写）；
 *   - 任务契约 tasks/foundation/PRJ-T18.json acceptance 1～3。
 *
 * 背景说明（快照捕获为什么锚定"磁盘 HEAD 一次读入"）：查询端口的各
 *   方法各自原子（与提交互斥），但跨调用的读序可能横跨一次并发提交
 *   ——分支表/权威元数据反映提交后状态而 parent 链行走自提交前 HEAD
 *   起步，会产生"新 tip 引用了不在快照闭包的修订"的混合视图。本层以
 *   **磁盘 HEAD 字节一次读入＋解析**为锚（HEAD 原子替换保证读到旧或
 *   新完整内容之一），闭包行走、分支表（取锚定元数据版本的 branches
 *   而非 currentMetadata）、results/reports/drafts 勾选全部锚定其上
 *   ——同一次捕获内部自洽（D-17 快照语义）。
 *
 * 背景说明（复制集白名单——§4.1 的消费口径）：复制集＝HEAD＋
 *   project.json＋revisions/＋objects/（无条件，项目状态本体）＋
 *   results/·reports/·drafts/（按勾选）。checkpoints/（任务缓存——
 *   CON-04 缓存语义不跨项目携带，缺失＝缓存未命中安全）、catalog/
 *   （阶段 C 接入）、.staging/·lock（非项目状态——D-17 明令不入）
 *   一律不在复制集。results 只取**完整运行**（有 manifest.json——
 *   D-13"只认完整运行"；未完成归档不跨项目携带，恢复语义同源）。
 *
 * 实现口径登记（DTB §5.4——§8.8/§8.9/§8.10 未定义判据，详见各函数
 *   注释，七项）：
 *   ①闭包行走＝磁盘 HEAD 锚定的双通道迭代（RevisionIndex::
 *     committedClosure 语义的"公共查询端口＋Codec 解析"版——服务只
 *     消费②端口与自有格式，不触碰存储内部部件，ARC-02 卫生）；
 *   ②results 勾选集＝闭包修订的 listRuns 并集（D-13 只认 manifest）；
 *   ③reports 引用扫描面＝report.json 文本中 run- 规范文本 token 的
 *     存在性（reporting 格式未冻结——PRJ 阶段 B 接入面；保守判定：
 *     报告引用的全部 run 都在勾选集才复制；无 report.json＝无引用
 *     照常复制）；
 *   ④drafts 复制以捕获时点磁盘完整版本为准（原子替换读到的即完整
 *     版本——可能新于 HEAD 快照：其 baseRevision 指向的修订若不在
 *     快照闭包，新项目打开协议⑤按恢复诊断列出，不影响打开——草稿
 *     是编辑态非提交态）；复制中途读不到（被放弃）＝跳过＋开发诊断
 *     （另存为路径；导出路径按 ISnapshotFileSource 契约返回不可读，
 *     由编解码实现中止）；
 *   ⑤临时位命名＝目标父目录旁 ".saveas-<8hex>.tmp/"（另存为）与
 *     ".rwpack-import-<8hex>.tmp/"（导入解包）——点前缀隐藏位，崩溃残留
 *     不被误识别为项目；失败/取消清理由服务承担（io.md TempArea 的
 *     会话互斥/残留回收随真实编解码装配落地，窄面不含——登记）；
 *   ⑥发布期 rename 失败一律按既有映射（磁盘满→DiskFull、拒绝访问→
 *     AccessDenied、其余→WriteRejected——PRJ-T05/T07 同表）；目标占用
 *     的竞态同走 WriteRejected（detail 携带 osError 定位）；
 *   ⑦checkpoints/catalog 不在复制集（见"背景说明"）。
 *
 * 线程约束：全部函数为无共享状态的操作（参数携带全部依赖）；并发安
 *   全性由调用方（服务级互斥）保证。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_STORECLONE_HPP
#define SDURWS_IRD_PROJECT_SRC_STORECLONE_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/PackageService.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "win32/AtomicFile.hpp"

namespace sdurws::ird::project::clone {

// =====================================================================
// 快照值类型
// =====================================================================

/**
 * @brief 快照文件条目（相对项目根的路径，正斜杠分隔；ASCII——身份
 *        token 与固定名构成，无用户自由文本）。
 *
 * 线程安全：纯值类型。
 */
struct SnapshotFile {
    /// 相对路径（如 "revisions/rev-.../manifest.json"；正斜杠——包内
    /// 编址与本地路径的共同形态）。
    std::string relPath;
    /// 捕获时点字节数（单位＝字节；不可变文件恒准确，drafts 见实现
    /// 口径④）。
    std::uint64_t sizeBytes = 0;
};

/**
 * @brief 存储快照（D-17 一致数据视图的进程内形态）——另存为复制与包
 *        导出取数的共同输入；捕获后即冻结。
 *
 * 线程安全：纯值类型（捕获后不可变使用）。
 */
struct StoreSnapshot {
    /// HEAD 文件捕获字节（快照锚——复制与重写都以它为准，不重读磁盘；
    /// D-17"锁定快照视图"的字面承载）。
    std::string headBytes;
    /// HEAD 解析结果（闭包行走锚 revisionId/revisionSeq 的来源）。
    HeadRecord head;
    /// HEAD 引用元数据版本的记录（分支表来源——drafts 勾选按锚定分支
    /// 表枚举，不用 currentMetadata〔可能已前进〕）。
    ProjectMetadataRecord headMetadata;
    /// HEAD 元数据版本的对象引用键（编址——快照清单装配用）。
    ObjectRefPair headMetadataRef{};
    /// 快照文件清单（含 "project.json"/"HEAD" 与各树成员；按 relPath
    /// 字典序——确定性，NFR-COR-02）。
    std::vector<SnapshotFile> files;
    /// 勾选集 results 的完整运行 id 规范文本（§8.9 报告引用判定输入；
    /// 确定性排序）。
    std::vector<std::string> selectedRunIds;
};

/**
 * @brief 快照捕获产出（快照＋§8.9 引用完整性处置清单）。
 *
 * 线程安全：纯值类型。
 */
struct CaptureResult {
    StoreSnapshot snapshot;
    /// 因引用 run 不在勾选集而未入清单的报告 id（§8.9——确定性排序；
    /// 开发级诊断经 sink 同步上报）。
    std::vector<std::string> skippedReports;
};

// =====================================================================
// 快照捕获（另存为与包导出的共同第 2 步）
// =====================================================================

/**
 * @brief 捕获存储快照（D-17——HEAD 锚定闭包清单＋所选树清单；见文件
 *        头"背景说明"与实现口径①②③④）。
 *
 * @param query          [in] 源上下文查询端口（②端口——ARC-02 卫生：
 *                      闭包行走/对象读取/运行/草稿清单全走公共端口）
 * @param projectDir     [in] 源项目目录（HEAD 文件一次读入的取数点；
 *                      results/reports 树枚举同源）
 * @param includeResults [in] 勾选 results（完整运行入清单＋勾选集）
 * @param includeReports [in] 勾选 reports（引用完整性过滤后入清单）
 * @param includeDrafts  [in] 勾选 drafts（锚定分支表的当前有效草稿）
 * @param sink           [in] 诊断 sink（非 owning，可空＝丢弃——开发
 *                      级观察点：跳过的报告/草稿等）
 *
 * @return 捕获产出（清单确定性排序）
 *
 * @throws StoreError StoreCorrupt（HEAD 缺失/损坏、闭包行走遇悬挂引用
 *         ——源存储数据侧错误，如实上抛；捕获发生在建临时位之前，此
 *         路径零磁盘副作用）
 *
 * 线程约束：调用方保证同一上下文的捕获与其他写轨操作串行（服务级互斥
 *         已覆盖）；并发提交并存时快照仍自洽（HEAD 锚定——文件头背景
 *         说明）。
 */
[[nodiscard]] CaptureResult captureSnapshot(const IProjectQueryPort& query,
                                            const std::filesystem::path& projectDir,
                                            bool includeResults,
                                            bool includeReports,
                                            bool includeDrafts,
                                            IDiagnosticsSink* sink);

// =====================================================================
// 复制与改写（另存为协议第 4/6 步的承载）
// =====================================================================

/**
 * @brief 复制快照清单到暂存位（§8.8"完整复制"；逐文件 write-through
 *        持久性闸门——AtomicFile 原语，不绕过事务原语私写）。
 *
 * 失败语义（实现口径④⑥）：
 *   - 不可变文件（objects/revisions/HEAD/project.json/results/reports）
 *     读不到＝快照与磁盘失真（结构上不应发生——对象/修订不可变）→
 *     StoreCorrupt 上抛（调用方清理临时位）；
 *   - drafts 读不到＝捕获后被用户放弃（discard 删除——编辑态的合法
 *     并发事实）→ 跳过＋开发诊断，不失败（草稿独立——§8.9）；
 *   - 写失败（磁盘满/权限）→ osToStoreError 映射上抛。
 *
 * @param sourceRoot [in] 源项目目录（逐文件读取；HEAD 用快照捕获字节
 *                   ——不重读磁盘，防并发提交换 HEAD 造成混合视图）
 * @param snapshot   [in] 快照（清单为复制范围——清单外一律不碰）
 * @param stagingDir [in] 已存在的暂存目录（同卷临时位）
 * @param atomic     [in] AtomicFile 门面（writeThrough 通道；调用方装
 *                   配——生产＝真实 Win32，测试可注入故障）
 * @param ops        [in] 目录创建通道（createDirectories）
 * @param sink       [in] 诊断 sink（可空）
 *
 * @throws StoreError StoreCorrupt（不可变文件读取失真）；DiskFull/
 *         AccessDenied/WriteRejected（写路径环境失败——映射同表）
 */
void copySnapshotFiles(const std::filesystem::path& sourceRoot,
                       const StoreSnapshot& snapshot,
                       const std::filesystem::path& stagingDir,
                       win32::AtomicFile& atomic,
                       win32::IFileOps& ops,
                       IDiagnosticsSink* sink);

/**
 * @brief 副本换新 projectId＋三处改写点（§8.8：project.json.projectId、
 *        HEAD.projectId、drafts 逐文件 projectId——D-16 冻结的改写面，
 *        元数据对象零触碰）。
 *
 * 重写形态：parse→仅改 projectId→dump 整体重写（§4.2"另存为整体重
 *   写 project.json"的契约口径；其余字段——schemaVersion/createdAtUtc/
 *   createdWithToolVersion——逐字保留）。HEAD 用快照捕获字节解析（与
 *   复制进副本的字节同源）。
 *
 * @param snapshot   [in] 快照（HEAD 字节与 drafts 清单来源）
 * @param newId      [in] 新项目身份（core 生成——调用方分配）
 * @param stagingDir [in] 副本所在暂存位（就地重写）
 * @param atomic     [in] AtomicFile 门面（writeThrough 重写通道）
 * @param sink       [in] 诊断 sink（可空）
 *
 * @throws StoreError StoreCorrupt（副本文件解析失败＝复制失真——不
 *         可达路径的防御面）；写路径环境失败同 copySnapshotFiles
 */
void rewriteProjectIdForCopy(const StoreSnapshot& snapshot,
                             const core::ProjectId& newId,
                             const std::filesystem::path& stagingDir,
                             win32::AtomicFile& atomic,
                             IDiagnosticsSink* sink);

// =====================================================================
// 临时位与发布原语（§8.8 同卷临时位/§8.10 导入临时区）
// =====================================================================

/**
 * @brief 生成目标同级的隐藏临时位路径（".<prefix>-<8hex>.tmp" 形态——
 *        实现口径⑤；不创建目录，只派生名字）。
 *
 * @param target [in] 最终目标路径（临时位与其同父目录＝同卷——rename
 *               发布的机制前提）
 * @param prefix [in] 临时位前缀 token（ASCII；如 "saveas"）
 * @return 临时位路径（8 位随机 hex 后缀——进程内单调种子＋random_device
 *         播种，TxEngine::generateTxId 同源形态）
 */
[[nodiscard]] std::filesystem::path makeTempSibling(const std::filesystem::path& target,
                                                    const char* prefix);

/**
 * @brief 整树删除临时位（尽力而为——PM-05"不留半成品"的清理半区）。
 *
 * 删除失败＝残留（句柄占用等）→ 开发诊断如实上报（channel 携带上下
 *   文），不抛——清理失败不改变"目标零写入"的既有事实，把失败升级为
 *   异常只会掩盖原始错误。
 *
 * @param dir    [in] 临时位根（不存在＝幂等成功）
 * @param ops    [in] 目录删除通道（removeTree）
 * @param sink   [in] 诊断 sink（可空）
 * @param channel [in] 开发诊断通道 token（如 "project/save-as"）
 */
void removeTreeBestEffort(const std::filesystem::path& dir,
                          win32::IFileOps& ops,
                          IDiagnosticsSink* sink,
                          const char* channel);

/**
 * @brief 发布临时位到目标（§7.2 行 2 只增发布原语——目录形态的
 *        publishNew：MoveFileExW 不带 REPLACE，目标已存在即失败）。
 *
 * @param stagingDir [in] 临时位（成功后不复存在——已改名）
 * @param targetDir  [in] 发布目标（必须不存在——存在即失败，不覆盖）
 * @param ops        [in] 文件操作通道（publishNew）
 * @param what       [in] 错误 detail 的操作名（"saveas-publish"/
 *                   "package-import-publish"）
 * @return ok==true＝已发布；ok==false＝osError 携带 Win32 原始码
 *         （目标已存在＝ERROR_ALREADY_EXISTS——调用方按实现口径⑥映射）
 */
[[nodiscard]] win32::FileResult publishDirectory(const std::filesystem::path& stagingDir,
                                                 const std::filesystem::path& targetDir,
                                                 win32::IFileOps& ops,
                                                 const char* what);

/**
 * @brief Win32 写路径原始错误码 → 稳定码 StoreError（实现口径⑥——与
 *        事务引擎/对象库同表：磁盘满→DiskFull、拒绝访问→AccessDenied、
 *        其余→WriteRejected；分类逻辑单点复用，不在服务内第二处映射）。
 *
 * @param osError [in] Win32 GetLastError()/error_code.value() 原始码
 * @param what    [in] 操作名（进 detail）
 * @param path    [in] 涉事路径（进 detail——开发定位）
 */
[[nodiscard]] StoreError osToStoreError(unsigned long osError,
                                        const std::string& what,
                                        const std::string& path);

/**
 * @brief 生产文件操作通道（进程级共享的无状态 Win32FileOps——AtomicFile
 *        默认构造同款取舍；服务工厂的默认注入源）。
 *
 * @return 共享实例指针（生存期＝进程；调用方不释放）
 */
[[nodiscard]] win32::IFileOps* defaultFileOps();

// =====================================================================
// 导出一致数据视图（D-17——ISnapshotFileSource 的 project 侧实现）
// =====================================================================

/**
 * @brief 快照视图（PackageService.hpp ISnapshotFileSource 的实现）——
 *        把冻结快照投影为包编解码实现的取数通道（清单全量、按需读出）。
 *
 * 生命周期与所有权：持快照指针（非 owning——快照由服务的单次导出调用
 *   持有，生存期覆盖本对象）与源目录拷贝。
 *
 * 线程约束：单次导出内被编解码实现串行调用（接口契约）。
 */
class SnapshotView final : public ISnapshotFileSource {
public:
    /**
     * @brief 构造视图。
     *
     * @param snapshot   [in] 冻结快照（非 owning；生存期覆盖本对象）
     * @param sourceRoot [in] 源项目目录（逐文件读取点）
     */
    SnapshotView(const StoreSnapshot& snapshot, std::filesystem::path sourceRoot);

    [[nodiscard]] std::vector<PackFileEntry> enumerate() const override;
    bool read(const std::string& packPath, std::vector<std::uint8_t>& outBytes) override;

private:
    /// packPath → relPath 解包（"payload/" 前缀校验——编解码实现违约
    /// fail-fast；快照清单内成员校验——"清单外文件一概不读"的双向契约）。
    [[nodiscard]] std::string stripPackPrefix(const std::string& packPath) const;

    const StoreSnapshot* m_snapshot;      ///< 冻结快照（非 owning）
    std::filesystem::path m_sourceRoot;   ///< 源项目目录
};

}  // namespace sdurws::ird::project::clone

#endif  // SDURWS_IRD_PROJECT_SRC_STORECLONE_HPP
