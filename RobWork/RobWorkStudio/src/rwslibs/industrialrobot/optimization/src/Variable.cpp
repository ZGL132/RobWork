/**
 * @file   Variable.cpp
 * @brief  设计变量模型实现——R1 词表物化、两种初始化、绑定校验与补丁差异
 *         （units/optimization.md §5.2～§5.4/§8.2/§12.2；任务 WP-20-T03）。
 *
 * 逐段实现契约见同名公共头；本文件注释聚焦"为什么"与逐条目的卡面锚点。
 * 确定性：词表条目序＝卡面 §5.3 行序（StageD 尾部追加离散器件类）——词表
 * 序即登记契约（NFR-COR-02），后续任务追加条目只允许表尾追加。
 */

#include <sdurws/ird/optimization/Variable.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <sdurws/ird/optimization/CandidatePatch.hpp>  // PatchItem/CandidatePatch——
                                                       // diff 实现的完整类型
#include <sdurws/ird/optimization/DiagCodes.hpp>  // OPT-* 码值常量（唯一书写点）

namespace sdurws::ird::optimization {
namespace {

using sdurws::ird::optimization::kOptInputInvalid;
using sdurws::ird::optimization::kOptStageLocked;

/// 词表条目便捷构造（仅本文件——保持条目表逐行可读，字段缺省值集中在此）。
VariableDefinition def(BindingToken pattern, VariableKind kind, std::string unit,
                       std::string authority, std::string specRef,
                       std::string prerequisite, std::string mutexGroup = {},
                       std::vector<std::string> enumValues = {},
                       bool enabledInStageB = true, bool enabledInStageD = false,
                       bool valueMustBePositive = false)
{
    VariableDefinition d;
    d.tokenPattern = std::move(pattern);
    d.kind = kind;
    d.unitSymbol = std::move(unit);
    d.authorityFieldPath = std::move(authority);
    d.specRef = std::move(specRef);
    d.bindingPrerequisite = std::move(prerequisite);
    d.mutexGroup = std::move(mutexGroup);
    d.enumValues = std::move(enumValues);
    d.enabledInStageB = enabledInStageB;
    d.enabledInStageD = enabledInStageD;
    d.valueMustBePositive = valueMustBePositive;
    return d;
}

/// §5.3 #1b 连杆截面类型的封闭值域（modeling §5.3 SegmentSpec primitive 词表
/// ——卡面 §5.3 行 1b 原文三值；P-OPT-4 裁决前不启用类型枚举切换）。
std::vector<std::string> sectionTypeValues()
{
    return {"SolidCylinder", "HollowCylinder", "Box"};
}

/// §5.3 #8 材料封闭值域（modeling 默认材料键集——PropertyEstimation.hpp
/// §"默认材料密度表"五行原键；密度 kg/m³ 随键，由估算链消费）。
std::vector<std::string> materialValues()
{
    return {"steel", "aluminum", "cast-iron", "titanium-alloy", "engineering-plastic"};
}

/// §5.3 #6 基座安装预设封闭值域（卡面行 6 原文三值；预设轴向唯一产出点＝
/// runtime installationPresetRotation()——P-RT-4；预设与 custom 互斥 MDL-22）。
std::vector<std::string> basePresetValues()
{
    return {"ground", "inverted", "wall"};
}

/**
 * @brief R1 词表（卡 §5.3 表逐行物化；多分量字段按分量后缀细分——见
 *        Variable.hpp 文件头注"多分量变量的实例化口径"）。
 *
 * 单位符号约定：""（空串）＝按关节类型实例化的单位（#5/#5b 转动 rad／
 * 移动 m——卡面该两行单位列原文"rad（转动）/m（移动）"）；其余连续条目
 * 单位唯一（m/rad/1）。实例化后的绑定 unit 必须 isValid（校验强制）。
 */
const std::vector<VariableDefinition>& stageBDefinitions()
{
    static const std::vector<VariableDefinition> kTable = {
        // ---- §5.3 #1 连杆截面尺寸（连续，m；Primitive 前提——P-OPT-4：
        //      尺寸变量走"绑定 Primitive 几何＋估算链"通道）----
        def("mdl.link[i].section.size", VariableKind::Continuous, "m",
            "robot-design/links[i]/shape/section/size",
            "units/optimization.md §5.3 #1",
            "仅当该连杆 shape 为 Primitive 时可绑定（Mesh 基线→锁定＋警告 "
            "OPT-VAR-UNBINDABLE——基线核验归 Preflight #14）；物性重估算经 "
            "mdl-property-formula/1（P-OPT-2/P-OPT-4 通道）"),
        // ---- §5.3 #1b 连杆截面类型（枚举；P-OPT-4 裁决前两阶段均不启用——
        //      条目保留登记不删除，引用即 OPT-STAGE-LOCKED）----
        def("mdl.link[i].section.type", VariableKind::Enumeration, "",
            "robot-design/links[i]/shape/section/type",
            "units/optimization.md §5.3 #1b",
            "R1 绑定基线已有类型不启用类型枚举切换（§16.3 P-OPT-4——建模侧"
            "无持久化截面字段，schema 增量裁决前锁定）",
            {}, sectionTypeValues(), false, false),
        // ---- §5.3 #2/#3 DH 长度/偏置（连续，m；StandardDH 权威前提；
        //      与同关节安装位置变量权威互斥 I-OPT-7/I-MDL-8）----
        def("mdl.joint[i].dh.a", VariableKind::Continuous, "m",
            "robot-design/joints[i]/dh/a", "units/optimization.md §5.3 #2",
            "基线 authority==StandardDH 才可绑定；显式权威基线→不可绑定＋"
            "引导用 joint-placement 变量（I-MDL-8）",
            "joint[i].dh-vs-origin"),
        def("mdl.joint[i].dh.d", VariableKind::Continuous, "m",
            "robot-design/joints[i]/dh/d", "units/optimization.md §5.3 #3",
            "同 §5.3 #2（StandardDH 权威；Explicit 态只读派生）",
            "joint[i].dh-vs-origin"),
        // ---- §5.3 #4 关节安装位置（连续 3 分量，m；Explicit 权威前提；
        //      与同关节 DH 变量互斥激活）----
        def("mdl.joint[i].origin.t.x", VariableKind::Continuous, "m",
            "robot-design/joints[i]/origin/t/x", "units/optimization.md §5.3 #4",
            "基线 authority==Explicit 才可绑定；与同关节 DH 变量互斥激活"
            "（权威互斥 I-OPT-7）",
            "joint[i].dh-vs-origin"),
        def("mdl.joint[i].origin.t.y", VariableKind::Continuous, "m",
            "robot-design/joints[i]/origin/t/y", "units/optimization.md §5.3 #4",
            "同 §5.3 #4（Explicit 权威；y 分量）", "joint[i].dh-vs-origin"),
        def("mdl.joint[i].origin.t.z", VariableKind::Continuous, "m",
            "robot-design/joints[i]/origin/t/z", "units/optimization.md §5.3 #4",
            "同 §5.3 #4（Explicit 权威；z 分量）", "joint[i].dh-vs-origin"),
        // ---- §5.3 #5 关节范围（连续对，转动 rad／移动 m——单位随关节类型
        //      实例化；qmin<qmax 与 MDL-06④ 硬断言同式）----
        def("mdl.joint[i].bounds.qmin", VariableKind::Continuous, "",
            "robot-design/joints[i]/bounds/qmin", "units/optimization.md §5.3 #5",
            "仅有限限位关节；qmin<qmax（MDL-06④）；continuous 关节→改用 #5b "
            "工作范围（分析消费属性不回写 bounds——V12-01）"),
        def("mdl.joint[i].bounds.qmax", VariableKind::Continuous, "",
            "robot-design/joints[i]/bounds/qmax", "units/optimization.md §5.3 #5",
            "同 §5.3 #5（上界分量）"),
        // ---- §5.3 #5b 工程工作范围（连续对，rad 语义；仅 continuous 关节；
        //      MDL-12 有限区间）----
        def("mdl.joint[i].working-range.qmin", VariableKind::Continuous, "",
            "robot-design/joints[i]/workingRange/qmin",
            "units/optimization.md §5.3 #5b",
            "仅 continuous 关节；有限区间（MDL-12）；分析消费属性不回写 "
            "bounds（V12-01）"),
        def("mdl.joint[i].working-range.qmax", VariableKind::Continuous, "",
            "robot-design/joints[i]/workingRange/qmax",
            "units/optimization.md §5.3 #5b",
            "同 §5.3 #5b（上界分量）"),
        // ---- §5.3 #6 基座姿态（EAA 3 分量 rad／预设枚举；预设与 custom
        //      互斥——MDL-22；basePosition R1 不作变量〔需求未列入〕）----
        def("mdl.base.orientation.eaa.x", VariableKind::Continuous, "rad",
            "robot-design/basePlacement/eaa/x", "units/optimization.md §5.3 #6",
            "custom 姿态必填 3 分量（MDL-22；EAA＝等效轴角，单位 rad）；"
            "与预设互斥",
            "base.orientation"),
        def("mdl.base.orientation.eaa.y", VariableKind::Continuous, "rad",
            "robot-design/basePlacement/eaa/y", "units/optimization.md §5.3 #6",
            "同 §5.3 #6（y 分量）", "base.orientation"),
        def("mdl.base.orientation.eaa.z", VariableKind::Continuous, "rad",
            "robot-design/basePlacement/eaa/z", "units/optimization.md §5.3 #6",
            "同 §5.3 #6（z 分量）", "base.orientation"),
        def("mdl.base.orientation.preset", VariableKind::Enumeration, "",
            "robot-design/basePlacement/preset", "units/optimization.md §5.3 #6",
            "预设轴向唯一产出点＝runtime installationPresetRotation()"
            "（P-RT-4）；预设与 custom 互斥（MDL-22）",
            "base.orientation", basePresetValues()),
        // ---- §5.3 #7 TCP 偏置（平移 m＋旋转 rad 各 3 分量；键必须已存在，
        //      不新增 TCP 键；默认 TCP 引用不破坏）----
        def("mdl.tcp[key].offset.t.x", VariableKind::Continuous, "m",
            "robot-design/tools[key]/tcpList[key]/offset/t/x",
            "units/optimization.md §5.3 #7",
            "键必须已存在（不新增 TCP 键）；默认 TCP 引用不破坏"),
        def("mdl.tcp[key].offset.t.y", VariableKind::Continuous, "m",
            "robot-design/tools[key]/tcpList[key]/offset/t/y",
            "units/optimization.md §5.3 #7",
            "同 §5.3 #7（y 分量）"),
        def("mdl.tcp[key].offset.t.z", VariableKind::Continuous, "m",
            "robot-design/tools[key]/tcpList[key]/offset/t/z",
            "units/optimization.md §5.3 #7",
            "同 §5.3 #7（z 分量）"),
        def("mdl.tcp[key].offset.r.x", VariableKind::Continuous, "rad",
            "robot-design/tools[key]/tcpList[key]/offset/r/x",
            "units/optimization.md §5.3 #7",
            "同 §5.3 #7（旋转 x 分量，T_tool_tcp 旋转部分，单位 rad）"),
        def("mdl.tcp[key].offset.r.y", VariableKind::Continuous, "rad",
            "robot-design/tools[key]/tcpList[key]/offset/r/y",
            "units/optimization.md §5.3 #7",
            "同 §5.3 #7（旋转 y 分量）"),
        def("mdl.tcp[key].offset.r.z", VariableKind::Continuous, "rad",
            "robot-design/tools[key]/tcpList[key]/offset/r/z",
            "units/optimization.md §5.3 #7",
            "同 §5.3 #7（旋转 z 分量）"),
        // ---- §5.3 #8 材料（枚举；值域＝modeling 默认材料键集；覆盖用户
        //      已提供物性时须显式授权〔改写 Provided 值〕）----
        def("mdl.link[i].material", VariableKind::Enumeration, "",
            "robot-design/links[i]/body/material", "units/optimization.md §5.3 #8",
            "值域＝modeling 默认材料键集（密度 kg/m³ 随键）；覆盖用户已提供"
            "物性（ValueProvenance==Provided）时须显式授权",
            {}, materialValues()),
        // ---- §5.3 #9 传动比（连续，无量纲；c 口径 c＝Δq_joint/Δθ_motor
        //      ——P-DT-10 本卡采用、与 drivetrain/runtime 一致〔DOPT-12〕；
        //      StageB 放行——可编辑并编译进候选，完整驱动性能评估归 OPT-D
        //      〔V12-02/DOPT-15，AT-09 回归反例〕；值 >0 且有限 I-MDL-11）----
        def("mdl.drivetrain.ratio[j]", VariableKind::Continuous, "1",
            "robot-drivetrain/ratioPerJoint[j]", "units/optimization.md §5.3 #9",
            ">0 且有限（I-MDL-11）；c 口径 c＝Δq_joint/Δθ_motor（drivetrain.md "
            "§5.3，P-DT-10）；StageB 可编辑并编译进候选（V12-02），完整驱动"
            "性能评估归 OPT-D（DOPT-15）",
            {}, {}, true, true, true),
    };
    return kTable;
}

/**
 * @brief StageD 追加条目（§5.6 R2 离散器件变量；REQUIREMENTS §15.0 OPT-D
 *        行权威命名 motor-key/reducer-key）。
 *
 * 边界登记（卡 §5.6 原文）：离散值为 selection 组合空间合法成员
 * （目录身份＋modelId）；组合合法性由 sel.combination-check 内层判定
 * （N5——本单元不自判）；目录版本与内容身份随绑定携带（SEL-08 同型——
 * 目录更新不静默改变历史候选）。直线传动/耦合链变量在 SEL-09-S1/MDL-21
 * 启用前不可绑定（OPT-COMBO-OUT-OF-SCOPE——绑定校验面，§5.7）。
 */
const std::vector<VariableDefinition>& stageDExtraDefinitions()
{
    static const std::vector<VariableDefinition> kTable = {
        def("mdl.drivetrain.motor-key[j]", VariableKind::DiscreteDevice, "",
            "robot-drivetrain/motorKey[j]", "units/optimization.md §5.6（§15.0 OPT-D 行）",
            "StageD 离散器件变量：值＝selection 目录 modelId；目录版本与内容"
            "身份随绑定携带（SEL-08 同型）；组合合法性归 sel.combination-check"
            "（N5）", {}, {}, false, true),
        def("mdl.drivetrain.reducer-key[j]", VariableKind::DiscreteDevice, "",
            "robot-drivetrain/reducerKey[j]",
            "units/optimization.md §5.6（§15.0 OPT-D 行）",
            "同 motor-key（减速器侧）", {}, {}, false, true),
    };
    return kTable;
}

// =====================================================================
// token 模式匹配（matchDefinition 与 mutex 组实例化共用）
// =====================================================================

/**
 * @brief 检查 pattern 是否匹配 token（逐段：[...] 占位段匹配任意非空段，
 *        其余逐字相等）。
 *
 * 实现为同步双指针扫描：非 '[' 区域逐字符比对；pattern 遇 '[' 时，双方
 * 各读到 ']'——pattern 括号内是占位名（如 i/j/key），token 括号内须非空
 * 且不含 ']'。任一侧提前结束即不匹配。时间 O(len)。
 */
bool patternMatches(std::string_view pattern, std::string_view token)
{
    std::size_t pi = 0, ti = 0;
    while (pi < pattern.size() && ti < token.size()) {
        if (pattern[pi] == '[') {
            // 占位段：pattern 读到 ']'；token 读到 ']'。
            const auto pClose = pattern.find(']', pi);
            const auto tClose = token.find(']', ti);
            if (pClose == std::string_view::npos || tClose == std::string_view::npos) {
                return false;  // 括号不闭合——形态非法，不匹配
            }
            if (tClose == ti) {
                return false;  // token 段为空——占位必须匹配非空段
            }
            pi = pClose + 1;
            ti = tClose + 1;
            continue;
        }
        if (pattern[pi] != token[ti]) {
            return false;  // 字面段不一致
        }
        ++pi;
        ++ti;
    }
    // 同时耗尽才匹配（一侧有余尾＝形态不同）。
    return pi == pattern.size() && ti == token.size();
}

/// 提取 token 中第一个 [...] 占位段的内容（如 "mdl.joint[3].dh.a" → "3"）。
std::string firstPlaceholderValue(const std::string& token)
{
    const auto open = token.find('[');
    const auto close = token.find(']', open == std::string::npos ? 0 : open);
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
        return {};  // 无占位段或空段——互斥组键退化为不含实例段
    }
    return token.substr(open + 1, close - open - 1);
}

/// mutexGroup 实例化：组键中的 [占位名] 替换为 token 的第一个占位段内容。
/// （如 "joint[i].dh-vs-origin" × "mdl.joint[3].dh.a" → "joint[3].dh-vs-origin"
/// ——同关节的 DH 组与安装位置组因此共享同一互斥键。）
std::string instantiateMutexGroup(const std::string& mutexGroup, const std::string& token)
{
    if (mutexGroup.empty()) {
        return {};
    }
    const auto placeholder = mutexGroup.find('[');
    if (placeholder == std::string::npos) {
        return mutexGroup;  // 无占位的组键（如 base.orientation）原样生效
    }
    const auto close = mutexGroup.find(']', placeholder);
    if (close == std::string::npos) {
        return mutexGroup;  // 形态异常——词表登记错误，按原样处理（不吞错：
                            //  词表是本单元自持数据，登记期已可审出）
    }
    const auto instance = firstPlaceholderValue(token);
    if (instance.empty()) {
        return mutexGroup;  // token 无占位——组键退化为模板形态
    }
    return mutexGroup.substr(0, placeholder) + instance + mutexGroup.substr(close + 1);
}

/// 绑定的类别与词表条目一致性（实现口径：词表 Continuous 条目允许绑定声明
/// Quantized——量化＝连续变量的网格化使用形态，类型系统全阶段支持，卡
/// §5.3"量化变量说明"；其余类别必须严格一致）。
bool kindCompatible(VariableKind defKind, VariableKind bindingKind)
{
    if (defKind == VariableKind::Continuous) {
        return bindingKind == VariableKind::Continuous
            || bindingKind == VariableKind::Quantized;
    }
    return defKind == bindingKind;
}

/// 指定阶段的条目启用位（§5.7 阶段锁变量面——未启用条目被引用即
/// OPT-STAGE-LOCKED，不降级、不丢弃）。
bool enabledInStage(const VariableDefinition& d, OptimizationStage stage)
{
    return stage == OptimizationStage::StageB ? d.enabledInStageB : d.enabledInStageD;
}

/// issue 便捷构造（码参取 string_view——DiagCodes 常量为 string_view，
/// MSVC 下到 string 无隐式转换，显式在此单点转换）。
BindingValidationIssue issue(std::string_view code, std::string bindingId, std::string detail)
{
    BindingValidationIssue i;
    i.code = std::string(code);
    i.bindingId = std::move(bindingId);
    i.detail = std::move(detail);
    return i;
}

}  // namespace

// =====================================================================
// 词表与 token
// =====================================================================

std::string_view toToken(VariableKind kind) noexcept
{
    switch (kind) {
    case VariableKind::Continuous:     return "continuous";
    case VariableKind::Quantized:      return "quantized";
    case VariableKind::Enumeration:    return "enumeration";
    case VariableKind::DiscreteDevice: return "discrete-device";
    }
    return {};  // 不可达——新枚举值追加时由编译器 -Wswitch 暴露
}

const std::vector<VariableDefinition>& builtinVariableDefinitions(OptimizationStage stage)
{
    // StageD 词表＝StageB 全量＋离散器件追加（每次调用重建拼接表——词表
    // 只读、拼接开销与研究规模同阶且一次性；静态缓存会引入初始化序负担，
    // 不值得）。条目序＝StageB 行序＋追加序（确定性，NFR-COR-02）。
    static const std::vector<VariableDefinition> kStageD = [] {
        auto all = stageBDefinitions();
        const auto& extra = stageDExtraDefinitions();
        all.insert(all.end(), extra.begin(), extra.end());
        return all;
    }();
    return stage == OptimizationStage::StageB ? stageBDefinitions() : kStageD;
}

const VariableDefinition* matchDefinition(const BindingToken& bindingId,
                                          OptimizationStage stage)
{
    // 全量词表匹配（阶段无关——见公共头注："是否登记"与"该阶段是否启用"
    // 是两个判定；阶段锁由校验层按条目 enabledInStage* 执行）。StageD 词表
    // ＝StageB 全量＋离散追加，即全量集；stage 参数不参与匹配域（保留扩展点）。
    (void)stage;
    const auto& table = builtinVariableDefinitions(OptimizationStage::StageD);
    const auto it = std::find_if(table.begin(), table.end(),
                                 [&](const VariableDefinition& d) {
                                     return patternMatches(d.tokenPattern, bindingId);
                                 });
    return it == table.end() ? nullptr : &*it;
}

// =====================================================================
// 两种初始化（OPT-01——共用同一管线，仅初始化参数与锁定集不同）
// =====================================================================

std::vector<VariableBinding> initializeBindings(OptimizationStage stage,
                                                StudyInitialization mode,
                                                const std::vector<VariableBinding>& bindings)
{
    std::vector<VariableBinding> out;
    out.reserve(bindings.size());
    for (const auto& b : bindings) {
        // 初始化只对词表内变量生效：未知绑定属调用方契约违约（fail-fast，
        // 不静默跳过——静默会产出"看起来初始化成功实则未授权面不全"的研究）。
        const VariableDefinition* d = matchDefinition(b.bindingId, stage);
        if (b.bindingId.empty() || d == nullptr) {
            throw OptimizationError(kOptInputInvalid,
                                    "initializeBindings: 绑定 " + b.bindingId
                                        + " 不在本阶段词表（初始化仅作用于词表内变量）");
        }
        VariableBinding nb = b;  // 拷贝——边界/默认值/诊断定位保持用户值
        // 类别一致性：与补丁校验同口径（Continuous 词表允许 Quantized 使用
        // 形态）；不一致立即 fail-fast（校验面会再报，这里尽早暴露）。
        if (!kindCompatible(d->kind, nb.kind)) {
            throw OptimizationError(
                kOptInputInvalid,
                "initializeBindings: 绑定 " + nb.bindingId + " 的值类别与词表"
                "定义不符（词表 " + std::string(toToken(d->kind)) + "，绑定 "
                + std::string(toToken(nb.kind)) + "）");
        }
        // 元数据回填（词表为准）：单位——绑定未显式给有效单位且词表有单位
        // 时回填；值域空时回填词表封闭值域（防私造）；权威字段定位——绑定
        // 未填时回填词表形态（实例化占位由用户按基线结构细化）。
        if (!nb.unit.isValid() && !d->unitSymbol.empty()) {
            if (const auto u = core::UnitToken::find(d->unitSymbol)) {
                nb.unit = *u;
            }
        }
        if (nb.enumValues.empty()) {
            nb.enumValues = d->enumValues;
        }
        if (nb.authorityFieldPath.empty()) {
            nb.authorityFieldPath = d->authorityFieldPath;
        }
        // 授权/锁定施加（§5.4/§8.2——两种初始化的"锁定集"差异所在）：
        //   NewModel：全部默认可绑定（authorized=true、locked=false）——
        //     边界由用户按机型工程范围填写（不发明默认工程数值，P-03）；
        //   Refit：未授权参数默认锁定（OPT-02）——初始化产出**全部**
        //     authorized=false、locked=true；"授权"是用户在研究定义中
        //     逐变量显式开启的**后续编辑动作**（授权状态进 config.opt
        //     canonical——授权集合变化＝新输入，§5.4），不属于初始化的
        //     推断职责：入参 authorized 位是 VariableBinding 的通用默认
        //     （卡 §5.2 字段默认 true，服务新机型场景），Refit 不信任它、
        //     统一重置，防止"默认值被误读为显式授权"而漏锁。
        if (mode == StudyInitialization::NewModel) {
            nb.authorized = true;
            nb.locked = false;
        } else {  // Refit——改型默认锁定（授权后续显式开启）
            nb.authorized = false;
            nb.locked = true;
        }
        out.push_back(std::move(nb));
    }
    return out;
}

// =====================================================================
// 绑定集校验（I-OPT-7～9＋§5.4/§5.7——研究定义校验面）
// =====================================================================

bool BindingValidationReport::ok() const noexcept
{
    // 警告级（kOptVarUnbindable）不阻塞；其余码均为阻塞（研究定义拒绝）。
    return std::all_of(issues.begin(), issues.end(), [](const BindingValidationIssue& i) {
        return i.code == kOptVarUnbindable;
    });
}

bool BindingValidationReport::hasCode(std::string_view code) const noexcept
{
    return std::any_of(issues.begin(), issues.end(),
                       [&](const BindingValidationIssue& i) { return i.code == code; });
}

namespace {

/**
 * @brief 单条绑定的结构校验（独立成函数——validateBindings 主体保持
 *        "逐条→互斥→闭包核对"三段清晰）。
 */
void validateOneBinding(const VariableBinding& b, const VariableDefinition& d,
                        OptimizationStage stage, BindingValidationReport& report)
{
    const auto addInvalid = [&](std::string detail) {
        report.issues.push_back(issue(kOptInputInvalid, b.bindingId, std::move(detail)));
    };
    // 阶段锁（§5.7 变量面）：词表条目在当前阶段未启用——StageB 引用离散
    // 器件（电机/减速器）或 1b 截面类型（P-OPT-4 裁决前）在此拒绝；不降级、
    // 不丢弃（§2.2 红线 2/5——OPT-VER-108 观测点）。
    if (!enabledInStage(d, stage)) {
        report.issues.push_back(issue(
            kOptStageLocked, b.bindingId,
            "当前阶段不支持该变量（阶段锁——词表条目 " + d.specRef + " 未在 "
                + std::string(toToken(stage)) + " 启用；不降级不丢弃）"));
        return;  // 阶段锁的绑定不再做值域核对（能力面问题优先、避免噪音）
    }
    // 类别一致性（实现口径见 kindCompatible 注）。
    if (!kindCompatible(d.kind, b.kind)) {
        addInvalid(std::string("值类别与词表定义不符（词表 ") + std::string(toToken(d.kind))
                   + "，绑定 " + std::string(toToken(b.kind)) + "）");
    }
    if (b.kind == VariableKind::Continuous || b.kind == VariableKind::Quantized) {
        // 连续/量化：单位必须显式（SI 真值纪律，I-OPT-8——补丁值恒 SI）。
        if (!b.unit.isValid()) {
            addInvalid("连续/量化绑定必须携带有效 SI 单位（core::UnitToken）");
        }
        // 边界有限且严格递增（卡 §5.2"必须 lower<upper"；NaN/Inf 拒绝——
        // 0/0 未配置边界同样在此被拒，用户须按工程范围填写，P-03）。
        if (!std::isfinite(b.lowerBound) || !std::isfinite(b.upperBound)) {
            addInvalid("边界必须有限（SI 单位；拒绝 NaN/±Inf）");
        } else if (!(b.lowerBound < b.upperBound)) {
            addInvalid("边界必须严格 lower<upper（含端点闭区间；未配置的 0/0 "
                       "同样拒绝——按机型工程范围填写，P-03）");
        }
        if (b.kind == VariableKind::Quantized) {
            // 量化步长 >0（OPT-VER-105"步长≤0 拒绝"）；有限另查。
            if (!std::isfinite(b.step) || !(b.step > 0.0)) {
                addInvalid("量化绑定步长必须 >0 且有限（与值同单位）");
            }
        } else if (b.step != 0.0) {
            addInvalid("连续绑定步长恒 0（量化语义请声明 Quantized——卡 §5.2）");
        }
    } else if (b.kind == VariableKind::Enumeration) {
        // 枚举：值域封闭且必须等于词表值域（防私造枚举项——词表是域内
        // 唯一定义点）；默认下标在界内；步长无意义置 0。
        if (b.enumValues.empty()) {
            addInvalid("枚举绑定值域为空（须为封闭值域）");
        } else if (b.enumValues != d.enumValues) {
            addInvalid("枚举值域必须等于词表封闭值域（防私造枚举项）");
        }
        if (b.defaultValueIndex >= b.enumValues.size()) {
            addInvalid("枚举默认下标越界（enumValues 内）");
        }
        if (b.step != 0.0) {
            addInvalid("枚举绑定步长无意义，须置 0（卡 §5.2）");
        }
    } else {  // DiscreteDevice
        // 离散器件：值域/边界无意义（值＝离散引用文本）；目录身份随绑定
        // 携带的形态（SEL-08）在 R2 落位时扩展，本面不校验引用文本内容
        // （组合合法性归 sel.combination-check——N5 非所有权）。
        if (b.step != 0.0) {
            addInvalid("离散器件绑定步长无意义，须置 0（卡 §5.2）");
        }
    }
    // 锁定/授权联动（§5.4）：authorized=false 必须 locked=true（未授权参数
    // 默认锁定——初始化施加；此处拦截手工构造的矛盾状态）。
    if (!b.authorized && !b.locked) {
        addInvalid("未授权（authorized=false）参数必须锁定（locked=true——"
                   "改型默认锁定，OPT-02/§5.4）");
    }
}

}  // namespace

BindingValidationReport OptimizationVariableProvider::validateBindings(
    const std::vector<VariableBinding>& bindings,
    const evidence::AnalysisSnapshot& snapshot) const
{
    BindingValidationReport report;
    std::vector<std::pair<std::string, std::string>> mutexSeen;  // (组键, 首个绑定)
    // 第一遍：逐条结构校验＋互斥组键收集（I-OPT-7——同一互斥键的两个激活
    // 绑定即权威冲突，如 StandardDH/Explicit 权威互斥、基座预设/custom 互斥）。
    for (const auto& b : bindings) {
        if (b.bindingId.empty()) {
            report.issues.push_back(issue(kOptInputInvalid, {},
                                          "绑定 bindingId 为空（研究内唯一稳定 token 必填）"));
            continue;
        }
        const VariableDefinition* d = matchDefinition(b.bindingId, m_stage);
        // 词表匹配以构造时登记的阶段为准（服务实例按阶段装配——运行期一次
        // 校验一种研究的阶段面；跨阶段词表混用属装配错误）。
        if (d == nullptr) {
            report.issues.push_back(issue(kOptInputInvalid, b.bindingId,
                                          "未知绑定（不在本阶段词表——绑定 token 形态"
                                          "须匹配 units/optimization.md §5.3 词表）"));
            continue;
        }
        const std::string mkey = instantiateMutexGroup(d->mutexGroup, b.bindingId);
        if (!mkey.empty()) {
            const auto prev = std::find_if(mutexSeen.begin(), mutexSeen.end(),
                                           [&](const auto& kv) { return kv.first == mkey; });
            if (prev != mutexSeen.end()) {
                report.issues.push_back(issue(
                    kOptInputInvalid, b.bindingId,
                    "权威互斥（I-OPT-7）：与绑定 " + prev->second + " 共享互斥组 "
                        + mkey + "（同一权威字段/互斥权威只能被一个绑定激活）"));
            } else {
                mutexSeen.emplace_back(mkey, b.bindingId);
            }
        }
        validateOneBinding(b, *d, m_stage, report);
    }
    // 第二遍：diagSubject 闭包核对（Preflight #4"引用悬空"的绑定级核对——
    // 非空者必须是快照 objectClosure 中的 ObjectId；不读对象字节，P-OPT-2
    // 裁决前研究定义面先行）。快照闭包按 objectId 线性查找（研究规模小；
    // 快照自身已按 objectId 字典序规范化——builder 冻结纪律）。
    for (const auto& b : bindings) {
        if (b.diagSubject.empty()) {
            continue;  // 未定位对象——核对不适用（诊断定位字段可选）
        }
        const auto oid = core::ObjectId::tryFromCanonical(b.diagSubject);
        if (!oid.has_value()) {
            report.issues.push_back(issue(kOptInputInvalid, b.bindingId,
                                          "diagSubject 非法 ObjectId 文本（须为 "
                                          "\"obj-<32hex>\" 规范形态）: " + b.diagSubject));
            continue;
        }
        const bool inClosure = std::any_of(
            snapshot.objectClosure.begin(), snapshot.objectClosure.end(),
            [&](const evidence::ObjectRefEntry& e) { return e.objectId == *oid; });
        if (!inClosure) {
            report.issues.push_back(issue(
                kOptInputInvalid, b.bindingId,
                "diagSubject 引用悬空（不在快照 objectClosure——基线工件缺失或"
                "对象定位错）: " + b.diagSubject));
        }
    }
    return report;
}

// =====================================================================
// 补丁差异（I-OPT-10——供差异预览与导出复用）
// =====================================================================

std::vector<VariableDiffEntry> diffCandidatePatch(const CandidatePatch& a,
                                                  const CandidatePatch& b)
{
    // 双指针合并两个已按 bindingId 排序的视图（canonical 语义排序——
    // 候补丁项在规范化构造中已排序；此处对入参再排序兜底，防御手工构造）。
    std::vector<PatchItem> la = a.items, lb = b.items;
    auto byBinding = [](const PatchItem& x, const PatchItem& y) {
        return x.bindingId < y.bindingId;
    };
    std::sort(la.begin(), la.end(), byBinding);
    std::sort(lb.begin(), lb.end(), byBinding);

    std::vector<VariableDiffEntry> out;
    std::size_t i = 0, j = 0;
    while (i < la.size() || j < lb.size()) {
        if (j >= lb.size() || (i < la.size() && la[i].bindingId < lb[j].bindingId)) {
            // 仅 a 有——removed（b 侧删除了该绑定取值）。
            VariableDiffEntry e;
            e.bindingId = la[i].bindingId;
            e.kind = 'D';
            e.oldScalar = la[i].scalarValue;
            e.oldEnumIndex = la[i].enumIndex;
            e.oldDiscreteRef = la[i].discreteRef;
            // 定位：词表形态（无绑定上下文——实现口径见 Variable.hpp 文件注；
            // diagSubject 置空，精确对象定位随带绑定上下文的消费面补全）。
            if (const auto* d = matchDefinition(la[i].bindingId, OptimizationStage::StageD)) {
                e.authorityFieldPath = d->authorityFieldPath;
            }
            out.push_back(std::move(e));
            ++i;
        } else if (i >= la.size() || lb[j].bindingId < la[i].bindingId) {
            // 仅 b 有——added。
            VariableDiffEntry e;
            e.bindingId = lb[j].bindingId;
            e.kind = 'A';
            e.newScalar = lb[j].scalarValue;
            e.newEnumIndex = lb[j].enumIndex;
            e.newDiscreteRef = lb[j].discreteRef;
            if (const auto* d = matchDefinition(lb[j].bindingId, OptimizationStage::StageD)) {
                e.authorityFieldPath = d->authorityFieldPath;
            }
            out.push_back(std::move(e));
            ++j;
        } else {
            // 两侧都有——逐字段比较（规范化构造保证非当前形态成员为默认值，
            // 三元组比较等价于值比较）；全等则不出现在差异中。
            const PatchItem& x = la[i];
            const PatchItem& y = lb[j];
            if (x.scalarValue != y.scalarValue || x.enumIndex != y.enumIndex
                || x.discreteRef != y.discreteRef) {
                VariableDiffEntry e;
                e.bindingId = x.bindingId;
                e.kind = 'M';
                e.oldScalar = x.scalarValue;
                e.newScalar = y.scalarValue;
                e.oldEnumIndex = x.enumIndex;
                e.newEnumIndex = y.enumIndex;
                e.oldDiscreteRef = x.discreteRef;
                e.newDiscreteRef = y.discreteRef;
                if (const auto* d = matchDefinition(x.bindingId, OptimizationStage::StageD)) {
                    e.authorityFieldPath = d->authorityFieldPath;
                }
                out.push_back(std::move(e));
            }
            ++i;
            ++j;
        }
    }
    // 输出天然按 bindingId 升序（双指针按序合并）——稳定顺序直接可消费。
    return out;
}

// =====================================================================
// 服务接口实现（薄转发——逻辑在上方自由函数/静态表）
// =====================================================================

OptimizationVariableProvider::OptimizationVariableProvider(OptimizationStage stage)
    : m_stage(stage)
{
    // 装配期定型阶段面（运行期只读——见公共头注）。
}

std::vector<VariableDefinition> OptimizationVariableProvider::definitionsFor(
    OptimizationStage stage) const
{
    return builtinVariableDefinitions(stage);
}

std::vector<VariableDiffEntry> OptimizationVariableProvider::diffCandidatePatch(
    const CandidatePatch& a, const CandidatePatch& b) const
{
    // ★ 必须写**命名空间限定名** optimization::diffCandidatePatch（I-OPT-10
    //   自由函数实现，本文件上方"补丁差异"节）：若写非限定名 diffCandidatePatch，
    //   C++ 非限定名字查找从最内层作用域向外——成员函数自身在类作用域先于
    //   命名空间作用域命中，调用即成员无限自递归（首轮验收 B-1 缺陷：任何
    //   接口调用即栈溢出崩溃，MSVC C4717 编译期告警；27 个用例全走自由函数
    //   漏过该路径）。接口实现＝自由函数的薄转发：经接口（§12.2
    //   IOptimizationVariableProvider）与直接调自由函数两条路径的输出必须
    //   逐字段一致，该一致性由 contract_test 接口路径用例钉扎。
    return optimization::diffCandidatePatch(a, b);
}

}  // namespace sdurws::ird::optimization
