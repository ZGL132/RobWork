/**
 * @file   Evidence.cpp
 * @brief  evidence 单元首个翻译单元（构建落位占位——EV-T01）。
 *
 * 设计依据：
 *   - units/evidence.md §12 EV-T01 行（产物＝构建落位；真实接口随 EV-T02+ 落地）
 *   - 任务契约 tasks/foundation/EV-T01.json（≙WP-05-T01）
 *
 * 背景说明：STATIC 库至少需要一个翻译单元（core/testkit/runtime 同例）；
 * 本占位不引用任何产品/框架头（编译依赖仅 core＋std，冒烟模式自洽）。
 */

namespace sdurws::ird::evidence {

/// 单元锚点符号（防空翻译单元警告；无业务语义——落位期占位）。
int evidenceUnitAnchor() noexcept
{
    return 0;
}

}  // namespace sdurws::ird::evidence
