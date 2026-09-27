/**
 * @file   PolicyNameContexts.hpp
 * @brief  策略名称上下文适配器（policy::IPolicyNameContext 的建模侧实现
 *         ——WP-24-T03b 收口：草稿桩退役，接 runtime::RuntimeNameMap 真端口）。
 *
 * 设计依据：
 *   - units/ui.md §16.7 v1.15④"仍余（阶段 E）：策略装载与 runtime 名称适配
 *     （就绪 L11/L 行程全量）"——本头即名称适配半区的落点；
 *   - units/policy（Contexts.hpp）IPolicyNameContext 三方法冻结签名——
 *     runtime ⑥端口转发形（R-4/P-POL-8：整串精确匹配、不拆前缀不猜测）；
 *   - runtime NameMap.hpp RuntimeNameMap 四查询入口（resolveRuntimeName/
 *     resolveObjectId/contentIdentity——并发只读安全、确定性）。
 *
 * 背景（为什么需要适配器而不是直接用映射）：行程评估器（policy 侧）只认
 * IPolicyNameContext 抽象面；映射本体归 runtime（PA-1 名称权威），建模侧
 * 不得私建第二构造路径——适配器只做"映射在位则转发、不在位则如实 nullopt"
 * 的空值翻译，零判定零缓存（ACC5）。
 *
 * 诚实边界：映射的运行期来源＝确定性编译产物（buildRuntimeNameMap——归
 * UI-T20 宿主运行时发布桥接线）；本适配器交付前，宿主未绑定映射时三方法
 * 分别如实返回 nullopt/nullopt/全零保留值——与退役前的草稿桩可观测行为
 * 一致，但已是真端口消费面（映射一旦绑定，L 行程即全量可用），不是占位。
 *
 * 线程约束：RuntimeNameMap 并发只读安全（NameMap.hpp 类注释）；本适配器
 * 成员为构造后只读指针——resolveObjectId/resolveRuntimeName 可与其余线程
 * 并发（就绪汇聚 domainReadiness 的跨线程拉取路径由此保证，§6.5 线程契约）。
 */
#ifndef IRD_MODELING_PLUGIN_POLICYNAMECONTEXTS_HPP
#define IRD_MODELING_PLUGIN_POLICYNAMECONTEXTS_HPP

#include <optional>
#include <string>

#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId/ContentIdentity（值面）
#include <sdurws/ird/policy/Contexts.hpp>     // policy::IPolicyNameContext（三方法契约）
#include <sdurws/ird/runtime/NameMap.hpp>     // runtime::RuntimeNameMap（⑥端口真身）

namespace sdurws {
namespace ird {
namespace modeling {

/**
 * @brief 运行时映射名称上下文（IPolicyNameContext 的 RuntimeNameMap 转发形
 *        ——就绪行程 L 行的名称解析真端口）。
 *
 * 生命周期/所有权：非 owning——构造注入映射指针（调用方保证存活期覆盖
 * 本上下文；nullptr＝未绑定，查询走空值轨）。不可变面：指针构造后不改写
 * （绑定切换经模块整体重建就绪上下文完成——装配期一次，SA-01 同纪律）。
 */
class RuntimeMapPolicyNameContext final : public policy::IPolicyNameContext {
public:
    /**
     * @param map [in] 运行时名称映射（非 owning；nullptr＝未绑定——三方法
     *                如实空值应答，不猜测——ARC-04）
     */
    explicit RuntimeMapPolicyNameContext(const runtime::RuntimeNameMap* map)
        : m_map(map)
    {
    }

    /**
     * @brief 运行时名 → 对象身份（转发 RuntimeNameMap::resolveRuntimeName
     *        ——整串精确匹配；未命中/未绑定＝nullopt，不猜测）。
     */
    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        if (m_map == nullptr) {
            return std::nullopt;  // 未绑定映射＝名称不可解析（如实空——不猜）
        }
        const auto resolved = m_map->resolveRuntimeName(runtimeName);
        if (!resolved.ok()) {
            return std::nullopt;  // UnknownObject＝映射中无该名称（ARC-04 不猜）
        }
        return resolved.get().objectId;
    }

    /**
     * @brief 对象身份 → 运行时名（转发 RuntimeNameMap::resolveObjectId
     *        ——身份作用域条目全名；未命中/未绑定＝nullopt）。
     */
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        if (m_map == nullptr) {
            return std::nullopt;
        }
        const auto resolved = m_map->resolveObjectId(object);
        if (!resolved.ok()) {
            return std::nullopt;
        }
        return resolved.get().fullName;
    }

    /**
     * @brief 名称映射内容身份（CON-06——转发 RuntimeNameMap::contentIdentity
     *        ；未绑定＝全零保留值，与"空映射"在本类型层同态——NameMap.hpp
     *        §查询入口 4/4 的既定口径，非自造哨兵）。
     */
    core::ContentIdentity nameMapContentIdentity() const override
    {
        return m_map != nullptr ? m_map->contentIdentity() : core::ContentIdentity{};
    }

private:
    const runtime::RuntimeNameMap* m_map;  ///< 映射（非 owning——见类注释）
};

}  // namespace modeling
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_MODELING_PLUGIN_POLICYNAMECONTEXTS_HPP
