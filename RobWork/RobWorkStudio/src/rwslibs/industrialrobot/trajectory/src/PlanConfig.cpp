/**
 * @file   PlanConfig.cpp
 * @brief  轨迹求解配置的实现翻译单元——合法域校验、canonical v1 编解码
 *         （magic IRDCFGTR1、小端、f64 位模式、集合字典序）、planConfig-
 *         Digest 摘要与快照配置引用条目组装（WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §5.5（身份路径与九字段约束表）、§6.2（Trajectory
 *     身份块 planConfigDigest 消费）、§14.2.1（descriptor 依赖行
 *     config.trj(Configuration,Required)——kindToken 落值出处）
 *   - 先例：kinematics/src/AnalysisConfig.cpp（WP-15-T10 同款五件套实现
 *     形态——校验序固定/编解码对称/digest 同源；字节序 helper 的位级
 *     写法与该实现一致，两域 canonical 独立成套但字节序纪律同款）
 *   - 需求 NFR-COR-01/02/03（确定性；同配置同字节；非法拒绝不钳制）
 *
 * 确定性：编码纯位组装＋map 字典序（无隐藏输入序）；摘要经
 * core::ContentDigester（SHA-256，FIPS 180-2——CR-02 唯一实现点）。
 */

#include <sdurws/ird/trajectory/PlanConfig.hpp>

#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>

#include <cstring>
#include <stdexcept>
#include <limits>

namespace sdurws::ird::trajectory {

namespace {

// =====================================================================
// 小端字节序 helper（编解码唯一书写点——位模式直写，f64 经 memcpy 转录
// IEEE754 表示；全仓 canonical 编码纪律同款实现形态）
// =====================================================================

/// 追加 u32 小端 4 字节。
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

/// 追加 u64 小端 8 字节。
void putU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

/// 追加 f64 小端 8 字节（IEEE754 位模式直写——不经过文本格式化，保证
/// 同值同字节；-0.0 与 0.0 位模式不同即身份不同——可复现性纪律）。
void putF64(std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "f64 位模式转录要求 8 字节 double");
    std::memcpy(&bits, &v, sizeof(bits));
    putU64(out, bits);
}

/// 追加 UTF-8 串（u32 字节长前缀＋字节原样；不做任何转码/规范化——
/// canonical 字节＝内存字节）。
void putString(std::vector<std::uint8_t>& out, const std::string& s)
{
    putU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

/// 从字节流读取 u32 小端（位移组装，不要求对齐——canonical 布局无填充）。
std::uint32_t getU32(const std::vector<std::uint8_t>& b, std::size_t& off)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(b.at(off++)) << (8 * i);
    }
    return v;
}

/// 从字节流读取 u64 小端。
std::uint64_t getU64(const std::vector<std::uint8_t>& b, std::size_t& off)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(b.at(off++)) << (8 * i);
    }
    return v;
}

/// 从字节流读取 f64（位模式转录——与 putF64 严格互逆）。
double getF64(const std::vector<std::uint8_t>& b, std::size_t& off)
{
    const std::uint64_t bits = getU64(b, off);
    double v = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

/// 从字节流读取串（u32 长度前缀＋字节）。
std::string getString(const std::vector<std::uint8_t>& b, std::size_t& off)
{
    const std::uint32_t n = getU32(b, off);
    std::string s(n, '\0');
    for (std::uint32_t i = 0; i < n; ++i) {
        s[i] = static_cast<char>(b.at(off++));
    }
    return s;
}

/// 有限性判定（std::isfinite 的位面等价——拒绝 NaN 与 ±inf；NFR-COR-03
/// "非有限不静默通过"）。
bool isFiniteDouble(double v)
{
    return v == v && v - v == 0.0;  // NaN≠NaN；inf−inf=NaN——两测覆盖
}

}  // namespace

// =====================================================================
// 合法域校验（校验序＝头文件注的 12 步固定序——首错即抛）
// =====================================================================

void validateTrajectoryPlanConfiguration(const TrajectoryPlanConfiguration& config)
{
    // 1/2：起始状态形态与附件一致性（§5.3 三选一——TaskPoint 必须给对象、
    // 其余形态不得携带悬空附件；"非 TaskPoint 却带 startTaskPoint"会让
    // 同语义配置产生不同 canonical 字节，破坏"同语义同身份"）。
    if (config.startStateKind == StartStateKind::TaskPoint
        && !config.startTaskPoint.isValid()) {
        throw TrajectoryError(
            "trajectory/plan-config/start-state",
            "startStateKind==TaskPoint 时 startTaskPoint 必须为合法对象身份"
            "（当前为全零保留值）");
    }
    if (config.startStateKind != StartStateKind::TaskPoint
        && config.startTaskPoint.isValid()) {
        throw TrajectoryError(
            "trajectory/plan-config/start-state",
            "startStateKind 非 TaskPoint 时 startTaskPoint 必须为保留值全零"
            "（不得携带悬空附件——身份面同语义同字节）");
    }

    // 3：规划器家族 token 非空（§5.5 PlannerSelection——空 token 无身份
    // 意义；具体词表值校验归 WP-16-T06 消费面）。
    if (config.plannerFamilyToken.empty()) {
        throw TrajectoryError("trajectory/plan-config/planner-family",
                              "plannerFamilyToken 不得为空串");
    }

    // 4：参数表串安全（键非空＋键值无 NUL——canonical 串按字节长度界读，
    // NUL 会使下游 C 串消费面截断歧义）。
    for (const auto& [key, value] : config.plannerParams) {
        if (key.empty()) {
            throw TrajectoryError("trajectory/plan-config/planner-param",
                                  "plannerParams 存在空键（键值规范表键须非空）");
        }
        if (key.find('\0') != std::string::npos
            || value.find('\0') != std::string::npos) {
            throw TrajectoryError("trajectory/plan-config/planner-param",
                                  "plannerParams 键/值不得含 NUL 字节（键 " + key + "）");
        }
    }

    // 5：种子非零（I-KIN-4 同款：0 非法拒绝，不做 0→1 静默替换——默认
    // 构造值 0＝故意非法，强制调用方显式选择种子）。
    if (config.planningSeed == 0U) {
        throw TrajectoryError("trajectory/plan-config/seed",
                              "planningSeed=0 非法（确定性种子必须显式给出，"
                              "不做 0→1 静默替换——I-KIN-4 同款）");
    }

    // 6~9：四个正浮点域（有限且 >0；单位见各自字段注——m/rad 逐项）。
    if (!isFiniteDouble(config.cartesianSampleStep) || config.cartesianSampleStep <= 0.0) {
        throw TrajectoryError("trajectory/plan-config/sample-step",
                              "cartesianSampleStep 须有限且 >0（m），实际 "
                                  + std::to_string(config.cartesianSampleStep));
    }
    if (!isFiniteDouble(config.ikContinuityThreshold) || config.ikContinuityThreshold <= 0.0) {
        throw TrajectoryError("trajectory/plan-config/ik-threshold",
                              "ikContinuityThreshold 须有限且 >0（rad|m 逐轴上界），"
                              "实际 " + std::to_string(config.ikContinuityThreshold));
    }
    if (!isFiniteDouble(config.smoothToleranceJoint) || config.smoothToleranceJoint <= 0.0) {
        throw TrajectoryError("trajectory/plan-config/smooth-tolerance-joint",
                              "smoothToleranceJoint 须有限且 >0（rad），实际 "
                                  + std::to_string(config.smoothToleranceJoint));
    }
    if (!isFiniteDouble(config.smoothToleranceTcp) || config.smoothToleranceTcp <= 0.0) {
        throw TrajectoryError("trajectory/plan-config/smooth-tolerance-tcp",
                              "smoothToleranceTcp 须有限且 >0（m），实际 "
                                  + std::to_string(config.smoothToleranceTcp));
    }

    // 10：运动律封闭词表（§5.5"封闭词表，当前唯一值＝唯一实现"；新增值
    // 走卡面增量修订——代码面拒绝未知 token 即词表守卫）。
    if (config.timeParamMethod != kTimeParamMethodQuinticSplineC2) {
        throw TrajectoryError("trajectory/plan-config/time-param-method",
                              "timeParamMethod 不在封闭词表（v1 仅 "
                                  + std::string(kTimeParamMethodQuinticSplineC2)
                                  + "），实际 " + config.timeParamMethod);
    }

    // 11：限值缩放系数 (0,1]（§5.5"保守缩放；1.0＝全限值"——0 无意义、
    // >1 放大限值违反保守缩放语义）。
    if (!isFiniteDouble(config.limitsScaleFactor) || config.limitsScaleFactor <= 0.0
        || config.limitsScaleFactor > 1.0) {
        throw TrajectoryError("trajectory/plan-config/limits-scale",
                              "limitsScaleFactor 须 ∈ (0,1]，实际 "
                                  + std::to_string(config.limitsScaleFactor));
    }

    // 12：驻留策略枚举值域（解码面防御——位型超大值拒绝）。
    switch (config.dwellPolicy) {
    case DwellPolicy::HonorEvents:
    case DwellPolicy::IgnoreDwell:
        break;
    default:
        throw TrajectoryError("trajectory/plan-config/dwell-policy",
                              "dwellPolicy 枚举值越界（底层值 "
                                  + std::to_string(static_cast<unsigned>(
                                      config.dwellPolicy))
                                  + "）");
    }

    // startStateKind 枚举值域（解码面防御；switch 全枚举无 default 以保
    // 编译器漏项告警，此处单独做位面守卫）。
    const auto startBits = static_cast<std::uint8_t>(config.startStateKind);
    if (startBits > static_cast<std::uint8_t>(StartStateKind::TaskPoint)) {
        throw TrajectoryError("trajectory/plan-config/start-state-kind",
                              "startStateKind 枚举值越界（底层值 "
                                  + std::to_string(startBits) + "）");
    }
}

// =====================================================================
// canonical v1 编解码（布局见 PlanConfig.hpp 类注——逐段对照）
// =====================================================================

std::vector<std::uint8_t> TrajectoryPlanConfigCodec::encode(
    const TrajectoryPlanConfiguration& config) const
{
    // 先校验后编码（fail-fast 先于任何字节产出——非法配置无字节面）。
    validateTrajectoryPlanConfiguration(config);

    std::vector<std::uint8_t> out;
    // 预留上界：纯标量段 82 字节＋词表/家族 token＋参数表——预留减少
    // 再分配；实际长度由内容决定（变长布局）。
    out.reserve(128U);

    // [0,9) magic：恒 9 字节 ASCII（F-395 教训——长度口径以实写字节为准，
    // 经 kTrajectoryPlanConfigMagicSize 常量单点定义）。
    out.insert(out.end(), std::begin(kTrajectoryPlanConfigMagic),
               std::begin(kTrajectoryPlanConfigMagic) + kTrajectoryPlanConfigMagicSize);
    // [9,13) schema 版本。
    putU32(out, kTrajectoryPlanConfigSchemaVersion);
    // [12,13) 起始状态枚举。
    out.push_back(static_cast<std::uint8_t>(config.startStateKind));
    // [13,29) 任务点对象 16 字节原样（非 TaskPoint 恒全零——校验已保证）。
    out.insert(out.end(), config.startTaskPoint.bytes.begin(),
               config.startTaskPoint.bytes.end());
    // [29,37) 种子。
    putU64(out, config.planningSeed);
    // [37,45) 采样步长（m）。
    putF64(out, config.cartesianSampleStep);
    // [45,53) IK 延续阈值（rad|m）。
    putF64(out, config.ikContinuityThreshold);
    // [53,61) 平滑容差——关节域（rad）。
    putF64(out, config.smoothToleranceJoint);
    // [61,69) 平滑容差——TCP 域（m）。
    putF64(out, config.smoothToleranceTcp);
    // [69,70) 驻留策略。
    out.push_back(static_cast<std::uint8_t>(config.dwellPolicy));
    // [70,78) 限值缩放系数。
    putF64(out, config.limitsScaleFactor);
    // 变长段 1：运动律词表 token（UTF-8）。
    putString(out, config.timeParamMethod);
    // 变长段 2：规划器家族 token（UTF-8）。
    putString(out, config.plannerFamilyToken);
    // 变长段 3：参数表——std::map 迭代序即键字典序（"键值规范表"的规范
    // 序承载；同集合任意插入序必得同字节——NFR-COR-01 集合字典序纪律）。
    putU32(out, static_cast<std::uint32_t>(config.plannerParams.size()));
    for (const auto& [key, value] : config.plannerParams) {
        putString(out, key);
        putString(out, value);
    }
    return out;
}

TrajectoryPlanConfiguration TrajectoryPlanConfigCodec::decode(
    const std::vector<std::uint8_t>& bytes) const
{
    // 截断转译壳：读取经 vector::at（越界即 std::out_of_range）——长度
    // 下界检查只能拦"整体过短"，"变长段长度声明与实际字节不符"的截断
    // 在读取中途越界。转译为域错误（结构违约语义统一——调用方只捕
    // TrajectoryError 一个类型；std::out_of_range 属实现细节不外溢）。
    try {
        return decodeImpl(bytes);
    } catch (const std::out_of_range&) {
        throw TrajectoryError("trajectory/plan-config/decode-truncated",
                              "canonical 字节在变长段读取中越界（长度声明与"
                              "实际字节不符——结构截断）");
    }
}

TrajectoryPlanConfiguration TrajectoryPlanConfigCodec::decodeImpl(
    const std::vector<std::uint8_t>& bytes) const
{
    // 结构校验段（先于合法域校验——格式错与值错分别定位）。
    // 长度下界：magic 9＋版本 4＋枚举 1＋对象 16＋u64 8＋4×f64 32＋u8 1
    // ＋f64 8＝79 字节纯标量段；三个变长段再各自至少 4 字节长度前缀。
    constexpr std::size_t kMinLen = 79U + 3U * 4U;
    if (bytes.size() < kMinLen) {
        throw TrajectoryError("trajectory/plan-config/decode-length",
                              "canonical 字节过短（" + std::to_string(bytes.size())
                                  + " < 下界 " + std::to_string(kMinLen) + "）");
    }
    if (std::memcmp(bytes.data(), kTrajectoryPlanConfigMagic,
                    kTrajectoryPlanConfigMagicSize) != 0) {
        throw TrajectoryError("trajectory/plan-config/decode-magic",
                              "magic 不符（期望 IRDCFGTR1）");
    }

    TrajectoryPlanConfiguration config;
    std::size_t off = kTrajectoryPlanConfigMagicSize;  // 跳过 magic（9 字节）

    // 版本：v1 编码不被更高/更低版本解码接受（向前不兼容——身份面不允许
    // 歧义解码）。
    const std::uint32_t version = getU32(bytes, off);
    if (version != kTrajectoryPlanConfigSchemaVersion) {
        throw TrajectoryError("trajectory/plan-config/decode-version",
                              "schema 版本不符（期望 1，实际 "
                                  + std::to_string(version) + "）");
    }

    // 标量段按布局序读回（顺序＝encode 写序，严格互逆）。
    const auto startBits = bytes.at(off++);
    if (startBits > static_cast<std::uint8_t>(StartStateKind::TaskPoint)) {
        throw TrajectoryError("trajectory/plan-config/decode-enum",
                              "startStateKind 枚举值越界（"
                                  + std::to_string(startBits) + "）");
    }
    config.startStateKind = static_cast<StartStateKind>(startBits);

    for (auto& byte : config.startTaskPoint.bytes) {
        byte = bytes.at(off++);
    }
    config.planningSeed = getU64(bytes, off);
    config.cartesianSampleStep = getF64(bytes, off);
    config.ikContinuityThreshold = getF64(bytes, off);
    config.smoothToleranceJoint = getF64(bytes, off);
    config.smoothToleranceTcp = getF64(bytes, off);
    const auto dwellBits = bytes.at(off++);
    if (dwellBits > static_cast<std::uint8_t>(DwellPolicy::IgnoreDwell)) {
        throw TrajectoryError("trajectory/plan-config/decode-enum",
                              "dwellPolicy 枚举值越界（"
                                  + std::to_string(dwellBits) + "）");
    }
    config.dwellPolicy = static_cast<DwellPolicy>(dwellBits);
    config.limitsScaleFactor = getF64(bytes, off);
    config.timeParamMethod = getString(bytes, off);
    config.plannerFamilyToken = getString(bytes, off);

    // 参数表：键字典序读回（解码端不重排——encode 已按序写；解码后
    // 插入 std::map 自动恢复字典序视图，二者一致）。
    const std::uint32_t paramCount = getU32(bytes, off);
    for (std::uint32_t i = 0; i < paramCount; ++i) {
        const std::string key = getString(bytes, off);
        const std::string value = getString(bytes, off);
        config.plannerParams[key] = value;
    }

    // 长度封闭：尾部剩余字节≠0 即结构违约（多一字节＝布局漂移或截断
    // 粘连——拒绝而非忽略，身份面字节必须可逆）。
    if (off != bytes.size()) {
        throw TrajectoryError("trajectory/plan-config/decode-tail",
                              "canonical 字节尾部存在剩余 "
                                  + std::to_string(bytes.size() - off)
                                  + " 字节（布局封闭性违约）");
    }

    // 合法域校验段（复用唯一校验点——解码产物与构造产物同规则）。
    validateTrajectoryPlanConfiguration(config);
    return config;
}

// =====================================================================
// 摘要出口与快照接线
// =====================================================================

core::ContentIdentity trajectoryPlanConfigurationDigest(const TrajectoryPlanConfiguration& config)
{
    // 摘要输入＝canonical 字节（先校验后编码后摘要——同 encode 拒绝序）。
    const std::vector<std::uint8_t> canonical = TrajectoryPlanConfigCodec{}.encode(config);
    core::ContentDigester digester;
    digester.update(canonical.data(), canonical.size());
    return core::ContentIdentity{digester.finalize()};
}

evidence::ConfigEntry makeTrajectoryPlanConfigRefEntry(const TrajectoryPlanConfiguration& config)
{
    // 条目三要素：kindToken（"config.trj"）＋canonical 字节＋内容身份
    // （对字节的 SHA-256——与 SnapshotBuilder 冻结期一致性校验天然一致）。
    evidence::ConfigEntry entry;
    entry.configKindToken = kConfigTrjKindToken;
    entry.canonicalBytes = TrajectoryPlanConfigCodec{}.encode(config);
    core::ContentDigester digester;
    digester.update(entry.canonicalBytes.data(), entry.canonicalBytes.size());
    entry.contentIdentity = core::ContentIdentity{digester.finalize()};
    return entry;
}

}  // namespace sdurws::ird::trajectory
