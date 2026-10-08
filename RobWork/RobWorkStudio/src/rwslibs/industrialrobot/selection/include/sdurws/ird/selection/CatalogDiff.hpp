/**
 * @file   CatalogDiff.hpp
 * @brief  目录差异比较（selection 单元）——SEL-08"目录差异比较"的结构
 *         化差异模型（型号新增/移除/修改、字段级增量、曲线点集增量、
 *         兼容关系增量）与 CatalogDiffer 纯函数（两个不可变目录快照的
 *         会话工具——D-SEL-14，不进③端口评估器注册表）。
 *
 * 设计依据：
 *   - units/selection.md §13.6（目录差异比较——CatalogDiff 计算库纯函数：
 *     输入两个 CatalogPackageSnapshot → 结构化差异（新增/移除/修改型号、
 *     字段级增量、曲线点集增量、兼容关系增量），输出用于呈现与升级影响
 *     提示；目录更新不静默改变历史结果——历史选型结果保持原目录版本
 *     切片身份，依赖旧版本的结果按当前性规则标记 Superseded，不删除、
 *     不改写）、D-SEL-14（会话工具，不作为正式证据评估器——不滥用
 *     ③端口）、§14.9（"目录 diff/锁定管理不设评估器形态"——本头零
 *     IEngineeringEvaluator 适配，契约测试 static_assert 钉住）
 *   - 需求 SEL-08（支持目录差异比较和项目锁定版本，目录更新不应静默
 *     改变历史结果）、CON-05（内容寻址——任何业务字段变更→新内容身份
 *     →依赖该目录的切片失效；diff 非空 ⇔ 包内容身份不同，契约测试钉住
 *     该失效链）、NFR-COR-01/02（纯函数确定性）、ERR-01（比较型差异
 *     ——旧值/新值双侧承载）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 2（"目录差异
 *     比较与项目锁定版本……目录更新不静默改变历史结果用例通过"）
 *
 * ★ 会话工具边界（D-SEL-14/§13.6——本头不做什么）：
 *   1. 不落位/不改写锁定版本：锁定管理（写入即锁定只增——PA-2）的唯一
 *      本域执行面是 CatalogProvider.hpp 的 InMemoryCatalogProvider（T03
 *      落位，其语义参照未来 project 存储端口注入适配）；比较是纯只读
 *      动作，对任何供给零副作用；
 *   2. 不进③端口评估器注册表：差异结果为会话工具输出，不作为正式证据
 *      归档（§13.6——登记 D-SEL-x 面）；大目录比较 >1 s 的后台任务承载
 *      归 execution（NFR-PERF-01）——本纯函数供其同步调用（R1 无
 *      execution 落位，异步编排为后续任务面）；
 *   3. 不做导入期校验：引用完整性/单位/范围等八码族校验是导入期职责
 *      （T03 CatalogValidation）；compare 的输入契约是两个"已经
 *      CatalogImporter::assemble 产出"的合法快照（直构快照须满足同样
 *      不变量——包内容身份有效、主表内 modelId 唯一，违反即 fail-fast）。
 *
 * 线程安全：CatalogDiffer 无状态纯函数对象（可重入——卡 §14.10）；
 * 全部输入由调用方持有，输出按值返回（卡 §14.0 所有权约定）。
 */

#ifndef IRD_SELECTION_CATALOGDIFF_HPP
#define IRD_SELECTION_CATALOGDIFF_HPP

#include <string>
#include <vector>

#include <sdurws/ird/selection/CatalogTypes.hpp>

namespace sdurws::ird::selection {

// =====================================================================
// 差异模型（§13.6 四类增量的承载——呈现与升级影响提示的消费面）
// =====================================================================

/// 型号差异类别（电机/减速器主表共用——主表行级别的三态）。
enum class ModelDiffKind {
    Added,     ///< 目标版本新增的型号（from 无 to 有）
    Removed,   ///< 目标版本移除的型号（from 有 to 无）
    Modified,  ///< 双方皆有但业务字段存在差异（fieldChanges 非空）
};

/**
 * @brief 字段级增量（§13.6"字段级增量"——修改型号的逐字段差异行）。
 *
 * 值双侧承载（ERR-01 精神——旧值/新值齐备，呈现层无需回查任一快照）：
 * 数值字段的文本为 canonical 定点格式（std::to_chars 最短 round-trip，
 * 与 canonicalPackageText 数值格式化同源——同值必同文本）；可缺失字段
 * 的一侧缺失以 kDiffAbsent 哨兵文本承载（与 Preference.hpp 的
 * kPrefAnnotationAbsent 同款"缺失是事实陈述"纪律）；文本字段原样。
 */
struct FieldChange {
    std::string field;    ///< 业务字段名（Motor/GearboxCatalogEntry 成员路径，
                          ///  如 "ratedTorque"/"overload.torque"/"mounting.flangeKind"）
    std::string oldText;  ///< 基线值规范文本（数值＝canonical 定点；缺失＝kDiffAbsent）
    std::string newText;  ///< 目标值规范文本（同上）

    bool operator==(const FieldChange& o) const
    {
        return field == o.field && oldText == o.oldText && newText == o.newText;
    }
    bool operator!=(const FieldChange& o) const { return !(*this == o); }
};

/// 可缺失字段一侧缺失的哨兵文本（"(absent)"——ASCII 哨兵，呈现层可识别；
/// 差异值文本域为 canonical 格式化结果，不会自然产生该串）。
inline constexpr const char* kDiffAbsent = "(absent)";

/**
 * @brief 型号差异条目（§13.6"新增/移除/修改型号"）。
 *
 * kind==Modified 时 fieldChanges 按字段注册表序排列（比较执行序＝电机
 * 20 字段/减速器 18 字段的固定登记序——确定性，NFR-COR-02）；Added/
 * Removed 时 fieldChanges 为空（型号实体可按 modelId 从对应快照取得，
 * 不重复承载——登记单元卡 §19.3 T07）。
 */
struct ModelDiff {
    ModelDiffKind kind = ModelDiffKind::Modified; ///< 差异类别
    bool isMotor = true;      ///< true＝电机主表（MotorCatalogEntry）；false＝减速器主表
    ModelId modelId;          ///< 型号稳定 ID（(catalogId, version, modelId) 三元组的键——
                              ///  跨版本同 ID 同型号，卡 §4.3）
    std::vector<FieldChange> fieldChanges; ///< 字段级增量（Modified 非空；按注册表序）

    bool operator==(const ModelDiff& o) const
    {
        return kind == o.kind && isMotor == o.isMotor && modelId == o.modelId
            && fieldChanges == o.fieldChanges;
    }
    bool operator!=(const ModelDiff& o) const { return !(*this == o); }
};

/**
 * @brief 曲线差异条目（§13.6"曲线点集增量"；目录能力曲线变更→依赖该
 *        曲线的切片失效——evidence §5.3"目录能力曲线"行的可定位判据，
 *        本结构给出点级定位）。
 *
 * 点级增量语义（同 x 变 y 的点对＝"移除旧值＋加入新值"两记——removed
 * 含 (x, y_old)、added 含 (x, y_new)，呈现层可直接理解）；仅头部字段
 * （量纲/单位）变化时 kind=Modified 且点增量为空、fieldChanges 非空。
 * Added/Removed 时点集与字段增量均为空（曲线实体可按 curveId 从对应
 * 快照取得——不重复承载，登记单元卡 §19.3 T07）。
 */
struct CurveDiff {
    /// 曲线差异类别（Added/Removed/Modified——同 ModelDiffKind 三态）。
    enum class Kind { Added, Removed, Modified };

    Kind kind = Kind::Modified; ///< 差异类别
    CurveId curveId;            ///< 曲线稳定 ID（包内唯一键）
    std::vector<FieldChange> fieldChanges;   ///< 头部字段增量（xQuantity/xUnit/yQuantity/
                                             ///  yUnit——Modified 时可非空，注册表序）
    std::vector<CapabilityPoint> addedPoints;   ///< 点级新增（按 x 升序；含同 x 变 y 的新值点）
    std::vector<CapabilityPoint> removedPoints; ///< 点级移除（按 x 升序；含同 x 变 y 的旧值点）

    bool operator==(const CurveDiff& o) const
    {
        return kind == o.kind && curveId == o.curveId && fieldChanges == o.fieldChanges
            && addedPoints == o.addedPoints && removedPoints == o.removedPoints;
    }
    bool operator!=(const CurveDiff& o) const { return !(*this == o); }
};

/**
 * @brief 兼容关系差异条目（§13.6"兼容关系增量"——全键三列的差异）。
 *
 * 键＝(motorId, gearboxId, mountKind) 全键（同型号对可声明多个安装关系
 * 词表值——键取全列避免把"新增安装方式"误判为"无差异"）；关系只有
 * 存在/不存在两态，无 Modified。
 */
struct CompatibilityDiff {
    /// 兼容关系差异类别（只有存在性两态）。
    enum class Kind { Added, Removed };

    Kind kind = Kind::Added;      ///< 差异类别
    CompatibilityRecord record;   ///< 差异关系行（全键三列——motorId/gearboxId/mountKind）

    bool operator==(const CompatibilityDiff& o) const
    {
        return kind == o.kind && record == o.record;
    }
    bool operator!=(const CompatibilityDiff& o) const { return !(*this == o); }
};

/**
 * @brief 目录结构化差异（§13.6 CatalogDiff——两个不可变快照的完整差异）。
 *
 * 空差异语义：empty()==true ⇔ 三表差异皆空 ⇔ 两快照业务内容相同（含
 * 内容身份相同——CON-05 失效链：diff 非空 ⇔ computePackageContentIdentity
 * 不同，契约测试钉住）。
 */
struct CatalogDiff {
    CatalogIdentity fromIdentity; ///< 基线快照身份（manifest.identity——含内容摘要）
    CatalogIdentity toIdentity;   ///< 目标快照身份（同上）
    std::vector<ModelDiff> models;                ///< 型号差异（(isMotor 电机先)→modelId→kind 升序）
    std::vector<CurveDiff> curves;                ///< 曲线差异（curveId 升序）
    std::vector<CompatibilityDiff> compatibility; ///< 兼容差异（motorId→gearboxId→mountKind 升序）

    /// 空差异判定：三表皆空（两快照业务内容相同）。
    bool empty() const noexcept
    {
        return models.empty() && curves.empty() && compatibility.empty();
    }

    bool operator==(const CatalogDiff& o) const
    {
        return fromIdentity == o.fromIdentity && toIdentity == o.toIdentity
            && models == o.models && curves == o.curves
            && compatibility == o.compatibility;
    }
    bool operator!=(const CatalogDiff& o) const { return !(*this == o); }
};

// =====================================================================
// 目录差异比较器（§13.6 纯函数——会话工具，D-SEL-14）
// =====================================================================

/**
 * @brief 目录差异比较器（§13.6 CatalogDiff 纯函数的唯一产品实现）。
 *
 * 比较语义（登记单元卡 §19.3 T07）：
 *  - 型号：键＝modelId，电机/减速器两主表独立比较。业务字段比较集＝
 *    固定注册表（电机 20 组/减速器 18 组——成员路径见 src/CatalogDiff.cpp
 *    注册表；含 curves 引用列表，引用列表按 curveId 排序后序列化比较
 *    ——引用语义是集合，列表序不参与语义）。**不比较**：catalog 身份
 *    字段（版本演进必然不同——非内容差异）、missing 清单与 status
 *    （导入期派生标记——其变化已由对应字段值变化承载）；
 *  - 曲线：键＝curveId；Modified 判定＝头部字段或点集或内容身份任一
 *    不同；点级增量按 x 匹配（双方点集 x 严格升序——装配/构造入口保证）；
 *  - 兼容关系：全键三列存在性比较；
 *  - manifest 级字段（formatVersion/version/source/files/fieldDictionary）
 *    不进差异条目：版本演进必然差异，否则 diff 恒非空失去"变更检测"
 *    语义——版本信息由 fromIdentity/toIdentity 承载（呈现层对照展示）；
 *  - 输出序（NFR-COR-02 确定性）：models 按 (isMotor 电机先)→modelId→
 *    kind 枚举序；curves 按 curveId；compatibility 按 (motorId, gearboxId,
 *    mountKind)。
 *
 * @param from [in] 基线快照（历史锁定版本；调用方持有，只读）
 * @param to   [in] 目标快照（新版锁定版本；调用方持有，只读）
 * @return 结构化差异（from/to 身份＋三表增量；同快照输入＝empty diff）
 *
 * @throws std::invalid_argument 调用方契约违约（fail-fast——卡 §14.0）：
 *         ①from/to 任一快照包内容身份无效（全零——无身份快照不可比较，
 *         CON-05）；②任一快照主表内 modelId 重复（装配产物唯一性前提
 *         破坏——assemble 已拒绝 DUPLICATE-ID，直构快照同责）。
 *
 * @note 纯函数：同输入恒同输出（NFR-COR-01/02）；可重入；对锁定供给
 *       与项目存储零副作用（会话工具——D-SEL-14）。本类不适配
 *       evidence::IEngineeringEvaluator（不进③端口注册表——§14.9，
 *       契约测试 static_assert 钉住）。
 */
class CatalogDiffer final {
public:
    CatalogDiff compare(const CatalogPackageSnapshot& from,
                        const CatalogPackageSnapshot& to) const;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_CATALOGDIFF_HPP
