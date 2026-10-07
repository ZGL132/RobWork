/**
 * @file   Types.cpp
 * @brief  optimization 基础词表实现——阶段 token 与域异常（WP-20-T03）。
 *
 * 设计依据：units/optimization.md §4.3（阶段 token "stage-b"/"stage-d"）、
 * §12.3（错误语义——fail-fast 异常携带稳定码）；DiagCodes.hpp 码值常量
 * （唯一书写点——调用方传入，本文件不持码值字面量）。
 *
 * 确定性：token 为编译期字面量，与枚举值的对应关系恒定（NFR-COR-02）。
 */

#include <sdurws/ird/optimization/Types.hpp>

#include <utility>

namespace sdurws::ird::optimization {

std::string_view toToken(OptimizationStage s) noexcept
{
    // 阶段稳定 token（卡 §4.3 config.opt canonical 的阶段编码要素）——
    // 编译期字面量，永不更改（进缓存键面，改名即全体身份漂移）。
    switch (s) {
    case OptimizationStage::StageB:
        return "stage-b";
    case OptimizationStage::StageD:
        return "stage-d";
    }
    // 不可达分支：枚举只有两个值；为未处理枚举值（未来追加——表尾追加
    // 纪律下的新值）兜底返回 StageB token 会让错误静默，故按域内约定
    // 断言失败路径不可达——返回空串并由消费方显式判空（防御式，不吞错）。
    return {};
}

OptimizationError::OptimizationError(std::string_view stableCode, const std::string& message)
    : std::runtime_error(message), m_stableCode(stableCode)
{
    // 码值与消息都为构造期拷贝——异常对象不可变（可安全跨线程传递，§12.3）。
    // stableCode 的取值域契约（DiagCodes.hpp 常量）由调用方保证；此处不校验
    // 注册表（注册表查询归 diagnostics 工厂面，异常轨不依赖装配状态）。
}

}  // namespace sdurws::ird::optimization
