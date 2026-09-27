/**
 * @file   AnalysisConfig.cpp
 * @brief  AnalysisConfig.hpp 契约的实现面——canonical v1 编解码（布局见
 *         头文件类注）、configDigest 摘要（唯一经 core::ContentDigester）、
 *         KIN-CONFIG-ILLEGAL 产码、配置变更依赖提示与显示单位投影。
 *
 * 设计依据：
 *   - units/kinematics.md §4.4（schema 合法域与 canonical 纪律）、§9.5
 *     （config.ik 失效面——四通道全命中；样本基准仅随 plan＋budget/seed）、
 *     §9.6 行 12（KIN-CONFIG-ILLEGAL 产码消费——T10）、§4.5/SA-12
 *     （显示单位纯投影——换算唯一经 core Units 入口，本 TU 零换算算术）
 *   - REQUIREMENTS KIN-12/13、AT-27、I-KIN-4、NFR-COR-03（非法拒绝：
 *     不钳制、不置零、不静默替换——全部违例 fail-fast 异常轨）
 *   - CR-02（traceability/foundation-api-diff.md：摘要算法全仓唯一＝
 *     core ContentDigester——本 TU 不引入第二套摘要实现）
 *
 * 实现要点（与头文件契约的对应关系）：
 *   - 编解码复用 src/CanonicalCodec.hpp 的定宽小端原语（T04 起共享——
 *     禁第二书写点漂移）；解码侧读原语为文件局部补充（读向小端 u32/u64/
 *     f64——CanonicalCodec 只收拢了写向）；
 *   - 校验唯一入口＝validateAnalysisConfiguration（encode/decode/比较
 *     前全部前置调用——三处消费一个校验点，防两套合法域漂移）；
 *   - 异常文案不含数值插值（KIN-CONFIG-ILLEGAL 注册面 paramSchema="[]"、
 *     非比较型——§9.6 行 12 无比较型标记，与登记一致；文案只给字段名/
 *     单位/合法域，确定性字符串）。
 *
 * 线程安全：全部为纯函数/无状态（codec 无成员状态）；确定性：同输入同
 * 字节同摘要同提示（NFR-COR-01/02）。
 */

#include <sdurws/ird/kinematics/AnalysisConfig.hpp>

#include <cmath>
#include <cstring>
#include <stdexcept>

#include <sdurws/ird/kinematics/DiagCodes.hpp>    // kKinConfigIllegal（在册码常量——禁拼码）
#include <sdurws/ird/kinematics/Evidence.hpp>     // kTaskPointsBatchEvaluationKey（提示键表）
#include <sdurws/ird/kinematics/Evaluators.hpp>   // kPoseMetricsEvaluationKey（提示键表）

#include "CanonicalCodec.hpp"  // 定宽小端写原语（私有实现头——仅本单元 src 可含，R-2）

namespace sdurws::ird::kinematics {

namespace {

// CanonicalCodec.hpp 写原语在本 TU 的引入点（detail 命名空间——共享实现
// 的唯一书写点，禁本 TU 复制第二套字节序语义）。
using detail::putBytes;
using detail::putF64;
using detail::putU32;
using detail::putU64;

// =====================================================================
// 局部读原语：定宽小端读取（解码侧——与 CanonicalCodec 写原语互逆）
// =====================================================================

/// 读取偏移处的小端 u32（调用方保证 off+4 ≤ 字节长——解码段已按定长布局
/// 一次校验总长，逐字段读取不再重复边界检查）。
std::uint32_t getU32At(const std::vector<std::uint8_t>& bytes, std::size_t off)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(bytes[off + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return v;
}

/// 读取偏移处的小端 u64（边界同上注）。
std::uint64_t getU64At(const std::vector<std::uint8_t>& bytes, std::size_t off)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(bytes[off + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return v;
}

/// 读取偏移处的 f64（小端位模式 → double——memcpy 免别名 UB，与 putF64
/// 互逆；位级精确还原，含 -0.0/非规格化位型）。
double getF64At(const std::vector<std::uint8_t>& bytes, std::size_t off)
{
    const std::uint64_t bits = getU64At(bytes, off);
    double v = 0.0;
    static_assert(sizeof(bits) == sizeof(v), "f64 位模式直读要求 8 字节 double");
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

/// 对字节串计算 SHA-256 并包成内容身份（CR-02：摘要唯一经
/// core::ContentDigester——全仓唯一实现点，本 TU 不引入第二套算法）。
core::ContentIdentity digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

/// v1 布局内三处 f64 容差的解码侧有限性复核（接收端防线——手工构造/
/// 传输损坏的非有限位型在解码入口即拒绝，NFR-COR-03"非有限显式拒绝"
/// 与 core Units 编码入口同款双端纪律）。
void requireFinite(double v, const char* field, const char* unit)
{
    if (!std::isfinite(v)) {
        throw std::invalid_argument(std::string("AnalysisConfiguration 解码拒绝：字段 ")
                                    + field + " 位型为非有限值（NaN/±Inf——" + unit
                                    + "；canonical 身份面不允许非有限位型）");
    }
}

}  // namespace

// =====================================================================
// 校验与产码
// =====================================================================

void validateAnalysisConfiguration(const AnalysisConfiguration& config)
{
    // 校验序固定（头文件契约列 1~9——同一坏配置必报同一首错，NFR-COR-02）。
    // 每条分支的进入条件即一种业务违约场景；文案给字段名/单位/合法域，
    // 不做数值插值（KIN-CONFIG-ILLEGAL 非比较型——§9.6 行 12 登记）。
    if (config.initialStrategy != InitialValueStrategy::ReferenceQ
        && config.initialStrategy != InitialValueStrategy::SeededRandom
        && config.initialStrategy != InitialValueStrategy::JointGrid) {
        // 解码面防御：枚举位型超出三值词表（手写字节/越权 cast）——值域
        // 之外的"策略"没有语义，拒绝而非取默认。
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：initialStrategy 超出三值词表"
            "（ReferenceQ/SeededRandom/JointGrid）");
    }
    // 计数下界（≥1）：无符号计数域中"负数"不可表达，0 即"计数负"的
    // schema 落值口径（§4.4"多初值数量（≥1）"/"单初值迭代上限"）。
    if (config.initialValuesCount < 1U) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：initialValuesCount 必须 ≥1（多初值"
            "数量计数；0/负形态拒绝，不钳制——NFR-COR-03）");
    }
    if (config.iterationLimit < 1U) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：iterationLimit 必须 ≥1（单初值迭代"
            "上限；0 次迭代＝不求解，不是合法配置）");
    }
    if (!std::isfinite(config.positionResidualTolerance)
        || config.positionResidualTolerance <= 0.0) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：positionResidualTolerance 必须为有限"
            "正值（单位 m；附录 D 第 1 项默认 1e-6——容差 ≤0/非有限拒绝）");
    }
    if (!std::isfinite(config.orientationResidualTolerance)
        || config.orientationResidualTolerance <= 0.0) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：orientationResidualTolerance 必须为"
            "有限正值（单位 rad（不是度）；附录 D 第 2 项默认 1e-6）");
    }
    if (!std::isfinite(config.ikDedupThresholdPerAxis)
        || config.ikDedupThresholdPerAxis <= 0.0) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：ikDedupThresholdPerAxis 必须为有限"
            "正值（单位 rad|m 逐轴；附录 D 第 3 项默认 1e-6）");
    }
    // 种子两处（regionBudget.seed＝区域采样随机序列源；seed＝初值策略
    // SeededRandom 序列源）：0 非法——I-KIN-4。拒绝而非静默替换（不做
    // 0→1）：种子是身份面输入，静默替造会让"用户没选种子"与"用户选了
    // 种子 1"两种意图坍缩成同一结果身份，破坏可复现性的语义（NFR-COR-03）。
    if (config.regionBudget.seed == 0U) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：regionBudget.seed 不得为 0（I-KIN-4"
            "——确定性种子必须显式选择，拒绝不做 0→1 静默替换）");
    }
    if (config.regionBudget.threadCount < 1U) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：regionBudget.threadCount 必须 ≥1"
            "（并行分片线程数；执行参数但入 configDigest——KIN-13/V-21）");
    }
    if (config.seed == 0U) {
        throw std::invalid_argument(
            "AnalysisConfiguration 非法：seed=0 非法（I-KIN-4——§3.4 随机性"
            "唯一来源必须显式选择，拒绝不做 0→1 静默替换）");
    }
}

core::DiagnosticRecord configurationIllegalDiagnostic(const std::string& cause)
{
    // KIN-CONFIG-ILLEGAL 产码面（§9.6 行 12——码值经 DiagCodes.hpp 在册
    // 常量，禁字符串拼码）。subject 缺席：求解配置是用户级设置（PM-14，
    // 非 .rwdesign 项目对象），没有 ObjectId 可指——不伪造身份（C-3 校验
    // 在 make 内执行，必填串为空即抛＝调用方错误显性化）。
    return core::DiagnosticRecord::make(
        std::string(kKinConfigIllegal),
        std::nullopt,  // 无项目对象身份可指——用户级配置非 .rwdesign 对象
        std::nullopt,  // localName 不伪造（R-4 名称归⑥端口）
        std::nullopt,  // runtimeName——⑥端口消费随名称面任务
        "求解配置编辑/装配（KIN-13——AnalysisConfiguration，用户级持久化）",
        cause,
        "修正求解配置后重试：种子须非零且显式选择、三容差须为有限正值"
        "（m/rad/rad|m）、初值数与迭代上限须 ≥1、线程数须 ≥1");
}

// =====================================================================
// canonical v1 编解码（布局表见头文件 AnalysisConfigurationCodec 类注）
// =====================================================================

std::vector<std::uint8_t> AnalysisConfigurationCodec::encode(const AnalysisConfiguration& config) const
{
    // 先校验后编码（fail-fast 先于任何字节产出——非法配置没有 canonical
    // 形态，半截编码无意义）。
    validateAnalysisConfiguration(config);

    std::vector<std::uint8_t> out;
    out.reserve(kAnalysisConfigCanonicalSize);

    // magic（8 字节 ASCII；sizeof 含结尾 NUL＝9——长度必须显式写 8，
    // F-395 教训：magic 长度口径以实写字节为准）。
    putBytes(out, kAnalysisConfigMagic, 8U);
    // 字段定序＝布局表序（偏移 8~64；逐字段写入顺序即编码契约）。
    putU32(out, kAnalysisConfigurationSchemaVersion);                    // 偏移 8：u32 schemaVersion
    out.push_back(static_cast<std::uint8_t>(config.initialStrategy));    // 偏移 12：u8 枚举底层值
    putU32(out, config.initialValuesCount);                              // 偏移 13：u32
    putU32(out, config.iterationLimit);                                  // 偏移 17：u32
    putF64(out, config.positionResidualTolerance);                       // 偏移 21：f64（m）
    putF64(out, config.orientationResidualTolerance);                    // 偏移 29：f64（rad）
    putF64(out, config.ikDedupThresholdPerAxis);                         // 偏移 37：f64（rad|m）
    putU64(out, config.regionBudget.seed);                               // 偏移 45：u64
    putU32(out, config.regionBudget.threadCount);                        // 偏移 53：u32
    putU64(out, config.seed);                                            // 偏移 57：u64

    // 定长自检（实现缺陷显性化——布局漂移即刻暴露而非产出错误身份字节）。
    if (out.size() != kAnalysisConfigCanonicalSize) {
        throw std::logic_error("AnalysisConfiguration canonical v1 编码长度"
                               "偏离布局表（实现缺陷——禁产出错误身份字节）");
    }
    return out;
}

AnalysisConfiguration AnalysisConfigurationCodec::decode(const std::vector<std::uint8_t>& bytes) const
{
    // ---- 结构校验段（长度/magic/版本——接收端对"是不是 v1 canonical
    // ---- 编码"的三连问；就地定位随文案）。任一不符＝调用方交来的字节
    // ---- 不是本 schema 的编码（存储损坏/异版本/手工拼装），fail-fast。
    if (bytes.size() != kAnalysisConfigCanonicalSize) {
        throw std::invalid_argument(
            "AnalysisConfiguration 解码拒绝：字节长度非 canonical v1 定长"
            "（期望 65 字节封闭布局——长度不符即非本版本编码）");
    }
    if (std::memcmp(bytes.data(), kAnalysisConfigMagic, 8U) != 0) {
        throw std::invalid_argument(
            "AnalysisConfiguration 解码拒绝：magic 不符（期望 \"IRDCFG01\""
            "——非本域编码或字节损坏）");
    }
    {
        const std::uint32_t version = getU32At(bytes, 8);
        if (version != kAnalysisConfigurationSchemaVersion) {
            throw std::invalid_argument(
                "AnalysisConfiguration 解码拒绝：schemaVersion 非 1（v1 编码"
                "不被其他版本解码接受——向前不兼容，身份面不允许歧义解码）");
        }
    }

    // ---- 逐字段还原段（布局偏移与 encode 一一对应；f64 位级精确还原）。
    AnalysisConfiguration config;
    config.initialStrategy = static_cast<InitialValueStrategy>(bytes[12]);
    config.initialValuesCount = getU32At(bytes, 13);
    config.iterationLimit = getU32At(bytes, 17);
    config.positionResidualTolerance = getF64At(bytes, 21);
    config.orientationResidualTolerance = getF64At(bytes, 29);
    config.ikDedupThresholdPerAxis = getF64At(bytes, 37);
    config.regionBudget.seed = getU64At(bytes, 45);
    config.regionBudget.threadCount = getU32At(bytes, 53);
    config.seed = getU64At(bytes, 57);

    // ---- 解码侧有限性复核（浮点位模式还原后先查非有限再入合法域校验
    // ---- ——NaN/±Inf 位型在 canonical 身份面无语义，双端拒绝）。
    requireFinite(config.positionResidualTolerance, "positionResidualTolerance", "m");
    requireFinite(config.orientationResidualTolerance, "orientationResidualTolerance", "rad");
    requireFinite(config.ikDedupThresholdPerAxis, "ikDedupThresholdPerAxis", "rad|m");

    // ---- 合法域复验（唯一校验点——枚举值域/计数下界/容差正值/种子非零
    // ---- 全部落 validateAnalysisConfiguration：编解码同域，防两套合法域漂移）。
    validateAnalysisConfiguration(config);
    return config;
}

// =====================================================================
// configDigest 摘要出口与快照接线
// =====================================================================

core::ContentIdentity analysisConfigurationDigest(const AnalysisConfiguration& config)
{
    // 摘要对象＝canonical 字节（§4.4"configDigest＝SHA-256"——对编码字节
    // 摘要，非对结构体内存映像：字节面才是跨进程可比的身份载体）。
    const AnalysisConfigurationCodec codec;
    return digestBytes(codec.encode(config));
}

evidence::ConfigEntry makeConfigurationRefEntry(const AnalysisConfiguration& config)
{
    // 编码一次、摘要同一份字节（encode 与 digest 单源——防两处编码漂移
    // 出"字节与身份不配对"的条目；evidence SnapshotBuilder 冻结期一致性
    // 校验因此天然通过）。
    const AnalysisConfigurationCodec codec;
    evidence::ConfigEntry entry;
    entry.configKindToken = kConfigIkKindToken;  // 恒 "config.ik"（§4.4 原文）
    entry.canonicalBytes = codec.encode(config);
    entry.contentIdentity = digestBytes(entry.canonicalBytes);
    return entry;
}

// =====================================================================
// 配置变更依赖提示（L-K7/AT-27——纯提示数据，无任何重算触发通道）
// =====================================================================

std::array<std::string_view, 4> analysisConfigConsumerKeys()
{
    // 顺序＝§4.3 依赖声明表行序（pose-metrics→task-point-ik→task-points-
    // batch→region-coverage；确定性序——提示呈现与测试断言的稳定基准）。
    return {std::string_view{kPoseMetricsEvaluationKey},
            std::string_view{kTaskPointIkEvaluationKey},
            std::string_view{kTaskPointsBatchEvaluationKey},
            std::string_view{kRegionCoverageEvaluationKey}};
}

AnalysisConfigChangeHint analyzeConfigurationChange(const AnalysisConfiguration& before,
                                                    const AnalysisConfiguration& after)
{
    // 非法值无身份可言——先校验后比较（任一侧非法即 fail-fast，比较结果
    // 无意义）。
    validateAnalysisConfiguration(before);
    validateAnalysisConfiguration(after);

    AnalysisConfigChangeHint hint;
    // 身份面比较（configDigest 字节等值——提示回答"身份变没变"；同配置
    // 恒同摘要，故摘要相等 ⇔ canonical 字节相等 ⇔ 逐字段相等）。
    const core::ContentIdentity beforeDigest = analysisConfigurationDigest(before);
    const core::ContentIdentity afterDigest = analysisConfigurationDigest(after);
    if (beforeDigest == afterDigest) {
        return hint;  // 身份未变——全空提示（configChanged=false、受影响键空）
    }

    // config.ik 列四行全命中（§9.5：四个评估通道的依赖声明都含 config.ik
    // ——配置一变四通道切片全失效，受影响结果需重算；只提示不触发）。
    hint.configChanged = true;
    hint.affectedEvaluationKeys = analysisConfigConsumerKeys();

    // 样本基准分层（D-04/§9.5 注※）：seed 是 sampleSetIdentity 的构成
    // 输入——种子变化＝新一轮研究基准（样本集随变，比较基准检查拦截）；
    // 其余字段（含线程数）只动 sliceId、不动样本基准。
    hint.sampleBaselineChanged = before.regionBudget.seed != after.regionBudget.seed;
    return hint;
}

// =====================================================================
// DisplayUnitProjection——显示单位纯投影（KIN-12；换算唯一经 core Units）
// =====================================================================

std::optional<DisplayUnitProjection>
DisplayUnitProjection::tryFind(std::string_view lengthSymbol, std::string_view angleSymbol)
{
    // 第一重：R1 冻结子集白名单（KIN-12 原文词表 m/cm/mm、deg/rad；
    // inch/grad/turn 为 R2 扩展 token——KIN-12-S1 归 WP-15-T17，此处拒绝
    // 即"不提前实现"的落值）。区分大小写（core 注册表词表纪律）。
    const bool lengthInR1 = (lengthSymbol == "m" || lengthSymbol == "cm"
                             || lengthSymbol == "mm");
    const bool angleInR1 = (angleSymbol == "rad" || angleSymbol == "deg");
    if (!lengthInR1 || !angleInR1) {
        return std::nullopt;  // R2 token/未知词——显示单位解析失败，调用方回退默认投影
    }
    // 第二重：core 注册表在册复核（正常路径恒命中——白名单即注册表子集；
    // 两表若漂移，此处兜底拒绝而非带无效句柄出逃）。
    const auto length = core::UnitToken::find(lengthSymbol);
    const auto angle = core::UnitToken::find(angleSymbol);
    if (!length.has_value() || !angle.has_value()) {
        return std::nullopt;
    }
    return DisplayUnitProjection(*length, *angle);
}

double DisplayUnitProjection::projectLength(double siMeters) const
{
    // 换算唯一经 core::convert（SA-12：显示层换算由调用方对唯一入口逐
    // 元素施加，不另设第二实现点——本函数零换算算术，纯委托）。
    // from 侧恒为 SI 单位 "m"（注册表编译期冻结行 1——缺席属实现缺陷，
    // 防御分支拒绝而非解引用空 optional）。
    const auto siMeter = core::UnitToken::find("m");
    if (!siMeter.has_value()) {
        throw std::logic_error("core Units 注册表缺少 \"m\"（实现缺陷——"
                               "编译期冻结词表不得缺席 SI 基准单位）");
    }
    // 非有限输入由 core::convert 拒绝（core/units/convert: ——NFR-COR-03）。
    return core::convert(siMeters, *siMeter, m_length);
}

double DisplayUnitProjection::projectAngle(double siRadians) const
{
    // 同 projectLength：唯一入口委托；from 侧恒为 SI 单位 "rad"（注册表
    // 冻结行——角度基准单位）。
    const auto siRadian = core::UnitToken::find("rad");
    if (!siRadian.has_value()) {
        throw std::logic_error("core Units 注册表缺少 \"rad\"（实现缺陷——"
                               "编译期冻结词表不得缺席 SI 基准单位）");
    }
    return core::convert(siRadians, *siRadian, m_angle);
}

}  // namespace sdurws::ird::kinematics
