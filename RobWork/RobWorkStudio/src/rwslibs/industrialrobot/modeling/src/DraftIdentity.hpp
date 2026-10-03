/**
 * @file   DraftIdentity.hpp
 * @brief  草稿对象确定性身份与种子连杆（单元内共享实现——Template.cpp
 *         模板建链与 StructureEdit.cpp 结构编辑共用的单一实现；UI-T47
 *         从 Template.cpp 匿名空间提升，行为零变化）。
 *
 * 设计依据：
 *   - units/modeling.md §5.2（临时句柄纪律——"内存编辑态先用临时句柄，
 *     提交时回填"；§5.2 v0.28 结构编辑段引用同款句柄）；
 *   - §14.6 v0.6 ②i（WP-13-T05 导入路径先例——确定性散列派生 128 位、
 *     不经随机源）；§14.4 D-MDL-7（origin 恒位姿种子等设计默认值登记）；
 *   - NFR-MNT-04（单一实现——两 TU 共用，禁第二拷贝）。
 *
 * 单元内私有头（src/——R-2 不跨单元暴露）；两函数均为纯函数、确定性
 * （同输入同输出——NFR-COR-02）、线程安全。
 */

#ifndef IRD_MODELING_SRC_DRAFTIDENTITY_HPP
#define IRD_MODELING_SRC_DRAFTIDENTITY_HPP

#include <string>

#include <sdurws/ird/core/Digest.hpp>      // ContentDigester/Digest256（SHA-256 单一摘要路径）
#include <sdurws/ird/core/Identity.hpp>    // ObjectId（临时句柄承载）
#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // defaultMaterialDensity——材料密度默认表（§5.3 单一权威）
#include <sdurws/ird/modeling/RobotDesign.hpp>  // LinkEntry/MaterialRef（种子连杆值面）

namespace sdurws::ird::modeling {

/**
 * @brief 由稳定键派生草稿对象的临时 ObjectId（确定性——同键同句柄）。
 *
 * 派生规则：对稳定键取 SHA-256，摘要前 16 字节即 ObjectId 字节（§5.2
 * "内存编辑态先用临时句柄，提交时回填"；真实 ObjectId 仍由命令 prepare
 * 阶段经 HandlerContext.objectId() 分配回填——PA-1，本句柄不外泄为
 * 持久身份）。全零摘要命中（保留值）在 SHA-256 下实际不可达，置尾字节
 * 1 作确定性兜底（同键仍同句柄，保留值不出现）。
 *
 * 稳定键约定：含批次命名空间前缀防跨流撞句柄——模板路径
 * "ird/modeling/template-draft/<templateId>/<类别>/<序>"；结构编辑路径
 * "ird/modeling/structure-draft/<类别>/<序>"（UI-T47）。
 */
inline core::ObjectId deriveDraftObjectId(const std::string& stableKey)
{
    core::ContentDigester digester;
    digester.update(stableKey.data(), stableKey.size());
    const core::Digest256 digest = digester.finalize();

    core::ObjectId id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.bytes[i] = digest[i];  // 摘要前 16 字节（确定性——不用随机源）
    }
    if (id.bytes == core::ObjectId{}.bytes) {
        id.bytes[15] = 1;  // 保留值（全零）兜底——确定性不改（实际不可达）
    }
    return id;
}

/**
 * @brief 种子连杆（§5.1 建链同构——物性 NotProvided 不猜测＋材料种子钢
 *        取 §5.3 默认表单点权威）。
 *
 * @param stableKey  [in] 临时句柄派生键（见 deriveDraftObjectId）
 * @param localName  [in] 局部名（调用方按链序命名——"base"/"l<序>"）
 * @param provenance [in] 来源标记（模板轨迹/结构编辑批次标记）
 * @return 种子连杆（几何/物性缺省＝NotProvided 面）
 *
 * @throws std::logic_error 材料表缺登记键 steel（表被改坏——实现缺陷
 *         fail-fast，不静默产出无材料种子）
 */
inline LinkEntry makeSeedLink(const std::string& stableKey,
                              const std::string& localName,
                              const core::ValueProvenance& provenance)
{
    LinkEntry link;
    link.objectId = deriveDraftObjectId(stableKey);
    link.localName = localName;

    const std::optional<double> steelDensity = defaultMaterialDensity("steel");
    if (!steelDensity.has_value()) {
        // 材料表键"steel"是本单元 §5.3 表内登记键——查不到＝表被改坏的
        // 实现缺陷，fail-fast（§5.1 材料默认句失守不静默）。
        throw std::logic_error("modeling/template/material-table: 默认材料表"
                               "缺少登记键 steel（实现缺陷）");
    }
    MaterialRef steel;
    steel.materialId = "steel";
    steel.density = core::SourcedValue<double>::provided(*steelDensity, provenance);
    link.body.material = steel;
    // mass/com/inertia/visual/collision 缺省＝NotProvided/空（§5.1 种子
    // 形态——不猜测；估算经 estimate-properties 命令或用户直填）。
    return link;
}

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_SRC_DRAFTIDENTITY_HPP