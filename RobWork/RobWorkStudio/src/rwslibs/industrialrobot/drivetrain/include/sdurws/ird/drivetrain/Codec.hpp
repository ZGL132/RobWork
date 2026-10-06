/**
 * @file   Codec.hpp
 * @brief  传动映射域的 canonical 字节编解码契约（组装方协议面）——输入侧
 *         DriveTrainModel（切片 Object 条目 `model.drivetrain` 载荷）与
 *         JointSeriesView（切片 UpstreamResult 条目 `dyn.joint-series` 载荷，
 *         P-DT-6 提议键）的编码/解码、输出侧 DriveTrainMappingOutput 载荷
 *         （EvaluationOutput.payload 的 canonical 编码）的编码/解码，以及
 *         评估键/依赖键/载荷 token 常量（③端口登记字面值的唯一书写点）。
 *
 * 设计依据：
 *   - units/drivetrain.md §5.1（输入来源与冻结链——Object/UpstreamResult/
 *     Configuration 条目）、§12.1（DynamicsJointSeries 提议 DTO——P-DT-6：
 *     对齐前评估器依赖声明使用提议键 dyn.joint-series）、§13.10（③端口
 *     注册面——评估器输出 payload canonical 形态）
 *   - evidence 契约（Evaluator.hpp——EvaluationOutput.payload＝DomainPayload
 *     {kindToken, canonicalBytes, digest}；字节对 evidence 不透明，编码契约
 *     归域——本文件即域编码契约）
 *   - 需求 NFR-COR-02（确定性——同值对象必得同字节、同字节必得等值对象）、
 *     NFR-COR-03（非有限值不得进入编码——编码入口 fail-fast）、CON-04/05
 *     （切片身份＝canonical 字节摘要——编码确定性是身份可比的前提）
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1——"dt.mapping
 *     评估器经③端口注册"的实现面；评估键词形偏差见文件尾注）
 *
 * ★ 实现口径偏差（DTB §5.4——随单元卡增量修订登记）：
 *   1. 评估键取 "dt-mapping"（kebab）：单元卡 §3.3/§13.10 记作 "dt.mapping"，
 *      但 evidence 的评估键词形闸门 isValidEvaluationKey（Slice.hpp §8.2
 *      原文语法 [a-z][a-z0-9-]{1,63}）**不含点**——含点键在注册期即被
 *      KeySyntaxInvalid 拒绝。evidence 是③端口所有者，词形闸门为其注册
 *      硬校验；本实现按词形权威取 kebab 形态并登记卡面记法偏差。
 *   2. 上游 P_joint 字段暂不消费：JointSeriesView 按 τ·q̇ 逐元素派生关节
 *      侧功率（虚功口径自洽——§8.3 复核口径）；上游独立 P_joint 字段的
 *      消费随 P-DT-6 对齐。
 *
 * 编码协议（跨组装方/评估器的稳定字节形态；版本化演进——magic 尾位数字
 * 递增）：小端定宽；头部＝8 字节 magic＋u32 codec 版本；字段按声明序固定
 * 排布；string＝u32 字节长＋UTF-8 字节；vector＝u32 元素数＋逐元素定宽；
 * id/摘要类型＝原始字节（16/32 字节）；枚举＝u8 底层值；optional<T>＝u8
 * 有无标志＋有值时 T。解码严格校验（magic/版本/长度/枚举域/索引界——违约
 * 抛 std::invalid_argument，调用方契约违约 fail-fast）。**非有限 double
 * 不得编码**（编码入口校验，NFR-COR-03——NaN/±Inf 不进入身份与持久化链）。
 *
 * 线程安全：全部纯函数（可重入）；输出按值返回。
 */

#ifndef IRD_DRIVETRAIN_CODEC_HPP
#define IRD_DRIVETRAIN_CODEC_HPP

#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace sdurws::ird::drivetrain {

// =====================================================================
// ③端口登记常量（唯一书写点——descriptor/codec/测试共用）
// =====================================================================

/// 评估键（kebab 形态——文件尾注偏差 1；evidence isValidEvaluationKey 词形）。
inline constexpr std::string_view kMappingEvaluationKey = "dt-mapping";

/// 输入/输出契约版本（>0；进 sliceId——CON-04；随契约面演进递增并登记）。
inline constexpr std::uint32_t kMappingContractVersion = 1;

/// 传动配置 Object 依赖键（组装方经该键把 DriveTrainModel 编码字节挂入切片）。
inline constexpr std::string_view kModelDrivetrainKey = "model.drivetrain";

/// 上游关节侧序列 UpstreamResult 依赖键（P-DT-6 提议键——dynamics 卡对齐前
/// 的本卡承载；对齐后如更名按注册清单同步）。
inline constexpr std::string_view kJointSeriesKey = "dyn.joint-series";

/// 求解配置 Configuration 依赖键（R1 阶段评估器不消费该条目——效率经
/// DriveTrainModel 值传递；键常量仅供组装方登记面预留，见单元卡偏差登记）。
inline constexpr std::string_view kMappingConfigKey = "config.dt-mapping";

/// 输出 payload 的域 token（DomainPayload.kindToken——词表归域，非空＋无 NUL）。
inline constexpr std::string_view kMappingPayloadToken = "dt-mapping-output-v1";

/**
 * @brief 上游序列的物化锚 id（P-DT-6 对齐前的本卡提议承载——确定性派生）。
 *
 * 背景：evidence 的 UpstreamResult 条目载荷＝UpstreamResultRef（上游切片
 * 身份＋运行身份——§4.2.1），**不含对象 id**；而评估调用上下文的读取原语
 * 是 tryObjectBytes(objectId, contentVersion)。为使序列内容在现有原语内
 * 可读，本域约定：组装方把关节侧序列字节物化在 id＝jointSeriesAnchor(
 * upstreamSliceId) 的对象下（与 Object 条目同一物化空间），评估器按同一
 * 派生规则读取——规则单点在本函数（组装方/评估器/测试共用，禁第二处
 * 字面派生）。P-DT-6 对齐后若 dynamics/evidence 冻结正式通道，此约定随
 * 注册清单同步退役。
 *
 * 派生规则：ObjectId.bytes ＝ upstreamSliceId.bytes 的前 16 字节（确定性、
 * 无随机——同上游切片必得同锚；版本参数用保留值〔全零〕＝宿主侧不做
 * 版本校验，版本一致性由上游切片身份本身承载）。
 *
 * 线程安全：纯函数。
 */
core::ObjectId jointSeriesAnchor(const core::ContentIdentity& upstreamSliceId);

// =====================================================================
// 输入侧编解码（组装方→切片条目字节→评估器解码）
// =====================================================================

/**
 * @brief 编码归一化传动模型（切片 Object 条目 `model.drivetrain` 的载荷
 *        字节形态）。
 *
 * @param model [in] 归一化模型（**须已过结构校验**——本函数不重做阻断面
 *              检查，只做编码安全校验：非有限值拒绝）
 * @return canonical 字节（同值模型必得同字节——NFR-COR-02）
 *
 * @throws std::invalid_argument 模型含非有限数值（q/τ/η/J 等任一——NFR-
 *         COR-03）或 coupling 窗口矩阵维度与声明不符（编码形态完整性）
 *
 * 线程安全：可重入纯函数。
 */
std::vector<std::uint8_t> encodeDriveTrainModel(const DriveTrainModel& model);

/**
 * @brief 解码归一化传动模型（encodeDriveTrainModel 的严格逆——解码值经
 *        再校验后构造；decode(encode(m)) 与 m 等值，NFR-COR-02 往返）。
 *
 * @param bytes [in] 切片条目载荷字节
 * @return 归一化模型（结构合法性由解码过程逐字段核对）
 *
 * @throws std::invalid_argument magic/版本/长度/枚举域/索引界违约（调用方
 *         契约违约 fail-fast——不返回半结果）
 */
DriveTrainModel decodeDriveTrainModel(const std::vector<std::uint8_t>& bytes);

/**
 * @brief 编码关节侧序列视图（切片 UpstreamResult 条目 `dyn.joint-series`
 *        的载荷字节形态——P-DT-6 提议契约的冻结承载）。
 *
 * @throws std::invalid_argument 样本含非有限值（t/q/q̇/q̈/τ 任一）
 */
std::vector<std::uint8_t> encodeJointSeries(const JointSeriesView& series);

/**
 * @brief 解码关节侧序列视图（encodeJointSeries 的严格逆）。
 *
 * @throws std::invalid_argument magic/版本/长度/枚举域/索引界违约
 */
JointSeriesView decodeJointSeries(const std::vector<std::uint8_t>& bytes);

// =====================================================================
// 输出侧编解码（评估器→payload 字节→消费域解码）
// =====================================================================

/**
 * @brief 编码映射总输出（EvaluationOutput.payload 的 canonical 字节形态；
 *        kindToken＝kMappingPayloadToken 由评估器侧装配）。
 *
 * 载荷边界（诚实声明）：本编码承载**映射数据面**（身份块/电机侧序列/
 * 工作点/反射惯量/完整性/缺失清单）；诊断记录列表不进 payload——诊断经
 * EvaluationOutput.diagnostics 通道传递（evidence 顶层字段，payload 与
 * 诊断双轨并行的契约见卡 §13.10 输出形态注）。
 *
 * @throws std::invalid_argument 输出含非有限数值（映射核心产出恒有限；
 *         伪造输出在编码入口被拒——NFR-COR-03）
 */
std::vector<std::uint8_t> encodeMappingOutput(const DriveTrainMappingOutput& output);

/**
 * @brief 解码映射总输出（encodeMappingOutput 的严格逆——消费域
 *        〔selection/reporting〕经此还原同源结果对象——§11.3 同源同身份）。
 *
 * @throws std::invalid_argument magic/版本/长度/枚举域/索引界违约
 */
DriveTrainMappingOutput decodeMappingOutput(const std::vector<std::uint8_t>& bytes);

}  // namespace sdurws::ird::drivetrain

#endif  // IRD_DRIVETRAIN_CODEC_HPP
