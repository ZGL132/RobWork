/**
 * @file   CatalogProvider.hpp
 * @brief  目录快照供给与业务校验接口（selection 单元）——ICatalogProvider/
 *         ICatalogValidator（卡 §14.1/§14.2 设计基线签名）＋v1 导入校验器/
 *         装配器实现＋内存锁定版本供给（版本锁定语义载体）＋P-IO-7 注册面。
 *
 * 设计依据：
 *   - units/selection.md §5.1（固定交接流程——io 文件层 → selection 字段
 *     映射 → 业务校验 → CatalogPackageSnapshot）、§5.2（P-IO-7 注册义务
 *     ——目录包文件清单/文件名结构契约由本卡注册）、§5.3（业务校验清单
 *     八码）、§6.3（曲线校验四码）、§14.1（ICatalogProvider）、§14.2
 *     （ICatalogValidator——输入 io 解析后的字段与来源身份，不接触文件
 *     系统）、§14.10（线程与生命周期——纯函数可重入）
 *   - 需求 SEL-01/SEL-02（目录包模板与导入校验）、AT-08（目录导入/错误
 *     字段/版本锁定）、ERR-01、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 1～3（导入校验
 *     用例/插值禁外推/分工不越界）
 *
 * ★ ICatalogProvider 的落位边界（诚实登记）：load(listLocked) 的完整实现
 *   依赖 project 存储端口（catalog/<id>/<ver>/ 落位与引用保护——卡 §4.2/
 *   §13.5），而 selection 零 project 编译边（卡 §3.2"运行时注入/端口"
 *   列），真实存储适配随 WP-19-T07（目录版本管理）经注入落位。本头落位：
 *   ①接口签名（卡 §14.1 逐注承载）；②InMemoryCatalogProvider——进程内
 *   锁定语义实现（写入即锁定只增、锁定后不可变、内容摘要不符拒绝），
 *   供 WP-19-T03 版本锁定用例与 T04/T05 筛选/组合消费，也是未来存储
 *   适配的语义参照（PA-2 不可变历史在本域的执行面）。
 *
 * 线程安全：CatalogImporter 与 LinearCurveEvaluator 为无状态纯函数对象
 * （可重入，卡 §14.10）；InMemoryCatalogProvider 非线程安全（锁定表
 * 可变——仅装配/测试单线程使用，注释与卡 §14.10"命令服务串行槽"外
 * 的注入面一致）。
 */

#ifndef IRD_SELECTION_CATALOGPROVIDER_HPP
#define IRD_SELECTION_CATALOGPROVIDER_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Curve.hpp>

namespace sdurws::ird::selection {

// =====================================================================
// §14.2 ICatalogValidator（业务校验——纯函数；不接触文件系统）
// =====================================================================

/**
 * @brief 目录包业务校验接口（卡 §14.2 设计基线——签名逐注承载）。
 *
 * 输入是 io 文件层校验通过的解析结果（ParsedCatalogInput——纯 std 形态，
 * L5 装配层自 io::RawTable/JsonDocument 映射）与 manifest（携带字段字典
 * 注册面 C-6）；输出逐项可定位报告。零文件系统访问——分工不越界的接口
 * 面（卡 §5.1）。
 */
class ICatalogValidator {
public:
    virtual ~ICatalogValidator() = default;

    /**
     * @brief 执行 §5.3 业务校验全表（schema/单位/必填/唯一性/范围/引用/
     *        兼容冲突/曲线——八码族＋曲线四码族）。
     *
     * @param parsed   [in] io 解析后的目录包数据（必备五表——缺表＝调用方
     *                 装配违约，见 ParsedCatalogInput 头注）
     * @param manifest [in] 包清单（formatVersion/catalogId/version/来源/
     *                 文件清单/字段字典——SEL-01）
     * @return 校验报告（issues 按 (file,rowNo,column,code) 稳定序；空＝通过）
     *
     * @throws std::invalid_argument schema 级致命结构错误（fail-fast——卡
     *         §14.2 note）：未知 formatVersion（携带升级指引文本）、必备
     *         解析表缺失、字段字典与必备文件不对位、manifest 声明的必备
     *         文件条目缺失。数据类错误（字段缺失/单位非法/越界/引用悬空
     *         等）一律入报告不抛——卡 §14.0 错误二分（调用方错误 fail-fast
     *         vs 数据/环境类返回诊断）。
     *
     * @note 纯函数：同输入恒同输出（NFR-COR-01/02）；可重入；无副作用。
     */
    virtual CatalogValidationReport validate(const ParsedCatalogInput& parsed,
                                             const CatalogManifest& manifest) const = 0;
};

/**
 * @brief v1 目录包导入校验器（ICatalogValidator 的唯一产品实现——
 *        §5.3/§6.3 全表；同时提供校验通过后的快照装配）。
 *
 * 校验→装配两段式（卡 §5.1 流程的 selection 段）：validate 报告空后
 * 才允许 assemble（未校验即装配＝调用方契约违约，抛 fail-fast）。
 */
class CatalogImporter final : public ICatalogValidator {
public:
    CatalogValidationReport validate(const ParsedCatalogInput& parsed,
                                     const CatalogManifest& manifest) const override;

    /**
     * @brief 由解析输入组装不可变目录快照（卡 §5.1"业务校验→快照"段；
     *        SEL-01 四表＋字段字典＋版本＋来源的完整落地）。
     *
     * 装配动作：字段映射（列名→业务字段）→ 数值解析（SI 域）→ 条目
     * missing 清单标记 → 条目排序（modelId/curveId/兼容对升序——确定性）→
     * 曲线内容身份与包内容身份计算回填 → 条目 status 定级
     * （Valid/Partial——missing 非空即 Partial）。
     *
     * @param parsed   [in] 解析输入（同 validate）
     * @param manifest [in] 包清单（同 validate；identity.contentIdentity
     *                 输入值被装配计算值回填覆盖——包身份唯一权威＝快照
     *                 规范序列化摘要，卡 §4.2）
     * @return 不可变快照（调用方持有；经 InMemoryCatalogProvider::lockVersion
     *         锁定或交 project 端口落位）
     *
     * @throws std::invalid_argument 前置校验未通过/未执行（调用方契约违约
     *         ——先 validate 后 assemble），或装配期遭遇 validate 应已拒绝
     *         的数据（内部一致性破坏——防御性 fail-fast，NFR-COR-03 不静默）。
     *
     * @note 纯函数；确定性；不触达 project/io（落位归上层端口）。
     */
    CatalogPackageSnapshot assemble(const ParsedCatalogInput& parsed,
                                    const CatalogManifest& manifest) const;
};

// =====================================================================
// §14.1 ICatalogProvider（目录快照供给）
// =====================================================================

/**
 * @brief 目录快照只读供给接口（卡 §14.1 设计基线——签名逐注承载）。
 *
 * 完整实现（project 对象库形态）随 WP-19-T07 经存储端口注入落位；本头
 * 另落位 InMemoryCatalogProvider（进程内锁定语义——见其头注）。
 */
class ICatalogProvider {
public:
    virtual ~ICatalogProvider() = default;

    /**
     * @brief 按锁定版本对象解析目录业务模型（卡 §14.1 @return 注——
     *        不可变快照；调用方持有返回值）。
     * @throws std::invalid_argument 锁定对象不存在/内容摘要不符（引用
     *         完整性破坏——fail-fast，卡 §14.1 @throws 注）
     * @note 线程：ConcurrentReadOnly（快照不可变共享）；R1；无副作用。
     */
    virtual CatalogPackageSnapshot load(const CatalogVersion& lock) const = 0;

    /// 项目内已锁定版本清单（卡 §14.1；catalogId→version 升序确定性序）。
    virtual std::vector<CatalogVersion> listLocked() const = 0;
};

/**
 * @brief 进程内锁定版本供给（ICatalogProvider 的内存实现——版本锁定
 *        语义载体，卡 §4.2"写入即锁定、只增"的本域执行面）。
 *
 * 锁定纪律（AT-08"版本锁定"的判定面）：
 *   1. lockVersion：同一 (catalogId, version) 只允许锁定一次——重复锁定
 *      即拒绝（写入即锁定只增，PA-2 不可变历史）；
 *   2. 锁定后快照不可变：load 返回锁定时的冻结拷贝，调用方对自身副本的
 *      任何修改不影响锁定版本（load 幂等——同锁同快照）；
 *   3. 引用完整性（卡 §14.1）：load 入参 identity.contentIdentity 与锁定
 *      记录不符（或锁不存在）→ fail-fast 拒绝。
 *
 * 非线程安全：内部锁定表可变——仅装配/测试单线程上下文使用（真实并发
 * 面归 project 存储端口形态，WP-19-T07）。
 */
class InMemoryCatalogProvider final : public ICatalogProvider {
public:
    /**
     * @brief 锁定一个目录版本（写入即锁定；锁定后入参快照与锁定副本独立）。
     * @param snapshot [in] 待锁定快照（须已经 CatalogImporter::assemble
     *                 产出——包内容身份必须有效，防锁定无身份快照）
     * @param lockObjectId [in] 锁定对象 ID（调用方分配——真实形态为 project
     *                 对象库 ID；内存实现仅承载值语义）
     * @return 锁定版本引用（锁定记录的只读投影）
     *
     * @throws std::invalid_argument 同 (catalogId, version) 已锁定（只增
     *         纪律），或快照包内容身份无效（全零——无身份不得锁定，CON-05）。
     */
    CatalogVersion lockVersion(const CatalogPackageSnapshot& snapshot,
                               core::ObjectId lockObjectId);

    CatalogPackageSnapshot load(const CatalogVersion& lock) const override;
    std::vector<CatalogVersion> listLocked() const override;

private:
    /// 锁定记录（快照冻结副本＋引用形态；追加序＝锁定序——只增不删，PA-2）。
    struct LockedEntry {
        CatalogVersion reference;        ///< 锁定版本引用（identity＋lockObjectId）
        CatalogPackageSnapshot snapshot; ///< 锁定时的快照冻结副本（之后不再变化）
    };
    std::vector<LockedEntry> locked_;    ///< 锁定表（追加序；遍历输出时按升序重排）
};

// =====================================================================
// §14.3 曲线构造入口（PerformanceCurve 的唯一校验构造——卡 §6.1/§6.3）
// =====================================================================

/**
 * @brief 能力曲线统一构造入口（卡 §6.1"构造入口排序＋校验"——§6.3 全表
 *        校验；§6.3 行 1 明确"不代排序：拒绝，要求目录修正"）。
 *
 * @param curveId    [in] 曲线稳定 ID（包内唯一性由导入校验层保证）
 * @param xQuantity  [in] 横坐标量纲 token（v1 词表 speed/torque/power）
 * @param yQuantity  [in] 纵坐标量纲 token
 * @param xUnit      [in] 横坐标 SI 单位 token（core 词表——UnitToken::find
 *                   未命中即拒绝：SEL-CATALOG-UNIT-INVALID）
 * @param yUnit      [in] 纵坐标 SI 单位 token
 * @param points     [in] 采样点（按 x 升序提交——违反即拒绝，不代排序）
 * @param catalog    [in] 所属目录版本（曲线版本＝所属目录版本，卡 §6.1）
 * @param reject     [out] 失败时的定位发现（成功时不清写——调用方以返回值判别）
 *
 * @return 成功返回曲线（含点集内容身份）；失败返回 nullopt 且 reject 携带
 *         首个失败码（SEL-CURVE-UNORDERED/DUP-X/NONFINITE/INTERVAL-INVALID
 *         或 SEL-CATALOG-UNIT-INVALID——多失败并存时报首个，确定性序）。
 *
 * @note 纯函数；确定性。曲线点数 < 2（含单点曲线）＝INTERVAL-INVALID
 *       （单点能力值应走"固定额定值"口径——卡 §6.4，不得以曲线形态声明）。
 */
std::optional<PerformanceCurve> tryMakePerformanceCurve(
    const CurveId& curveId, std::string xQuantity, std::string yQuantity,
    std::string xUnit, std::string yUnit, std::vector<CapabilityPoint> points,
    const CatalogIdentity& catalog, CatalogIssue& reject);

// =====================================================================
// P-IO-7 注册面（目录包文件清单 schema——卡 §5.2 的物化数据）
// =====================================================================

/**
 * @brief v1 目录包文件清单注册数据（卡 §5.2 表的物化——P-IO-7 消账面）。
 *
 * io 卡 §7.8 明示"具体文件名/清单 schema 由 selection 卡注册"——本函数
 * 返回 v1 注册形态（五文件：名称/角色/必备性/通道）；L5 装配层把该数据
 * 交给 io 清单核对框架执行文件层核对（文件在包内的存在性/必备性/路径
 * 防护），selection 不重复执行文件层核对（分工不越界——卡 §5.1）。
 * io.md 侧的同步登记随装配动作进行（卡 §5.2 注——本卡不直接修改 io.md）。
 *
 * @return 清单条目集（序＝卡 §5.2 表行序——manifest/motors/gearboxes/
 *         curves/compatibility；确定性序，NFR-COR-02）
 */
std::vector<ManifestEntry> catalogPackageFileSchema();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_CATALOGPROVIDER_HPP
