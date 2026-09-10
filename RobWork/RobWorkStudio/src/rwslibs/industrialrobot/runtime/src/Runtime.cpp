/**
 * @file   Runtime.cpp
 * @brief  runtime 单元首个翻译单元（构建落位占位——RT-T01）。
 *
 * 设计依据：
 *   - units/runtime.md §12 RT-T01 行（产物＝构建落位；真实接口随 RT-T02+ 落地）、
 *     §3.4（目标与集成）
 *   - 任务契约 tasks/foundation/RT-T01.json（≙WP-06-T01）
 *
 * 背景说明：STATIC 库至少需要一个翻译单元（core/Core.cpp 同例）；本占位不引用
 * rw::math/rw::models/rwsim 头——独立冒烟模式无框架目标，两模式各自成立
 * （P-ENV-1 混链口径）；真实接口（Errors/CanonicalModel/编译器）随 RT-T02~T11
 * 逐任务落地，落位时本文件保留为单元锚点或并入实际实现。
 */

namespace sdurws::ird::runtime {

/// 单元锚点符号（防止空翻译单元的 ISO 警告；无业务语义——落位期占位）。
int runtimeUnitAnchor() noexcept
{
    return 0;
}

}  // namespace sdurws::ird::runtime
