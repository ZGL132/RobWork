/**
 * @file   Policy.cpp
 * @brief  policy 单元首个翻译单元（构建落位占位——POL-T01）。
 *
 * 设计依据：
 *   - units/policy.md §12 POL-T01 行（产物＝构建落位；真实接口随 POL-T02+ 落地）
 *   - 任务契约 tasks/foundation/POL-T01.json（≙WP-07-T01）
 *
 * 背景说明：STATIC 库至少需要一个翻译单元（core/testkit/runtime/evidence 同例）；
 * 本占位不引用任何产品/框架头（编译依赖仅 core＋std，冒烟模式自洽）。
 */

namespace sdurws::ird::policy {

/// 单元锚点符号（防空翻译单元警告；无业务语义——落位期占位）。
int policyUnitAnchor() noexcept
{
    return 0;
}

}  // namespace sdurws::ird::policy
