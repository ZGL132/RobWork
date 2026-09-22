/**
 * @file   Import.cpp
 * @brief  URDF 导入字段映射的实现——pugixml DOM 解析（O-40）、§6.3 字段
 *         映射表逐行落位、MDL-11 轴语义、§6.7 资源映射与四清单/待确认/
 *         关节状态/错误项报告；mapXacroExpanded 同边界委托＋来源留痕；
 *         mapWorkCellXml R1 边界响应（MDL-18）。
 *
 * 设计依据：
 *   - units/modeling.md §6.1～§6.4/§6.7（管线三分/字段映射表/轴语义/临时
 *     句柄与 Recorded 资源）、§9.4.3（接口契约——@pre/@post/@错误）、
 *     §9.5（MDL-IMPORT-* 码面——经 DiagCodes.hpp 常量产码）、§3.4（纯
 *     函数服务确定性/SI 总约定）
 *   - units/io.md §9.6/§10.5（输入为 io 产物；modeling 不自行读文件——
 *     SA-14；IO-RES-MISSING 缺失事实经依赖树缺失叶到达）
 *   - 需求 MDL-03/11/12/18/22、NFR-COR-01/02/03、CON-03、V-08/V-10 观测面
 *   - 任务契约 tasks/foundation/WP-13-T05.json acceptance 1~5（本提交＝
 *     DTB 规模列「解析映射」切片；链型判定两维度随第二提交）
 *
 * 实现结构（阅读地图）：
 *   匿名命名空间：locale 无关数值解析（from_chars）/稳定数值文本
 *   （to_chars）/rpy→旋转矩阵（URDF 固定轴约定，逐元素构造零框架外联
 *   符号）/惯量相似变换（inertial 系→连杆系）/名称净化（runtime 消歧
 *   预处理）/确定性临时句柄（双 FNV-1a 64 拼合 128 位）/行索引（字节
 *   偏移→行列，诊断稳定排序键）/自碰撞元素递归收集。
 *   主体 ModelImportMapper::mapUrdf 九步：①DOM 解析 ②元素扫描（收集）
 *   ③忽略/不支持面登记 ④名称净化与冲突 ⑤结构校验（引用/单父/单根）
 *   ⑥分支检测（多可动分支→拒绝未选链）⑦主链解析 ⑧草稿构造（字段
 *   映射逐行）⑨诊断稳定排序出口。每步的业务依据见段前注释。
 *
 * 确定性（NFR-COR-01/02）：全部数值路径 locale 无关（from_chars/to_chars）；
 * 浮点运算次序固定；诊断按（相对键，行，列，产出序）字典序稳定排序；
 * 同输入字节＋同 options→逐字段相等输出。线程安全：无共享可变状态。
 */

#include <sdurws/ird/modeling/Import.hpp>

#include <sdurws/ird/core/Provenance.hpp>    // core::ValueProvenance/ProvenanceKind::ImportMapped——来源标记
#include <sdurws/ird/io/IoDiagnostics.hpp>   // io::errorCodeToken——IO-RES-MISSING 码面唯一来源
#include <sdurws/ird/modeling/DiagCodes.hpp> // MDL-IMPORT-* 码值常量（唯一书写点）

#include "InertiaMath.hpp"  // 单元私有头（R-2）——I-MDL-5 SPD/三角不等式单一实现（§6.3 错误项判定）

#include <pugixml.hpp>  // O-40：URDF DOM 解析（vcpkg 经典模式 1.16/x64-windows，modeling PRIVATE）。
                        // 有界性由 io BudgetGuard 前置保证：进入本单元的字节已经 io 预算入账
                        // （SingleFileBytes/TotalBytes），DOM 规模与输入字节同阶；pugixml 默认
                        // 不解析外部实体（无 XXE/二次读文件通道——SA-14）。

#include <algorithm>
#include <cctype>        // std::tolower——relPath 折叠小写（io relPath 约定）
#include <cmath>
#include <charconv>      // std::from_chars/to_chars——locale 无关数值转换（NFR-COR-02）
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::modeling {

// =====================================================================
// 错误码 token 表（switch 全枚举、无 default——新增值漏登记编译告警）
// =====================================================================

std::string_view importErrorCodeToken(ImportErrorCode code) noexcept
{
    switch (code) {
    case ImportErrorCode::UnsupportedJointType:      return "UnsupportedJointType";
    case ImportErrorCode::MimicBlocked:              return "MimicBlocked";
    case ImportErrorCode::ZeroAxisReported:          return "ZeroAxisReported";
    case ImportErrorCode::MultiBranchNeedsSelection: return "MultiBranchNeedsSelection";
    case ImportErrorCode::ResourceMissing:           return "ResourceMissing";
    case ImportErrorCode::NameConflict:              return "NameConflict";
    case ImportErrorCode::SourceInconsistent:        return "SourceInconsistent";
    case ImportErrorCode::NotImplemented:            return "NotImplemented";
    }
    return "unknown";  // 防御（全枚举覆盖后不可达——io/modeling token 表同款口径）
}

bool ImportOutcome::operator==(const ImportOutcome& o) const
{
    // 可选成员相等：有无一致＋有值时逐字段相等（RobotDesign/ImportReport/
    // ImportError 均为纯值聚合——RobotDesign 的 == 见 RobotDesign.hpp）。
    const bool draftEqual = draft.has_value() == o.draft.has_value()
                            && (!draft.has_value() || *draft == *o.draft);
    const bool errorEqual = error.has_value() == o.error.has_value()
                            && (!error.has_value()
                                || (error->code == o.error->code
                                    && error->params == o.error->params
                                    && error->detail == o.error->detail));
    return draftEqual && report == o.report && errorEqual;
}

namespace {

// =====================================================================
// 稳定数值转换（locale 无关——NFR-COR-01/02 的实现基础）
// =====================================================================

/**
 * @brief 解析十进制浮点文本（std::from_chars——不经 locale，同文本同值）。
 *
 * 为什么不用 pugixml 的 as_double()：其内部经 strtod，受进程 LC_NUMERIC
 * 影响（不同 locale 下小数点/指数解析不同）——导入映射的确定性戒律要求
 * 数值路径 locale 无关，故取属性原文自行解析。
 *
 * @param text [in] 属性原文（十进制/科学计数；"nan"/"inf" 亦被 from_chars
 *             接受——有限性由调用方按字段语义另行判定并走非法值面）
 * @param out  [out] 解析结果（仅返回 true 时有效）
 * @return true＝整个文本被完整消费；false＝空/语法非法/残留字符
 *
 * 纯函数；线程安全；确定性。
 */
bool parseDoubleStable(std::string_view text, double* out)
{
    // 跳过首尾 ASCII 空白（URDF 属性惯例不含；保守容忍——容错不等同改值）
    std::size_t b = 0;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) { ++b; }
    std::size_t e = text.size();
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) { --e; }
    if (b >= e) { return false; }
    const char* first = text.data() + b;
    const char* last = text.data() + e;
    const auto result = std::from_chars(first, last, *out);
    return result.ec == std::errc() && result.ptr == last;
}

/**
 * @brief 浮点→稳定文本（std::to_chars 最短往返表示——locale 无关，同值
 *        同串跨进程一致；报告 valueText/错误 detail 的唯一格式化点）。
 *
 * 最短往返（而非 %.17g 定点）：位级相同的 double 必产出相同文本，往返
 * 解析（from_chars）还原位级同值——报告可比对、实现不改值，文本仅呈现
 * （无舍入声明）。
 */
std::string formatDoubleStable(double v)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, result.ptr);
}

/**
 * @brief 三维向量属性文本解析（"x y z" 空白分隔——URDF axis/origin 惯例）。
 * @return true＝恰三段且全部解析成功；false＝段数不符或任一段语法非法
 *         （"nan" 等不算语法失败——有限性由调用方按字段语义判定）
 */
bool parseVec3Stable(std::string_view text, double* x, double* y, double* z)
{
    const auto isSpace = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    std::string_view parts[3];
    int count = 0;
    std::size_t i = 0;
    while (i < text.size() && count < 3) {
        while (i < text.size() && isSpace(text[i])) { ++i; }
        if (i >= text.size()) { break; }
        const std::size_t start = i;
        while (i < text.size() && !isSpace(text[i])) { ++i; }
        parts[count++] = text.substr(start, i - start);
    }
    while (i < text.size() && isSpace(text[i])) { ++i; }
    if (count != 3 || i < text.size()) { return false; }
    return parseDoubleStable(parts[0], x) && parseDoubleStable(parts[1], y)
        && parseDoubleStable(parts[2], z);
}

// =====================================================================
// rpy → 旋转矩阵（URDF 固定轴 roll-pitch-yaw：R = Rz(yaw)·Ry(pitch)·Rx(roll)）
// =====================================================================

/**
 * @brief URDF <origin rpy="r p y"> 的旋转矩阵（单位 rad；固定轴约定——
 *        绕父系 X/Y/Z 依次旋转，等价 R=Rz·Ry·Rx）。
 *
 * 为什么不用 rw::math::RPY/Rotation3D::identity 等框架构造：SourcedValue
 * 默认构造路径与独立冒烟模式要求"冒烟可达代码零框架外联符号"（T03 落位
 * 纪律——JointPose 同款），逐元素解析式为零依赖且算式确定（同输入位级
 * 同输出）。公式为标准欧拉角展开（与 rw::math::RPY 约定逐元素一致）。
 *
 * 纯函数；线程安全；确定性。
 */
rw::math::Rotation3D<double> rpyToRotation(double roll, double pitch, double yaw)
{
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    // R = Rz(yaw)·Ry(pitch)·Rx(roll) 展开（行主序）
    return rw::math::Rotation3D<double>(
        cy * cp,
        cy * sp * sr - sy * cr,
        cy * sp * cr + sy * sr,
        sy * cp,
        sy * sp * sr + cy * cr,
        sy * sp * cr - cy * sr,
        -sp,
        cp * sr,
        cp * cr);
}

// =====================================================================
// 惯量张量相似变换（R·I·Rᵀ——inertial origin rpy≠0 时把 URDF 惯量从
// inertial 系旋到连杆系；§6.3 惯量行与 §4.3-B"连杆系参考姿态"的桥）
// =====================================================================

/**
 * @brief 对称惯量张量的正交相似变换 I' = R·I·Rᵀ（单位 kg·m²）。
 *
 * URDF <inertia> 在 <inertial><origin> 的旋转坐标系下给定；BodyData.inertia
 * 的参考姿态是连杆系（M-2/§4.3-B）——origin rpy≠0 时必须把张量旋到连杆
 * 系，否则物性语义漂移（质心基准不变：URDF 惯量本就关于质心给定）。
 *
 * 纯函数；线程安全；确定性（固定乘加次序）。
 */
InertiaTensor rotateInertia(const InertiaTensor& i, const rw::math::Rotation3D<double>& r)
{
    const double m[3][3] = {
        { i.ixx, i.ixy, i.ixz },
        { i.ixy, i.iyy, i.iyz },
        { i.ixz, i.iyz, i.izz },
    };
    double acc[3][3] = {};  // R·I
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += r(row, k) * m[k][col];
            }
            acc[row][col] = sum;
        }
    }
    double out3[3][3] = {};  // (R·I)·Rᵀ
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += acc[row][k] * r(col, k);  // Rᵀ(k,col)=R(col,k)
            }
            out3[row][col] = sum;
        }
    }
    InertiaTensor out;
    out.ixx = out3[0][0];
    out.iyy = out3[1][1];
    out.izz = out3[2][2];
    out.ixy = out3[0][1];
    out.ixz = out3[0][2];
    out.iyz = out3[1][2];
    return out;
}

// =====================================================================
// 名称净化（§6.3 robot name 行："非法字符按 runtime 消歧规则预处理并报告"）
// =====================================================================

/**
 * @brief localName 合法字符集净化（[A-Za-z0-9_.-] 之外的字节→'_'）。
 *
 * 依据：卡 §4.3 localName 行"合法字符集 [A-Za-z0-9_.-] 以 runtime 消歧
 * 规则兜底"——导入映射做预处理（逐字节替换、变化入默认补全清单），最终
 * 消歧仍归 runtime（R-4：不做名称前缀拼接/剥离）。逐字节替换是确定性
 * 映射；两个不同源名净化后可能同串——由此产生的重复按名称冲突处置
 * （NameConflict，不静默二次改名）。
 *
 * 纯函数；线程安全；确定性。
 */
std::string sanitizeLocalName(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (const char ch : raw) {
        const bool legal = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')
                           || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.'
                           || ch == '-';
        out.push_back(legal ? ch : '_');
    }
    return out;
}

// =====================================================================
// 确定性临时句柄（§6.7"tmp-<序号> 不入库"的承载）
// =====================================================================

/**
 * @brief 由（类别，序号，localName）派生确定性临时 ObjectId。
 *
 * 卡面语义（§6.7）：导入映射期间使用确定性临时句柄（不入库），真实
 * ObjectId 全部在命令 prepare 阶段由 project 分配（PA-1）。ObjectId 是
 * 128 位二进制强类型（core Identity.hpp），无文本位承载"tmp-<序号>"，
 * 故以名字符串的确定性散列派生字节：同输入同句柄，生成不经随机源（与
 * ObjectId::generate 的随机派生本质不同——随机句柄会破坏确定性草稿）。
 * 类别/序号入散列输入：关节与连杆同名不碰撞；分隔字节防串接歧义。
 *
 * 散列算法：两个独立偏移基的 FNV-1a 64 位实例拼合 128 位（core §4.1
 * std::hash 规定 FNV-1a 128 同族算法；此处独立实现以避免消费 core 私有
 * detail——R-2；仅散列用途、非密码学承诺）。
 *
 * @param kind   [in] 类别串（"joint"/"link"——仅派生输入，不外泄）
 * @param index  [in] 链上序号（0 起）
 * @param name   [in] 净化后 localName
 * @return 非全零确定性句柄（全零碰撞防御性置末字节——保留值纪律）
 *
 * 纯函数；线程安全；确定性。
 */
core::ObjectId deriveTempObjectId(std::string_view kind, std::size_t index,
                                  std::string_view name)
{
    std::uint64_t hi = 0xcbf29ce484222325ull;  // FNV-1a 64 位偏移基
    std::uint64_t lo = 0x84222325cbf29ce4ull;  // 反转字序第二偏移基（独立序列）
    const auto absorb = [&hi, &lo](unsigned char byte) {
        hi ^= byte;
        hi *= 0x100000001b3ull;  // FNV-1a 64 位素数
        lo ^= byte;
        lo *= 0x100000001b3ull;
    };
    for (const char ch : kind) { absorb(static_cast<unsigned char>(ch)); }
    absorb(0);  // 类别分隔
    for (int shift = 0; shift < 64; shift += 8) {  // 序号定宽 8 字节小端
        absorb(static_cast<unsigned char>((index >> shift) & 0xFFu));
    }
    absorb(0);  // 序号/名分隔
    for (const char ch : name) { absorb(static_cast<unsigned char>(ch)); }

    core::ObjectId id;
    for (int i = 0; i < 8; ++i) {
        id.bytes[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFu);
        id.bytes[static_cast<std::size_t>(i) + 8] =
            static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFu);
    }
    bool allZero = true;
    for (const std::uint8_t b : id.bytes) {
        if (b != 0) { allZero = false; break; }
    }
    if (allZero) { id.bytes[15] = 1; }  // 保留值兜底（全零＝空非法）
    return id;
}

// =====================================================================
// 摘要十六进制（资源状态表呈现——小写；仅编码不哈希，SA-12 不涉）
// =====================================================================

std::string digestToHex(const core::Digest256& digest)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

// =====================================================================
// 行索引（字节偏移→行/列——pugixml offset_debug 产出字节偏移，需换算）
// =====================================================================

/**
 * @brief 源文本行索引（诊断"按源文件行序稳定排序"的定位基础）。
 *
 * pugixml 只提供字节偏移（offset_debug），不提供行列——本类在解析后对
 * 源文本扫描一次换行符建立行起点表，查询经二分（对数；行数与输入同阶
 * ——预算有界）。列按字节计（UTF-8 多字节字符的"列"为字节列——定位
 * 取舍：稳定性优先于显示宽度，跨平台一致）。
 */
class LineIndex {
public:
    explicit LineIndex(std::string_view text)
    {
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') { lineStarts_.push_back(i + 1); }
        }
    }

    /// 偏移→（行,列），各 1 起；偏移无效（-1）→（0,0）＝无定位。
    std::pair<std::uint32_t, std::uint32_t> lineColumn(std::size_t offset) const
    {
        if (offset == static_cast<std::size_t>(-1)) { return {0, 0}; }
        const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
        if (it == lineStarts_.begin()) { return {0, 0}; }
        const std::size_t lineNo = static_cast<std::size_t>(it - lineStarts_.begin());
        const std::size_t col = offset - lineStarts_[lineNo - 1] + 1;
        return {static_cast<std::uint32_t>(lineNo), static_cast<std::uint32_t>(col)};
    }

private:
    std::vector<std::size_t> lineStarts_;  ///< 各行首字节偏移（升序；0 为首行起点）
};

// =====================================================================
// 诊断收集（产出＋源位置＋产出序——出口统一按行序稳定排序）
// =====================================================================

/**
 * @brief 诊断中间条目（DiagnosticRecord＋排序键）。
 *
 * 码面唯一来源：MDL-IMPORT-* 取 DiagCodes.hpp 常量、IO-RES-MISSING 取
 * io::errorCodeToken——禁字符串拼码（§9.5 尾段）。结构化参数经
 * DiagnosticRecord 的 context/cause 文本以 "key=value" 形态承载
 * （DiagnosticRecord 无独立参数表——core §4.8 字段面；键名与描述符
 * paramSchema 对齐，呈现归 diagnostics/ui 文案层）。
 */
struct DiagEntry {
    core::DiagnosticRecord record;  ///< 已构造诊断记录（make 工厂产出——C-3 校验）
    std::string relFile;            ///< 排序键：源相对键（空＝无定位，排最前）
    std::uint32_t line = 0;         ///< 排序键：行（0＝无定位）
    std::uint32_t column = 0;       ///< 排序键：列（0＝无定位）
    std::size_t seq = 0;            ///< 排序键：同位置产出序（稳定化兜底）
};

/**
 * @brief 追加一条 MDL-IMPORT-* 诊断（码常量唯一来源——DiagCodes.hpp；
 *        subject 携带草稿临时句柄，模型内定位锚；localName 冗余定位）。
 */
void pushImportDiag(std::vector<DiagEntry>& entries, std::size_t seq,
                    std::string_view code, const core::ObjectId& subject,
                    const std::string& localName, std::string cause,
                    std::string action, const ImportSourceSpan& span)
{
    DiagEntry entry;
    entry.record = core::DiagnosticRecord::make(
        std::string(code), subject, localName, std::nullopt,
        "urdf-import@" + span.relFile + ":" + std::to_string(span.line) + ":"
            + std::to_string(span.column),
        std::move(cause), std::move(action));
    entry.relFile = span.relFile;
    entry.line = span.line;
    entry.column = span.column;
    entry.seq = seq;
    entries.push_back(std::move(entry));
}

/**
 * @brief 诊断稳定排序出口（（相对键，行，列，产出序）字典序——NFR-COR-02
 *        "诊断按源文件行序稳定排序"）后一次性追加到调用方容器（追加不
 *        清空——输出参数契约）。成功/失败出口共用（失败面诊断同样有序）。
 */
void flushDiags(std::vector<DiagEntry>& entries,
                std::vector<core::DiagnosticRecord>& diags)
{
    std::sort(entries.begin(), entries.end(),
              [](const DiagEntry& a, const DiagEntry& b) {
                  if (a.relFile != b.relFile) { return a.relFile < b.relFile; }
                  if (a.line != b.line) { return a.line < b.line; }
                  if (a.column != b.column) { return a.column < b.column; }
                  return a.seq < b.seq;
              });
    for (const DiagEntry& entry : entries) {
        diags.push_back(entry.record);
    }
}

/**
 * @brief 失败出口统一收口（诊断稳定排序＋无草稿/带报告的 outcome 返回）。
 *        提前返回路径（结构违例/不可表达类型/装配违约）全部经此——保证
 *        任何出口的诊断都经过同一稳定排序。
 */
ImportOutcome finalizeFailure(std::vector<DiagEntry>& entries,
                              std::vector<core::DiagnosticRecord>& diags,
                              ImportOutcome outcome)
{
    flushDiags(entries, diags);
    return outcome;
}

// =====================================================================
// 自碰撞元素递归收集（§6.3 末行——<disable_collisions> 全树定位）
// =====================================================================

/**
 * @brief 深度优先收集全部 <disable_collisions> 元素（宿主不限——可出现
 *        在 robot 直下或 gazebo 扩展内；文档序遍历＝确定性序）。
 *
 * 为什么手写递归不用 XPath：收敛依赖面（XPath 为 pugixml 可裁剪特性，
 * 不引入对构建特性的隐式依赖）；递归深度与 DOM 深度同阶（io 预算有界）。
 */
void collectDisableCollisions(const pugi::xml_node& node,
                              std::vector<pugi::xml_node>* out)
{
    for (const pugi::xml_node& child : node.children()) {
        if (std::string(child.name()) == "disable_collisions") {
            out->push_back(child);
        }
        collectDisableCollisions(child, out);  // 元素内继续深搜（文档序）
    }
}

}  // namespace

// =====================================================================
// 主体：mapUrdf（解析映射切片）
// =====================================================================

ImportOutcome ModelImportMapper::mapUrdf(const ValidatedSource& source,
                                         const ImportOptions& options,
                                         std::vector<core::DiagnosticRecord>& diags) const
{
    (void)options;  // 本提交无选项字段（链型判定提交扩展显式选链后消费）

    ImportOutcome outcome;
    ImportReport& report = outcome.report;
    report.sourceLabel = source.dependencyTree.rootRel;

    // 诊断中间收集：每个诊断携（源位置，产出序），出口统一稳定排序——
    // "诊断按源文件行序稳定排序"（§9.4.3 确定性行）。
    std::vector<DiagEntry> diagEntries;
    std::size_t diagSeq = 0;

    // —— 元素源事实（扫描段收集；映射段只读消费——两阶段结构化映射）。
    struct InertialSrc {
        bool present = false;                       ///< <inertial> 出现
        ImportSourceSpan span;
        bool hasMass = false;                       ///< mass 解析成功（数值有效性另判）
        double mass = 0.0;                          ///< 单位 kg
        std::string massRaw;                        ///< value 属性原文（非法值保留——NFR-COR-03）
        bool massIllegal = false;                   ///< value 缺失/语法非法
        ImportSourceSpan massSpan;
        bool hasOrigin = false;
        double ox = 0.0, oy = 0.0, oz = 0.0;        ///< 质心位置 m（连杆系）
        double roll = 0.0, pitch = 0.0, yaw = 0.0;  ///< inertial 系姿态 rad（惯量参考系）
        std::string originRaw;                      ///< xyz/rpy 原文（非法值保留）
        bool originIllegal = false;
        ImportSourceSpan originSpan;
        bool hasInertia = false;                    ///< 六分量齐备且解析成功
        InertiaTensor inertia;                      ///< 原始张量（inertial 系），kg·m²
        std::string inertiaRaw;                     ///< 属性原文拼接（非法值保留）
        bool inertiaIllegal = false;
        ImportSourceSpan inertiaSpan;
    };

    struct GeometrySrc {
        bool present = false;                       ///< <geometry> 存在
        ImportSourceSpan span;
        bool isMesh = false;                        ///< mesh（box/cylinder/sphere＝图元）
        std::string meshFilename;                   ///< filename 属性原文
        bool meshNamePresent = false;
        ImportSourceSpan meshSpan;
        bool hasScale = false;
        std::string scaleText;                      ///< scale 原文（报告承载）
        double scale[3] = {1.0, 1.0, 1.0};
        bool scaleIllegal = false;
        bool isPrimitive = false;                   ///< 图元（不支持面）
        std::string primitiveKind;                  ///< 图元元素名
    };

    struct AttachmentSrc {                          ///< visual / collision 共用形态
        bool present = false;
        ImportSourceSpan span;
        bool hasOrigin = false;
        double ox = 0.0, oy = 0.0, oz = 0.0;        ///< m（连杆系）
        double roll = 0.0, pitch = 0.0, yaw = 0.0;  ///< rad
        std::string originRaw;                      ///< 非法值保留
        bool originIllegal = false;
        GeometrySrc geometry;
        std::string materialName;                   ///< <material name>（估算密度提示源）
        bool materialPresent = false;
    };

    struct LinkSrc {
        std::string rawName;
        ImportSourceSpan span;
        InertialSrc inertial;
        AttachmentSrc visual;
        int visualCount = 0;                        ///< <visual> 次数（>1＝多引用面）
        AttachmentSrc collision;
        int collisionCount = 0;
    };

    struct JointSrc {
        std::string rawName;
        std::string rawType;                        ///< type 属性原文
        ImportSourceSpan span;
        ImportSourceSpan typeSpan;
        std::string parentLink;
        std::string childLink;
        bool refsIllegal = false;                   ///< parent/child link 属性缺失
        bool hasOrigin = false;
        double ox = 0.0, oy = 0.0, oz = 0.0;        ///< m
        double roll = 0.0, pitch = 0.0, yaw = 0.0;  ///< rad
        std::string originRaw;
        bool originIllegal = false;
        ImportSourceSpan originSpan;
        bool hasAxis = false;
        double ax = 0.0, ay = 0.0, az = 0.0;
        std::string axisRaw;                        ///< xyz 原文（诊断/非法值承载）
        bool axisIllegal = false;                   ///< xyz 缺失/语法非法
        ImportSourceSpan axisSpan;
        bool hasLimit = false;
        bool hasLower = false, hasUpper = false;
        double lower = 0.0, upper = 0.0;            ///< rad（转动）/m（移动）
        std::string limitRaw;                       ///< 非法值保留
        bool limitIllegal = false;                  ///< lower/upper 存在但语法非法
        ImportSourceSpan limitSpan;
        bool hasEffort = false;
        double effort = 0.0;                        ///< N·m（Peak 候选）
        bool effortIllegal = false;
        bool hasVelocity = false;
        std::string velocityText;                   ///< 原文（无 schema 落点——报告承载）
        bool hasMimic = false;
        std::string mimicJoint;                     ///< mimic joint 属性（报告承载）
        ImportSourceSpan mimicSpan;
    };

    struct DocScan {
        std::string robotName;
        bool robotNamePresent = false;
        ImportSourceSpan robotSpan;
        std::vector<LinkSrc> links;
        std::vector<JointSrc> joints;
        int transmissionCount = 0;
        std::vector<std::pair<std::string, ImportSourceSpan>> foreignElements;
        std::vector<std::pair<std::string, ImportSourceSpan>> robotMaterials;
        std::vector<pugi::xml_node> disableCollisions;
    };

    DocScan scan;

    // -----------------------------------------------------------------
    // 第一步：DOM 解析＋行索引。
    // io 已做良构检查（IO-FORMAT-XML-SYNTAX 拦截）——此处失败属调用方装配
    // 契约违约（bytes 与 io 产物不一致），走 SourceInconsistent 值面，
    // 无草稿产出（无半成品）。
    // -----------------------------------------------------------------
    const std::string text(source.bytes.begin(), source.bytes.end());
    const LineIndex lineIndex(text);
    pugi::xml_document doc;
    const pugi::xml_parse_result parseResult =
        doc.load_buffer(text.data(), text.size(), pugi::parse_default);
    const auto spanOfNode = [&lineIndex, &rootRel = source.dependencyTree.rootRel](
                                const pugi::xml_node& node) -> ImportSourceSpan {
        const auto lc = lineIndex.lineColumn(static_cast<std::size_t>(node.offset_debug()));
        return ImportSourceSpan{rootRel, lc.first, lc.second};
    };
    if (!parseResult) {
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("stage", "xml-parse");
        err.detail = std::string("DOM 解析失败（io 良构检查应为前置）: ")
                     + parseResult.description();
        outcome.error = std::move(err);
        ImportErrorItem item;
        item.kind = "value-illegal";
        item.subject = "document";
        item.detail = parseResult.description();
        item.span = ImportSourceSpan{source.dependencyTree.rootRel, 0, 0};
        report.errors.push_back(std::move(item));
        report.submittable = false;
        return finalizeFailure(diagEntries, diags, std::move(outcome));
    }

    const pugi::xml_node root = doc.document_element();
    if (std::string(root.name()) != "robot") {
        // 根元素非 <robot>：URDF 语义层破损（良构≠合法 URDF——io 只保前者）。
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("stage", "root-element");
        err.params.emplace_back("actual", std::string(root.name()));
        err.detail = "根元素非 <robot>——URDF 语义校验失败";
        outcome.error = std::move(err);
        ImportErrorItem item;
        item.kind = "value-illegal";
        item.subject = "document/root";
        item.detail = std::string("根元素 '") + root.name() + "' 非 <robot>";
        item.span = spanOfNode(root);
        report.errors.push_back(std::move(item));
        report.submittable = false;
        return finalizeFailure(diagEntries, diags, std::move(outcome));
    }
    scan.robotSpan = spanOfNode(root);
    scan.robotNamePresent = static_cast<bool>(root.attribute("name"));
    scan.robotName = std::string(root.attribute("name").as_string(""));

    // -----------------------------------------------------------------
    // 第二步：元素扫描（收集不判定——判定在映射段以已收集事实进行）。
    // 遍历序＝文档序：全部清单条目的追加序天然按源行序（确定性的一部分）。
    // 数值一律经 parseDoubleStable（locale 无关）；非法原文全部保留
    // （NFR-COR-03——非法态保留原串，不静默转 0）。
    // -----------------------------------------------------------------
    for (const pugi::xml_node& child : root.children()) {
        const std::string name(child.name());
        if (name == "link") {
            LinkSrc link;
            link.span = spanOfNode(child);
            link.rawName = std::string(child.attribute("name").as_string(""));
            for (const pugi::xml_node& sub : child.children()) {
                const std::string subName(sub.name());
                if (subName == "inertial") {
                    // <inertial>：首个为映射源（URDF 语义单 inertial；重复
                    // 出现属源文件异常——其余进忽略清单，不静默）。
                    if (!link.inertial.present) {
                        link.inertial.present = true;
                        link.inertial.span = spanOfNode(sub);
                        for (const pugi::xml_node& is : sub.children()) {
                            const std::string isName(is.name());
                            if (isName == "origin") {
                                link.inertial.hasOrigin = true;
                                link.inertial.originSpan = spanOfNode(is);
                                const std::string xyz(is.attribute("xyz").as_string(""));
                                const std::string rpy(is.attribute("rpy").as_string(""));
                                link.inertial.originRaw = "xyz='" + xyz + "' rpy='" + rpy + "'";
                                link.inertial.originIllegal =
                                    !parseVec3Stable(xyz, &link.inertial.ox, &link.inertial.oy,
                                                     &link.inertial.oz)
                                    || !parseVec3Stable(rpy, &link.inertial.roll,
                                                        &link.inertial.pitch,
                                                        &link.inertial.yaw);
                            } else if (isName == "mass") {
                                link.inertial.massSpan = spanOfNode(is);
                                const pugi::xml_attribute value = is.attribute("value");
                                if (value) {
                                    link.inertial.massRaw = std::string(value.as_string(""));
                                    if (parseDoubleStable(link.inertial.massRaw,
                                                          &link.inertial.mass)) {
                                        link.inertial.hasMass = true;
                                    } else {
                                        link.inertial.massIllegal = true;
                                    }
                                } else {
                                    link.inertial.massIllegal = true;  // value 属性缺失
                                }
                            } else if (isName == "inertia") {
                                link.inertial.inertiaSpan = spanOfNode(is);
                                InertiaTensor raw{};
                                bool ok = true;
                                const auto attr = [&is, &ok](const char* key, double* out) {
                                    const pugi::xml_attribute a = is.attribute(key);
                                    if (!a || !parseDoubleStable(a.as_string(""), out)) {
                                        ok = false;
                                    }
                                };
                                attr("ixx", &raw.ixx);
                                attr("iyy", &raw.iyy);
                                attr("izz", &raw.izz);
                                attr("ixy", &raw.ixy);
                                attr("ixz", &raw.ixz);
                                attr("iyz", &raw.iyz);
                                // 非法值保留原文（六属性原文拼接——确定性序）
                                const auto rawAttr = [&is](const char* key) {
                                    return std::string(is.attribute(key).as_string(""));
                                };
                                link.inertial.inertiaRaw =
                                    "ixx='" + rawAttr("ixx") + "' iyy='" + rawAttr("iyy")
                                    + "' izz='" + rawAttr("izz") + "' ixy='"
                                    + rawAttr("ixy") + "' ixz='" + rawAttr("ixz")
                                    + "' iyz='" + rawAttr("iyz") + "'";
                                if (ok) {
                                    link.inertial.hasInertia = true;
                                    link.inertial.inertia = raw;
                                } else {
                                    link.inertial.inertiaIllegal = true;
                                }
                            } else {
                                scan.foreignElements.emplace_back("inertial/" + isName,
                                                                  spanOfNode(is));
                            }
                        }
                    } else {
                        scan.foreignElements.emplace_back("link/inertial(重复)", spanOfNode(sub));
                    }
                } else if (subName == "visual" || subName == "collision") {
                    AttachmentSrc& att = (subName == "visual") ? link.visual : link.collision;
                    int& attCount = (subName == "visual") ? link.visualCount : link.collisionCount;
                    ++attCount;
                    if (attCount > 1) {
                        // 多引用面：schema 单引用——首个映射、其余逐条进不支持
                        // 清单（映射段统一登记，不静默丢弃）。
                        continue;
                    }
                    att.present = true;
                    att.span = spanOfNode(sub);
                    for (const pugi::xml_node& as : sub.children()) {
                        const std::string asName(as.name());
                        if (asName == "origin") {
                            att.hasOrigin = true;
                            const std::string xyz(as.attribute("xyz").as_string(""));
                            const std::string rpy(as.attribute("rpy").as_string(""));
                            att.originRaw = "xyz='" + xyz + "' rpy='" + rpy + "'";
                            att.originIllegal =
                                !parseVec3Stable(xyz, &att.ox, &att.oy, &att.oz)
                                || !parseVec3Stable(rpy, &att.roll, &att.pitch, &att.yaw);
                        } else if (asName == "geometry") {
                            att.geometry.present = true;
                            att.geometry.span = spanOfNode(as);
                            for (const pugi::xml_node& g : as.children()) {
                                const std::string gName(g.name());
                                if (gName == "mesh") {
                                    att.geometry.isMesh = true;
                                    att.geometry.meshSpan = spanOfNode(g);
                                    const pugi::xml_attribute fn = g.attribute("filename");
                                    att.geometry.meshNamePresent = static_cast<bool>(fn);
                                    att.geometry.meshFilename =
                                        std::string(fn.as_string(""));
                                    const pugi::xml_attribute scale = g.attribute("scale");
                                    if (scale) {
                                        att.geometry.hasScale = true;
                                        att.geometry.scaleText =
                                            std::string(scale.as_string(""));
                                        att.geometry.scaleIllegal =
                                            !parseVec3Stable(att.geometry.scaleText,
                                                             &att.geometry.scale[0],
                                                             &att.geometry.scale[1],
                                                             &att.geometry.scale[2]);
                                    }
                                } else if (gName == "box" || gName == "cylinder"
                                           || gName == "sphere") {
                                    att.geometry.isPrimitive = true;
                                    att.geometry.primitiveKind = gName;
                                } else {
                                    scan.foreignElements.emplace_back("geometry/" + gName,
                                                                      spanOfNode(g));
                                }
                            }
                        } else if (asName == "material") {
                            const pugi::xml_attribute mName = as.attribute("name");
                            if (mName) {
                                att.materialPresent = true;
                                att.materialName = std::string(mName.as_string(""));
                            }
                        } else {
                            scan.foreignElements.emplace_back(subName + "/" + asName,
                                                              spanOfNode(as));
                        }
                    }
                } else {
                    scan.foreignElements.emplace_back("link/" + subName, spanOfNode(sub));
                }
            }
            scan.links.push_back(std::move(link));
        } else if (name == "joint") {
            JointSrc joint;
            joint.span = spanOfNode(child);
            joint.rawName = std::string(child.attribute("name").as_string(""));
            const pugi::xml_attribute type = child.attribute("type");
            joint.typeSpan = spanOfNode(child);
            joint.rawType = type ? std::string(type.as_string("")) : std::string();
            for (const pugi::xml_node& sub : child.children()) {
                const std::string subName(sub.name());
                if (subName == "parent" || subName == "child") {
                    const pugi::xml_attribute link = sub.attribute("link");
                    if (!link) {
                        joint.refsIllegal = true;  // link 属性缺失——结构违例
                    } else if (subName == "parent") {
                        joint.parentLink = std::string(link.as_string(""));
                    } else {
                        joint.childLink = std::string(link.as_string(""));
                    }
                } else if (subName == "origin") {
                    joint.hasOrigin = true;
                    joint.originSpan = spanOfNode(sub);
                    const std::string xyz(sub.attribute("xyz").as_string(""));
                    const std::string rpy(sub.attribute("rpy").as_string(""));
                    joint.originRaw = "xyz='" + xyz + "' rpy='" + rpy + "'";
                    joint.originIllegal =
                        !parseVec3Stable(xyz, &joint.ox, &joint.oy, &joint.oz)
                        || !parseVec3Stable(rpy, &joint.roll, &joint.pitch, &joint.yaw);
                } else if (subName == "axis") {
                    joint.hasAxis = true;
                    joint.axisSpan = spanOfNode(sub);
                    const pugi::xml_attribute xyz = sub.attribute("xyz");
                    joint.axisRaw = std::string(xyz.as_string(""));
                    if (!xyz || joint.axisRaw.empty()) {
                        joint.axisIllegal = true;  // xyz 属性缺失
                    } else {
                        joint.axisIllegal = !parseVec3Stable(joint.axisRaw, &joint.ax,
                                                             &joint.ay, &joint.az);
                    }
                } else if (subName == "limit") {
                    joint.hasLimit = true;
                    joint.limitSpan = spanOfNode(sub);
                    const pugi::xml_attribute lower = sub.attribute("lower");
                    const pugi::xml_attribute upper = sub.attribute("upper");
                    if (lower) {
                        joint.hasLower = true;
                        if (!parseDoubleStable(lower.as_string(""), &joint.lower)) {
                            joint.limitIllegal = true;
                            joint.limitRaw = std::string(lower.as_string(""));
                        }
                    }
                    if (upper) {
                        joint.hasUpper = true;
                        if (!parseDoubleStable(upper.as_string(""), &joint.upper)) {
                            joint.limitIllegal = true;
                            joint.limitRaw = std::string(upper.as_string(""));
                        }
                    }
                    const pugi::xml_attribute effort = sub.attribute("effort");
                    if (effort) {
                        joint.hasEffort = true;
                        if (!parseDoubleStable(effort.as_string(""), &joint.effort)) {
                            joint.effortIllegal = true;
                        }
                    }
                    const pugi::xml_attribute velocity = sub.attribute("velocity");
                    if (velocity) {
                        joint.hasVelocity = true;
                        joint.velocityText = std::string(velocity.as_string(""));
                        double v = 0.0;  // 值仅校验语法（无 schema 落点——不消费）
                        if (!parseDoubleStable(joint.velocityText, &v)) {
                            joint.limitIllegal = true;  // velocity 语法非法同归 limit 面
                        }
                    }
                } else if (subName == "mimic") {
                    joint.hasMimic = true;
                    joint.mimicSpan = spanOfNode(sub);
                    joint.mimicJoint = std::string(sub.attribute("joint").as_string(""));
                } else {
                    scan.foreignElements.emplace_back("joint/" + subName, spanOfNode(sub));
                }
            }
            scan.joints.push_back(std::move(joint));
        } else if (name == "transmission") {
            ++scan.transmissionCount;  // 不支持面（§6.3——线性耦合候选提示，R2）
        } else if (name == "material") {
            // robot 级材质定义：仅颜色/纹理——无密度语义；body.material 的
            // 映射源是 visual 内联材质名。robot 级定义入忽略清单（报告不
            // 解释其颜色语义，可观察不静默）。
            scan.robotMaterials.emplace_back(
                std::string(child.attribute("name").as_string("")), spanOfNode(child));
        } else {
            // <gazebo> 等外来扩展（§6.3 忽略清单——报告不解释）。
            scan.foreignElements.emplace_back(name, spanOfNode(child));
        }
    }

    // disable_collisions 全树收集（§6.3 末行——自碰撞配置；P-MDL-3：仅
    // 报告候选，不写策略对象、不入编码权威语义）。
    collectDisableCollisions(root, &scan.disableCollisions);
    for (const pugi::xml_node& dc : scan.disableCollisions) {
        ImportSelfCollisionItem candidate;
        candidate.link1 = std::string(dc.attribute("link1").as_string(""));
        candidate.link2 = std::string(dc.attribute("link2").as_string(""));
        candidate.span = spanOfNode(dc);
        report.selfCollisionCandidates.push_back(std::move(candidate));
    }

    // -----------------------------------------------------------------
    // 第三步：忽略/不支持清单的扫描面登记（链无关内容——无论链型判定
    // 结果如何都必须可观察，MDL-03"不静默"）。
    // -----------------------------------------------------------------
    for (const auto& foreign : scan.foreignElements) {
        ImportIgnoredItem item;
        item.element = foreign.first;
        item.reason = "外来扩展/未登记元素——报告不解释（§6.3 忽略清单）";
        item.span = foreign.second;
        report.ignored.push_back(std::move(item));
    }
    for (const auto& material : scan.robotMaterials) {
        ImportIgnoredItem item;
        item.element = "material '" + material.first + "'";
        item.reason = "robot 级材质定义（仅颜色）——无密度语义，估算不可用；"
                      "body.material 映射源为 visual 内联材质名";
        item.span = material.second;
        report.ignored.push_back(std::move(item));
    }
    if (scan.transmissionCount > 0) {
        ImportUnsupportedItem item;
        item.kind = "transmission";
        item.subject = "transmission x" + std::to_string(scan.transmissionCount);
        item.reason = "URDF 传动声明不映射（§6.3——线性耦合候选提示，R2/MDL-21）";
        item.guidance = "如需耦合建模，待阶段 D（MDL-21 启用）经传动设计对象登记";
        item.span = scan.robotSpan;
        report.unsupported.push_back(std::move(item));
    }

    // -----------------------------------------------------------------
    // 第四步：名称净化与冲突检测（I-MDL-2；净化变化入默认补全清单）。
    // 冲突判据＝净化后的最终 localName（两个不同源名净化同串＝真实冲突，
    // 不得静默二次改名）；链接内/关节内各自唯一（URDF 两命名空间；跨类
    // 同名合法——runtime 名称映射消歧兜底，R-4）。
    // -----------------------------------------------------------------
    std::vector<std::string> linkNames;
    linkNames.reserve(scan.links.size());
    for (const LinkSrc& link : scan.links) {
        linkNames.push_back(sanitizeLocalName(link.rawName));
    }
    std::vector<std::string> jointNames;
    jointNames.reserve(scan.joints.size());
    for (const JointSrc& joint : scan.joints) {
        jointNames.push_back(sanitizeLocalName(joint.rawName));
    }
    bool nameConflict = false;
    for (std::size_t i = 0; i < linkNames.size(); ++i) {
        for (std::size_t j = i + 1; j < linkNames.size(); ++j) {
            if (linkNames[i] == linkNames[j]) {
                nameConflict = true;
                ImportErrorItem item;
                item.kind = "name-conflict";
                item.subject = "links[" + std::to_string(i) + "]/links["
                               + std::to_string(j) + "]";
                item.detail = "连杆 localName 重复（净化后）: " + linkNames[i];
                item.span = scan.links[i].span;
                report.errors.push_back(std::move(item));
            }
        }
    }
    for (std::size_t i = 0; i < jointNames.size(); ++i) {
        for (std::size_t j = i + 1; j < jointNames.size(); ++j) {
            if (jointNames[i] == jointNames[j]) {
                nameConflict = true;
                ImportErrorItem item;
                item.kind = "name-conflict";
                item.subject = "joints[" + std::to_string(i) + "]/joints["
                               + std::to_string(j) + "]";
                item.detail = "关节 localName 重复（净化后）: " + jointNames[i];
                item.span = scan.joints[i].span;
                report.errors.push_back(std::move(item));
            }
        }
    }
    if (nameConflict) {
        ImportError err;
        err.code = ImportErrorCode::NameConflict;
        err.params.emplace_back("scope", "local-name");
        err.detail = "localName 重复（净化后仍冲突）——应用边界前拦截（I-MDL-2）";
        outcome.error = std::move(err);
        report.submittable = false;
        // 不返回：草稿仍产出（呈现/编辑用），不可提交（submittable=false）。
    }

    // -----------------------------------------------------------------
    // 第五步：结构校验（URDF 语义层——io 只保 XML 良构）。
    // ①引用完整：joint.parent/child 指向存在的 link；②单父：每 link 至多
    // 一个 joint 以其为 child；③单根：恰一个不被引用为 child 的 link。
    // 违例→SourceInconsistent（结构无法确信——无草稿，无半成品）。
    // -----------------------------------------------------------------
    const std::size_t kNone = static_cast<std::size_t>(-1);
    const auto findLinkByRaw = [&scan, kNone](const std::string& raw) -> std::size_t {
        for (std::size_t i = 0; i < scan.links.size(); ++i) {
            if (scan.links[i].rawName == raw) { return i; }
        }
        return kNone;
    };
    std::vector<std::size_t> childLinkOfJoint(scan.joints.size(), kNone);   ///< 关节的子连杆
    std::vector<std::size_t> parentLinkOfJoint(scan.joints.size(), kNone);  ///< 关节的父连杆
    std::vector<std::size_t> parentJointOfLink(scan.links.size(), kNone);   ///< 连杆的父关节
    std::vector<std::vector<std::size_t>> childJointsOfLink(scan.links.size());
    bool structureBroken = false;
    for (std::size_t j = 0; j < scan.joints.size(); ++j) {
        const JointSrc& joint = scan.joints[j];
        if (joint.refsIllegal) { structureBroken = true; continue; }
        const std::size_t parent = findLinkByRaw(joint.parentLink);
        const std::size_t child = findLinkByRaw(joint.childLink);
        if (parent == kNone || child == kNone) {
            structureBroken = true;
            ImportErrorItem item;
            item.kind = "name-conflict";
            item.subject = "joint '" + joint.rawName + "'";
            item.detail = "parent/child link 引用悬空（URDF 结构违例）";
            item.span = joint.span;
            report.errors.push_back(std::move(item));
            continue;
        }
        childLinkOfJoint[j] = child;
        parentLinkOfJoint[j] = parent;
        childJointsOfLink[parent].push_back(j);
        if (parentJointOfLink[child] != kNone) {
            structureBroken = true;
            ImportErrorItem item;
            item.kind = "name-conflict";
            item.subject = "link '" + scan.links[child].rawName + "'";
            item.detail = "连杆被多个 joint 声明为 child（非树结构——URDF 违例）";
            item.span = scan.links[child].span;
            report.errors.push_back(std::move(item));
        } else {
            parentJointOfLink[child] = j;
        }
    }
    std::size_t rootLink = kNone;
    for (std::size_t l = 0; l < scan.links.size(); ++l) {
        if (parentJointOfLink[l] == kNone) {
            if (rootLink != kNone) {
                structureBroken = true;  // 多根（森林）——兜底面
                ImportErrorItem item;
                item.kind = "name-conflict";
                item.subject = "link '" + scan.links[l].rawName + "'";
                item.detail = "多根 URDF（森林结构）——单根违例";
                item.span = scan.links[l].span;
                report.errors.push_back(std::move(item));
            } else {
                rootLink = l;
            }
        }
    }
    if (rootLink == kNone && !structureBroken) {
        structureBroken = true;
        ImportErrorItem item;
        item.kind = "name-conflict";
        item.subject = "document";
        item.detail = "无根连杆（环状引用）——单根违例";
        item.span = scan.robotSpan;
        report.errors.push_back(std::move(item));
    }
    if (structureBroken) {
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("stage", "structure");
        err.detail = "URDF 结构违例（悬空引用/多父/多根/环）——无草稿产出";
        outcome.error = std::move(err);
        report.submittable = false;
        return finalizeFailure(diagEntries, diags, std::move(outcome));
    }

    // -----------------------------------------------------------------
    // 第六步：分支检测（§6.4 维度一——本提交切片：多可动分支→拒绝未选链，
    // 分支对象经 error params 可观察；选链接收与分支报告条目/辅助分支
    // 候选处置随链型判定提交）。
    // 可动关节＝revolute|continuous|prismatic（fixed 为结构连接不计数；
    // I-MDL-1：Fixed 可存在于链上——主链含 fixed 属正常形态）。
    // -----------------------------------------------------------------
    const auto jointTypeOf = [](const std::string& rawType) -> int {
        // 返回值与 JointType 枚举序对齐（0=Revolute 1=Continuous
        // 2=Prismatic 3=Fixed）；-1＝不可表达/未知（planar/floating/垃圾
        // 值——运动承载面，链成形后由映射段的类型检查统一阻断——V-10：
        // "识别＋报告＋阻断"先于结构拒绝）。
        if (rawType == "revolute") { return 0; }
        if (rawType == "continuous") { return 1; }
        if (rawType == "prismatic") { return 2; }
        if (rawType == "fixed") { return 3; }
        return -1;
    };
    std::vector<int> jointTypes;
    jointTypes.reserve(scan.joints.size());
    for (const JointSrc& joint : scan.joints) {
        jointTypes.push_back(jointTypeOf(joint.rawType));
    }
    // 可动（可入草稿的三类）与运动承载（含不可表达类型——分支/链成形判定
    // 用：未知类型代表"有运动但暂不可表达"，不能当 fixed 折叠掉，否则
    // 纯 planar 文件会被误报"无可建模主链"而非 UNSUPPORTED-JOINT）。
    const auto isMovableType = [](int t) { return t == 0 || t == 1 || t == 2; };
    const auto isMotionBearing = [](int t) {
        return t == 0 || t == 1 || t == 2 || t < 0;
    };

    // 子树可动性（树上前序收集＋逆序聚合——迭代实现，递归深度不受输入控制）。
    // subtreeHasMovable[c]＝"c 的严格后代中存在可动关节"（不含 c 的父关节
    // ——父关节的可动性在判定"分支是否含运动"时单独并入，见 branchHasMotion）。
    std::vector<bool> subtreeHasMovable(scan.links.size(), false);
    {
        std::vector<std::size_t> order;
        std::vector<bool> visited(scan.links.size(), false);
        std::vector<std::size_t> stack{rootLink};
        visited[rootLink] = true;
        while (!stack.empty()) {
            const std::size_t cur = stack.back();
            stack.pop_back();
            order.push_back(cur);
            for (const std::size_t j : childJointsOfLink[cur]) {
                const std::size_t c = childLinkOfJoint[j];
                if (c != kNone && !visited[c]) {
                    visited[c] = true;
                    stack.push_back(c);
                }
            }
        }
        // 逆前序＝子先于父：子树可动性自下而上聚合（运动承载口径）。
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            for (const std::size_t j : childJointsOfLink[*it]) {
                const std::size_t c = childLinkOfJoint[j];
                if (c != kNone && (isMotionBearing(jointTypes[j]) || subtreeHasMovable[c])) {
                    subtreeHasMovable[*it] = true;
                }
            }
        }
    }
    // 分支含运动判定：l 的子关节 j 通向的子树是否含可动运动＝关节自身
    // 运动承载（可动或不可表达类型——后者阻断面在映射段）或子树更深处
    // 有运动（两判缺一不可——叶上的可动关节使子树判定为 false 但分支
    // 仍然有效）。
    const auto branchHasMotion = [&](std::size_t j) {
        const std::size_t c = childLinkOfJoint[j];
        return c != kNone
               && (isMotionBearing(jointTypes[j]) || subtreeHasMovable[c]);
    };

    // 分裂检测：某 link 有 ≥2 个"分支含运动"的子关节→多可动分支。
    {
        std::vector<std::string> branchRoots;
        std::string splitLinkName;
        for (std::size_t l = 0; l < scan.links.size(); ++l) {
            std::vector<std::string> movableChildRoots;
            for (const std::size_t j : childJointsOfLink[l]) {
                if (branchHasMotion(j)) {
                    movableChildRoots.push_back(linkNames[childLinkOfJoint[j]]);
                }
            }
            if (movableChildRoots.size() >= 2) {
                splitLinkName = linkNames[l];
                branchRoots = std::move(movableChildRoots);
                break;  // 取最上（先遇到）分裂点——确定性
            }
        }
        if (!branchRoots.empty()) {
            // 多可动分支＋未选链（本提交切片）：拒绝产出草稿——未经用户
            // 显式选择不排除可动分支（DTB 禁止项）；分支对象与原因经
            // error params 可观察（report.branches 条目随链型判定提交增列）。
            ImportError err;
            err.code = ImportErrorCode::MultiBranchNeedsSelection;
            err.params.emplace_back("branch-count", std::to_string(branchRoots.size()));
            std::string roots;
            for (std::size_t i = 0; i < branchRoots.size(); ++i) {
                if (i > 0) { roots += ","; }
                roots += branchRoots[i];
            }
            err.params.emplace_back("branch-roots", roots);
            err.params.emplace_back("split-link", splitLinkName);
            err.detail = "多可动分支：须用户显式选择主链后重试（§6.4 维度一；"
                         "MDL-IMPORT-BRANCH-SELECTION 报告面随链型判定提交）";
            outcome.error = std::move(err);
            report.submittable = false;
            return finalizeFailure(diagEntries, diags, std::move(outcome));
        }
    }

    // -----------------------------------------------------------------
    // 第七步：主链解析（单可动链直通——§6.4 维度一第一行）。
    // 主链＝根到叶：每步取唯一"子树含可动关节"的子关节；主链外的 fixed
    // 子树（无可动关节）逐元素进忽略清单（不构成拒绝——候选处置随链型
    // 判定提交，可观察不静默）。
    // -----------------------------------------------------------------
    std::vector<std::size_t> chainJoints;   ///< 链序（root→leaf）
    std::vector<std::size_t> chainLinks;    ///< links[0]=root … size=关节+1（I-MDL-1）
    {
        std::size_t cur = rootLink;
        chainLinks.push_back(cur);
        while (true) {
            std::size_t nextJoint = kNone;
            for (const std::size_t j : childJointsOfLink[cur]) {
                if (branchHasMotion(j)) {
                    nextJoint = j;
                    break;
                }
            }
            if (nextJoint == kNone) { break; }  // 叶——主链结束
            chainJoints.push_back(nextJoint);
            cur = childLinkOfJoint[nextJoint];
            chainLinks.push_back(cur);
        }
        if (chainJoints.empty()) {
            // 无任何可动关节：不构成 robot 链（I-MDL-1 joints≥1）。
            ImportError err;
            err.code = ImportErrorCode::SourceInconsistent;
            err.params.emplace_back("stage", "chain");
            err.detail = "文件不含可动关节（无可建模主链）——无草稿产出";
            outcome.error = std::move(err);
            report.submittable = false;
            return finalizeFailure(diagEntries, diags, std::move(outcome));
        }
    }
    {
        // 主链外元素登记（fixed 连接子树——§6.4"辅助分支不构成拒绝"）。
        std::vector<bool> linkOnChain(scan.links.size(), false);
        for (const std::size_t l : chainLinks) { linkOnChain[l] = true; }
        std::vector<bool> jointOnChain(scan.joints.size(), false);
        for (const std::size_t j : chainJoints) { jointOnChain[j] = true; }
        for (std::size_t l = 0; l < scan.links.size(); ++l) {
            if (!linkOnChain[l]) {
                ImportIgnoredItem item;
                item.element = "link '" + linkNames[l] + "'";
                item.reason = "主链外 fixed 连接子树——不构成拒绝；场景/环境候选"
                              "处置随链型判定提交（§6.4 维度一辅助分支）";
                item.span = scan.links[l].span;
                report.ignored.push_back(std::move(item));
            }
        }
        for (std::size_t j = 0; j < scan.joints.size(); ++j) {
            if (!jointOnChain[j]) {
                if (jointTypes[j] < 0) {
                    // 主链外的不可表达类型：识别面不缺席——登记不支持条目
                    // （不阻断：该分支不进草稿，选择另一分支即不经过它——
                    // 这是合法选链而非绕过；V-10 的阻断对象是所选主链）。
                    ImportUnsupportedItem item;
                    item.kind = "joint-type";
                    item.subject = "joint '" + jointNames[j] + "'（主链外分支）";
                    item.reason = "关节类型 '" + scan.joints[j].rawType
                                  + "' 不可表达——分支不映射（候选处置随链型判定"
                                  "提交）";
                    item.guidance = "该分支不进入草稿；如需建模请改类型或拆分文件";
                    item.span = scan.joints[j].span;
                    report.unsupported.push_back(std::move(item));
                    continue;
                }
                ImportIgnoredItem item;
                item.element = "joint '" + jointNames[j] + "'";
                item.reason = "主链外分支关节——不构成拒绝；场景/环境候选处置随"
                              "链型判定提交（§6.4 维度一辅助分支）";
                item.span = scan.joints[j].span;
                report.ignored.push_back(std::move(item));
            }
        }
    }

    // -----------------------------------------------------------------
    // 第八步：草稿构造（§6.3 映射表逐行）。
    // 全部 Provided 值以 ImportMapped 来源标记落位（CON-01——来源可溯）；
    // 非法数值以 SourcedValue::invalid 保留原串（NFR-COR-03——不静默转 0）。
    // -----------------------------------------------------------------
    RobotDesign draft;
    draft.authority = AuthorityMode::Explicit;  // 导入映射产显式权威草稿（axis/origin 直映射为权威一等字段——MDL-09）

    // 来源标记的统一构造点（ImportMapped＋urdf-import 方法标记——五类
    // 来源之一，CON-01；methodTag 词法 [a-z0-9./_-] 经 core 工厂校验）。
    const auto importProvenance = [](std::string_view tag) {
        return core::ValueProvenance::make(core::ProvenanceKind::ImportMapped, {},
                                           std::nullopt, std::string(tag));
    };

    // —— <robot name>：displayName（呈现名保留原文）＋根 localName 候选
    //    （净化——非法字符替换入默认补全清单，NFR-COR-03 不静默）。
    draft.displayName = scan.robotName;
    {
        const std::string sanitized = sanitizeLocalName(scan.robotName);
        if (scan.robotNamePresent && sanitized != scan.robotName) {
            ImportDefaultItem item;
            item.field = "root-local-name-candidate";
            item.appliedValue = sanitized;
            item.reason = "robot name 含非法字符——runtime 消歧预处理（§6.3；"
                          "最终消歧归 runtime，R-4）";
            item.span = scan.robotSpan;
            report.defaults.push_back(std::move(item));
        } else if (!scan.robotNamePresent) {
            ImportDefaultItem item;
            item.field = "displayName";
            item.appliedValue = "(空)";
            item.reason = "<robot> name 属性缺失——呈现名留空（可编辑）";
            item.span = scan.robotSpan;
            report.defaults.push_back(std::move(item));
        }
        ImportMappedItem mapped;
        mapped.sourcePath = "robot@name";
        mapped.targetField = "displayName + root-local-name-candidate";
        mapped.valueText = scan.robotName + " / " + sanitizeLocalName(scan.robotName);
        mapped.note = "§6.3 首行——名称映射（净化变化入默认补全清单）";
        mapped.span = scan.robotSpan;
        report.mapped.push_back(std::move(mapped));
    }

    // —— basePlacement：无安装语义→ground（MDL-22/V15-04），来源
    //    ImportMapped 入默认补全清单（NFR-COR-03；验收 2 点名面）。
    draft.basePlacement = BasePlacement{};  // preset=Ground 缺省——显式赋值表意
    {
        ImportDefaultItem item;
        item.field = "basePlacement";
        item.appliedValue = "ground(preset)";
        item.reason = "URDF 无安装语义——MDL-22 默认地面（来源 import-mapped 入清单）";
        item.span = scan.robotSpan;
        report.defaults.push_back(std::move(item));
    }

    // —— 关节逐个映射（链序）。序：类型面（不可表达→整场失败无草稿；
    //     mimic→类型保留＋阻断面）→ origin → axis（MDL-11 全语义）→
    //     limits/effort/velocity（§6.3 limit 行）。
    bool axisBlocked = false;    ///< 存在零轴/非有限轴关节（MDL-11——不可提交）
    bool mimicBlocked = false;   ///< 存在 mimic 关节（MDL-12——不可提交）
    bool valueIllegal = false;   ///< 存在非法数值面（origin/limit/mass 等）
    for (std::size_t ci = 0; ci < chainJoints.size(); ++ci) {
        const std::size_t j = chainJoints[ci];
        const JointSrc& src = scan.joints[j];
        const std::string& name = jointNames[j];
        // 关节临时句柄（确定性——§6.7）：诊断 subject 与草稿 objectId 同源。
        const core::ObjectId jointTempId = deriveTempObjectId("joint", ci, name);

        // —— 关节类型（§6.3 joint 行：四类识别；不可表达→UnsupportedJoint
        //     Type 无草稿——不得转 FixedFrame，M-6/V-10）。
        const int type = jointTypes[j];
        if (type < 0) {
            ImportUnsupportedItem item;
            item.kind = "joint-type";
            item.subject = "joint '" + name + "'";
            item.reason = "关节类型 '" + src.rawType
                          + "' 不可表达（JointType 词表四值之外）——不得转 FixedFrame（M-6）";
            item.guidance = "修改源文件为 revolute/continuous/prismatic/fixed";
            item.span = src.typeSpan;
            report.unsupported.push_back(std::move(item));
            pushImportDiag(diagEntries, diagSeq++, kMdlImportUnsupportedJoint,
                           jointTempId, name,
                           "source-type=" + src.rawType + "（词表外）",
                           "移除该关节或改拓扑——不得转 FixedFrame/经选链绕过（MDL-12）",
                           src.typeSpan);
            ImportError err;
            err.code = ImportErrorCode::UnsupportedJointType;
            err.params.emplace_back("joint-name", name);
            err.params.emplace_back("source-type", src.rawType);
            err.detail = "不可表达关节类型——无草稿产出（不得转 FixedFrame/绕过）";
            outcome.error = std::move(err);
            report.submittable = false;
            return finalizeFailure(diagEntries, diags, std::move(outcome));
        }

        JointEntry entry;
        entry.objectId = jointTempId;
        entry.localName = name;
        entry.type = static_cast<JointType>(type);  // 枚举序＝判别序（Revolute..Fixed）
        report.jointStatuses.push_back(
            ImportJointStatusItem{name, "mapped", "类型 " + src.rawType, src.span});

        // —— mimic（§6.3 mimic 行：识别＋报告＋阻断；类型保留不转 Fixed）。
        if (src.hasMimic) {
            mimicBlocked = true;
            ImportUnsupportedItem item;
            item.kind = "mimic-joint";
            item.subject = "joint '" + name + "' → '" + src.mimicJoint + "'";
            item.reason = "mimic 耦合语义不映射（MDL-12/MDL-21 R1）——关节类型保留"
                          "（不转 FixedFrame），草稿不可提交（V-10）";
            item.guidance = "移除 mimic 或等待阶段 D 线性耦合（MDL-21）通道";
            item.span = src.mimicSpan;
            report.unsupported.push_back(std::move(item));
            pushImportDiag(diagEntries, diagSeq++, kMdlImportUnsupportedJoint,
                           jointTempId, name,
                           "source-type=mimic mimic-joint=" + src.mimicJoint,
                           "移除 mimic 或等待阶段 D 线性耦合通道（不得经选链绕过）",
                           src.mimicSpan);
        }

        // —— origin（T_parent_joint，m/rad；§4.3-A origin 行）。
        if (src.hasOrigin) {
            if (src.originIllegal) {
                valueIllegal = true;
                entry.origin = core::SourcedValue<JointPose>::invalid(src.originRaw);
                ImportErrorItem item;
                item.kind = "value-illegal";
                item.subject = "joint '" + name + "'.origin";
                item.detail = "origin 数值非法（原文保留: " + src.originRaw + "）";
                item.span = src.originSpan;
                report.errors.push_back(std::move(item));
                report.jointStatuses.back().status = "invalid";
                report.jointStatuses.back().reason = "origin 数值非法";
            } else {
                const rw::math::Transform3D<double> t(
                    rw::math::Vector3D<double>(src.ox, src.oy, src.oz),
                    rpyToRotation(src.roll, src.pitch, src.yaw));
                entry.origin = core::SourcedValue<JointPose>::provided(
                    JointPose(t), importProvenance("urdf-import"));
                ImportMappedItem mapped;
                mapped.sourcePath = "joint[" + std::to_string(ci) + "]/origin";
                mapped.targetField = "joints[" + std::to_string(ci) + "].origin";
                mapped.valueText = "xyz(" + formatDoubleStable(src.ox) + " "
                                   + formatDoubleStable(src.oy) + " "
                                   + formatDoubleStable(src.oz) + ") m; rpy("
                                   + formatDoubleStable(src.roll) + " "
                                   + formatDoubleStable(src.pitch) + " "
                                   + formatDoubleStable(src.yaw) + ") rad";
                mapped.note = "来源 import-mapped；T_parent_joint（core §4.6）；"
                              "zeroOffset=0（schema 缺省——URDF 无零位偏置概念）";
                mapped.span = src.originSpan;
                report.mapped.push_back(std::move(mapped));
            }
        } else {
            entry.origin = core::SourcedValue<JointPose>::notProvided();
            ImportDefaultItem item;
            item.field = "joints[" + std::to_string(ci) + "].origin";
            item.appliedValue = "not-provided";
            item.reason = "<origin> 缺失——恒等位姿待补全（SourcedValue NotProvided 降级）";
            item.span = src.span;
            report.defaults.push_back(std::move(item));
        }

        // —— axis（MDL-11 全语义；仅可动关节）。
        if (isMovableType(type)) {
            if (src.hasAxis && !src.axisIllegal) {
                const bool finite = std::isfinite(src.ax) && std::isfinite(src.ay)
                                    && std::isfinite(src.az);
                const double normSq = src.ax * src.ax + src.ay * src.ay + src.az * src.az;
                // 非有限或零轴（norm²==0；极端量级经 IEEE 溢出/下溢同样落入
                // 非有限/零判——确定性不受影响）→ Invalid 面（保留原串）。
                if (!finite || !(normSq > 0.0)) {
                    axisBlocked = true;
                    entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::invalid(
                        src.axisRaw);
                    ImportJointStatusItem& status = report.jointStatuses.back();
                    status.status = "invalid";
                    status.reason = "轴非法（原文: " + src.axisRaw + "）";
                    ImportErrorItem item;
                    item.kind = "value-illegal";
                    item.subject = "joint '" + name + "'.axis";
                    item.detail = "轴分量非有限或零向量（原文保留: " + src.axisRaw + "）";
                    item.span = src.axisSpan;
                    report.errors.push_back(std::move(item));
                    pushImportDiag(diagEntries, diagSeq++, kMdlImportZeroAxis,
                                   jointTempId, name,
                                   "joint-name=" + name + " axis-raw=" + src.axisRaw,
                                   "修正源文件——零轴/非有限轴草稿不得提交修订（MDL-11）",
                                   src.axisSpan);
                } else {
                    const double norm = std::sqrt(normSq);
                    const rw::math::Vector3D<double> axis(src.ax / norm, src.ay / norm,
                                                          src.az / norm);
                    entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                        axis, importProvenance("urdf-import"));
                    ImportMappedItem mapped;
                    mapped.sourcePath = "joint[" + std::to_string(ci) + "]/axis";
                    mapped.targetField = "joints[" + std::to_string(ci) + "].axis";
                    mapped.valueText = "(" + formatDoubleStable(axis[0]) + " "
                                       + formatDoubleStable(axis[1]) + " "
                                       + formatDoubleStable(axis[2])
                                       + ") 单位向量（无量纲）";
                    mapped.note = "任意有限非零轴可接受（不要求 Z——MDL-11）；归一化存储";
                    mapped.span = src.axisSpan;
                    report.mapped.push_back(std::move(mapped));
                }
            } else if (src.hasAxis) {
                // axis 存在但语法非法：同入 Invalid 面（保留原串，不静默改值）。
                axisBlocked = true;
                entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::invalid(
                    src.axisRaw);
                ImportJointStatusItem& status = report.jointStatuses.back();
                status.status = "invalid";
                status.reason = "轴属性语法非法（原文: " + src.axisRaw + "）";
                ImportErrorItem item;
                item.kind = "value-illegal";
                item.subject = "joint '" + name + "'.axis";
                item.detail = "axis xyz 语法非法（原文保留: " + src.axisRaw + "）";
                item.span = src.axisSpan;
                report.errors.push_back(std::move(item));
                pushImportDiag(diagEntries, diagSeq++, kMdlImportZeroAxis,
                               jointTempId, name,
                               "joint-name=" + name + " axis-raw=" + src.axisRaw,
                               "修正源文件——非法轴草稿不得提交修订（MDL-11）",
                               src.axisSpan);
            } else {
                // 缺 <axis>：按 URDF 语义取局部 +X，入待确认草稿清单
                // （MDL-11 后半——用户确认后可应用；不静默：默认值入默认
                // 补全清单＋待确认条目＋ Warning 诊断三重登记）。
                entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                    rw::math::Vector3D<double>(1.0, 0.0, 0.0),
                    importProvenance("urdf-import/axis-default"));
                ImportDefaultItem item;
                item.field = "joints[" + std::to_string(ci) + "].axis";
                item.appliedValue = "(1, 0, 0)（关节局部系 +X）";
                item.reason = "<axis> 缺失——URDF 语义默认 +X（MDL-11；待确认）";
                item.span = src.span;
                report.defaults.push_back(std::move(item));
                ImportPendingItem pending;
                pending.kind = "axis-default-plus-x";
                pending.subject = "joint '" + name + "'";
                pending.question = "轴向未在源文件声明，已默认关节局部 +X——请确认";
                pending.span = src.span;
                report.pendingConfirms.push_back(std::move(pending));
                pushImportDiag(diagEntries, diagSeq++, kMdlImportPendingConfirm,
                               jointTempId, name,
                               "item-kind=axis-default-plus-x subject=joint '" + name
                                   + "'",
                               "逐条确认——缺 axis 默认 +X（MDL-11/MDL-IMPORT-PENDING-CONFIRM）",
                               src.span);
            }
        } else if (src.hasAxis) {
            // fixed 关节无轴语义（URDF 未定义——入忽略面，不解释）。
            ImportIgnoredItem item;
            item.element = "joint '" + name + "'/axis";
            item.reason = "fixed 关节无轴语义——忽略（不解释）";
            item.span = src.axisSpan;
            report.ignored.push_back(std::move(item));
        }

        // —— limits（§6.3 limit 行）。
        if (type == 0 || type == 2) {
            // revolute/prismatic：有限限位可动关节（I-MDL-4；单位 rad/m）。
            if (src.hasLower && src.hasUpper && !src.limitIllegal) {
                entry.bounds = core::SourcedValue<JointLimits>::provided(
                    JointLimits{src.lower, src.upper}, importProvenance("urdf-import"));
                if (src.lower < src.upper) {
                    ImportMappedItem mapped;
                    mapped.sourcePath = "joint[" + std::to_string(ci) + "]/limit";
                    mapped.targetField = "joints[" + std::to_string(ci) + "].bounds";
                    mapped.valueText = "qmin=" + formatDoubleStable(src.lower) + " qmax="
                                       + formatDoubleStable(src.upper) + " "
                                       + (type == 0 ? "rad" : "m");
                    mapped.note = "来源 import-mapped";
                    mapped.span = src.limitSpan;
                    report.mapped.push_back(std::move(mapped));
                } else {
                    // qmin≥qmax：Provided 保留原值（不静默修复），错误项＋
                    // 不可提交（断言④在应用边界兜底——§6.3"应用将被断言
                    // 阻断"的导入期预提示面）。
                    valueIllegal = true;
                    ImportErrorItem item;
                    item.kind = "limit-order";
                    item.subject = "joint '" + name + "'.bounds";
                    item.detail = "qmin≥qmax（" + formatDoubleStable(src.lower) + " ≥ "
                                  + formatDoubleStable(src.upper) + " "
                                  + (type == 0 ? "rad" : "m") + "）";
                    item.span = src.limitSpan;
                    report.errors.push_back(std::move(item));
                    report.jointStatuses.back().status = "invalid";
                    report.jointStatuses.back().reason = "限位区间无序";
                }
            } else if (src.limitIllegal) {
                // limit 属性语法非法：invalid 态保留原串＋错误项＋invalid。
                entry.bounds = core::SourcedValue<JointLimits>::invalid(src.limitRaw);
                valueIllegal = true;
                ImportErrorItem item;
                item.kind = "value-illegal";
                item.subject = "joint '" + name + "'.limit";
                item.detail = "limit 属性数值语法非法（原文保留: "
                              + src.limitRaw + "）";
                item.span = src.limitSpan;
                report.errors.push_back(std::move(item));
                report.jointStatuses.back().status = "invalid";
                report.jointStatuses.back().reason = "limit 数值非法";
            } else {
                // 缺 lower/upper（全部或其一）：待确认草稿项（§6.3——确认
                // 后可应用；bounds NotProvided 走降级）。
                entry.bounds = core::SourcedValue<JointLimits>::notProvided();
                ImportPendingItem pending;
                pending.kind = "limit-missing";
                pending.subject = "joint '" + name + "'";
                pending.question = src.hasLower || src.hasUpper
                                       ? "限位不完整（仅一侧）——请补全后确认"
                                       : "<limit> 缺失——请录入行程限位后确认";
                pending.span = src.hasLimit ? src.limitSpan : src.span;
                report.pendingConfirms.push_back(std::move(pending));
                pushImportDiag(diagEntries, diagSeq++, kMdlImportPendingConfirm,
                               jointTempId, name,
                               "item-kind=limit-missing subject=joint '" + name + "'",
                               "逐条确认——缺行程限位（MDL-IMPORT-PENDING-CONFIRM）",
                               pending.span);
            }
        } else if (type == 1) {
            // continuous：bounds NotApplicable（类型保留——I-MDL-4；工程
            // 工作范围确认随链型判定提交）。
            entry.bounds = core::SourcedValue<JointLimits>::notApplicable();
            ImportMappedItem mapped;
            mapped.sourcePath = "joint[" + std::to_string(ci) + "]";
            mapped.targetField = "joints[" + std::to_string(ci) + "].bounds";
            mapped.valueText = "not-applicable";
            mapped.note = "continuous 无限位——类型保留（§6.3；workingRange 确认"
                          "随链型判定提交）";
            mapped.span = src.span;
            report.mapped.push_back(std::move(mapped));
        } else if (src.hasLimit) {
            // fixed：无限位语义（出现即忽略面）。
            ImportIgnoredItem item;
            item.element = "joint '" + name + "'/limit";
            item.reason = "fixed 关节无限位语义——忽略（不解释）";
            item.span = src.limitSpan;
            report.ignored.push_back(std::move(item));
        }

        // —— effort→torqueLimit（drivetrain 对象 Peak 候选；传动对象不在
        //     导入草稿内创建，报告登记供回填流消费）。
        if (src.hasEffort && src.effortIllegal) {
            valueIllegal = true;
            ImportErrorItem item;
            item.kind = "value-illegal";
            item.subject = "joint '" + name + "'.limit@effort";
            item.detail = "effort 数值语法非法";
            item.span = src.limitSpan;
            report.errors.push_back(std::move(item));
        } else if (src.hasEffort && isMovableType(type)) {
            ImportDrivetrainItem item;
            item.jointName = name;
            item.valueText = formatDoubleStable(src.effort) + " N·m";
            item.span = src.limitSpan;
            report.drivetrainCandidates.push_back(std::move(item));
        }
        // —— velocity（无 schema 落点——不支持面报告，不静默丢弃）。
        if (src.hasVelocity) {
            ImportUnsupportedItem item;
            item.kind = "joint-velocity-limit";
            item.subject = "joint '" + name + "' velocity=" + src.velocityText;
            item.reason = "<limit velocity> 无 schema 落点（§4.3-A 未登记 maxVelocity"
                          "——不发明字段）——报告承载不静默丢弃";
            item.guidance = "速度上限可经传动设计/策略对象在后续编辑流登记";
            item.span = src.limitSpan;
            report.unsupported.push_back(std::move(item));
        }

        draft.joints.push_back(std::move(entry));
    }

    // —— 连杆逐个映射（链序；links[0]=根，links[ci+1]=joints[ci] 子连杆）。
    // 资源清单去重键＝依赖树相对键（同文件被多个 visual/collision 引用只
    // 登记一条 manifest——GeometryRef 以 resourceId 复用）。
    std::vector<ImportResourceItem> resourceRows;  // 资源状态表行（映射段收集，尾部并入）
    bool treeMismatch = false;                     // mesh 引用不在依赖树（装配违约面）
    std::string mismatchRef;                       // 首个不一致引用（定位）
    const auto mapAttachment = [&](const AttachmentSrc& att, const std::string& linkName,
                                   std::size_t linkIndex, const char* role,
                                   std::optional<GeometryRef>* target) {
        if (!att.present) { return; }
        // origin 非法：不产 GeometryRef（无半成品几何），错误项承载。
        if (att.hasOrigin && att.originIllegal) {
            valueIllegal = true;
            ImportErrorItem item;
            item.kind = "value-illegal";
            item.subject = "link '" + linkName + "'/" + role + "/origin";
            item.detail = "origin 数值非法（原文保留: " + att.originRaw + "）";
            item.span = att.span;
            report.errors.push_back(std::move(item));
            return;
        }
        if (!att.geometry.present) {
            ImportIgnoredItem item;
            item.element = std::string(role) + "（无 geometry）";
            item.reason = "<geometry> 缺失——无可映射几何，忽略（不解释）";
            item.span = att.span;
            report.ignored.push_back(std::move(item));
            return;
        }
        // 图元：§6.3 未登记映射语义——不支持面报告（不静默降级为mesh/空）。
        if (att.geometry.isPrimitive) {
            ImportUnsupportedItem item;
            item.kind = "geometry-primitive";
            item.subject = "link '" + linkName + "'/" + role + " <"
                           + att.geometry.primitiveKind + ">";
            item.reason = "图元几何无映射语义（§6.3 仅登记 mesh 行）——报告不静默丢弃";
            item.guidance = "可改用 mesh 引用，或待图元映射语义登记后重试";
            item.span = att.geometry.span;
            report.unsupported.push_back(std::move(item));
            return;
        }
        // 非单位缩放：GeometryRef 无 scale 落点（刚体变换之外）——不映射
        // 只报告（静默丢弃缩放会让碰撞/视觉语义漂移——NFR-COR-03）。
        bool unitScale = false;
        if (att.geometry.hasScale && !att.geometry.scaleIllegal) {
            unitScale = att.geometry.scale[0] == 1.0 && att.geometry.scale[1] == 1.0
                        && att.geometry.scale[2] == 1.0;
        }
        if (att.geometry.hasScale && (att.geometry.scaleIllegal || !unitScale)) {
            ImportUnsupportedItem item;
            item.kind = "mesh-scale";
            item.subject = "link '" + linkName + "'/" + role + " scale="
                           + att.geometry.scaleText;
            item.reason = "mesh 缩放无 schema 落点（GeometryRef 仅承载刚体位姿）——"
                          "不映射只报告（静默丢弃会漂移几何语义）";
            item.guidance = "在网格资产侧应用缩放后以单位缩放引用";
            item.span = att.geometry.meshSpan;
            report.unsupported.push_back(std::move(item));
            return;
        }
        if (!att.geometry.meshNamePresent || att.geometry.meshFilename.empty()) {
            ImportIgnoredItem item;
            item.element = std::string(role) + "/mesh";
            item.reason = "mesh filename 属性缺失——无可映射引用，忽略（不解释）";
            item.span = att.geometry.meshSpan;
            report.ignored.push_back(std::move(item));
            return;
        }
        // ROS URI（package:// 等）：不支持→定位诊断引导改相对路径（§6.3；
        // 定位面＝报告条目 span——§9.5 无对应码行，不私定码；不置阻断——
        // 几何缺位可编辑补全，与待确认项同级的非阻断面）。
        if (att.geometry.meshFilename.find("://") != std::string::npos) {
            ImportUnsupportedItem item;
            item.kind = "ros-uri";
            item.subject = "link '" + linkName + "'/" + role + " filename="
                           + att.geometry.meshFilename;
            item.reason = "package:// 等 ROS URI 不支持（io R1 口径——相对导入根解析）";
            item.guidance = "改相对路径（相对 URDF 所在目录）后重试";
            item.span = att.geometry.meshSpan;
            report.unsupported.push_back(std::move(item));
            return;
        }
        // 相对引用→依赖树节点（io relPath 约定：正斜杠＋折叠小写）。
        std::string relKey;
        relKey.reserve(att.geometry.meshFilename.size());
        for (const char ch : att.geometry.meshFilename) {
            relKey.push_back(ch == '\\' ? '/' : static_cast<char>(std::tolower(
                static_cast<unsigned char>(ch))));
        }
        const io::ResourceNode* node = nullptr;
        for (const io::ResourceNode& n : source.dependencyTree.nodes) {
            if (n.relPath == relKey) {
                node = &n;
                break;
            }
        }
        if (node == nullptr) {
            // 引用不在依赖树＝bytes 与树非同一 io 产物（装配契约违约）。
            treeMismatch = true;
            mismatchRef = relKey;
            return;
        }
        // 资源清单条目（同键去重）：Recorded 态必带 externalRecord（I-MDL-10）。
        // 缺失叶：digest 无从取得（全零＝空保留值），absPath 以相对键登记
        // （无绝对路径事实——io probe 届时报 Missing；§6.7"缺失≠不可行"，
        // V-08 应用可过（Warning））。
        const bool missing = !node->exists;
        ResourceRef ref;
        ref.resourceId = relKey;
        ref.state = ResourceState::Recorded;
        ExternalResourceRecord record;
        record.absPath = missing
                             ? relKey
                             : node->snapshot->finalPath.string();
        if (!missing) { ref.contentDigest = node->snapshot->contentDigest; }
        record.recordedDigest =
            missing ? core::Digest256{} : node->snapshot->contentDigest;
        ref.externalRecord = record;
        bool duplicated = false;
        for (const ResourceRef& existing : draft.resourceManifest) {
            if (existing.resourceId == relKey) { duplicated = true; break; }
        }
        if (!duplicated) { draft.resourceManifest.push_back(std::move(ref)); }

        // 资源状态表行（缺失事实一次一行——同键多引用仍只一行）。
        bool rowDuplicated = false;
        for (const ImportResourceItem& row : resourceRows) {
            if (row.resourceId == relKey) { rowDuplicated = true; break; }
        }
        if (!rowDuplicated) {
            ImportResourceItem row;
            row.resourceId = relKey;
            row.relPath = relKey;
            row.state = missing ? "missing" : "recorded";
            row.digestHex = missing ? std::string() : digestToHex(node->snapshot->contentDigest);
            row.span = att.geometry.meshSpan;
            resourceRows.push_back(std::move(row));
        }
        if (missing) {
            // IO-RES-MISSING 事实诊断（码面＝io 注册码——io::errorCodeToken
            // 唯一来源；缺失事实经资源状态表＋本诊断承载，**不置阻断**——
            // V-08"应用可过（Warning）"：就绪校验 L6 呈现，固化归 CON-03）。
            DiagEntry diag;
            diag.record = core::DiagnosticRecord::make(
                std::string(io::errorCodeToken(io::IoErrorCode::ResMissing)),
                deriveTempObjectId("link", linkIndex, linkName), linkName, std::nullopt,
                "urdf-import@" + att.geometry.meshSpan.relFile + ":"
                    + std::to_string(att.geometry.meshSpan.line) + ":"
                    + std::to_string(att.geometry.meshSpan.column),
                "path=" + relKey + "（依赖树缺失叶——资源事实，§2.5 正交）",
                "重关联文件或固化资源——草稿可保存（就绪校验 L6 呈现）");
            diag.relFile = att.geometry.meshSpan.relFile;
            diag.line = att.geometry.meshSpan.line;
            diag.column = att.geometry.meshSpan.column;
            diag.seq = diagSeq++;
            diagEntries.push_back(std::move(diag));
        }
        // GeometryRef（origin 合法性已在前置分支保证；localTransform＝
        // T_link_geom，m/rad，core §4.6 约定）。
        GeometryRef geometry;
        geometry.resourceRefId = relKey;
        geometry.kind = GeometryKind::Mesh;
        if (att.hasOrigin) {
            geometry.localTransform = rw::math::Transform3D<double>(
                rw::math::Vector3D<double>(att.ox, att.oy, att.oz),
                rpyToRotation(att.roll, att.pitch, att.yaw));
        }
        *target = std::move(geometry);
        ImportMappedItem mapped;
        mapped.sourcePath = "link '" + linkName + "'/" + role + "/geometry/mesh";
        mapped.targetField = std::string("links[") + std::to_string(linkIndex) + "]."
                             + role + " → resourceManifest[" + relKey + "]";
        mapped.valueText = missing ? relKey + "（缺失——Recorded 草稿可携带）" : relKey;
        mapped.note = "§6.3 mesh 行；身份＝内容摘要（路径不作身份，§4.8）";
        mapped.span = att.geometry.meshSpan;
        report.mapped.push_back(std::move(mapped));
    };

    for (std::size_t ci = 0; ci < chainLinks.size() && !treeMismatch; ++ci) {
        const std::size_t l = chainLinks[ci];
        const LinkSrc& src = scan.links[l];
        const std::string& name = linkNames[l];
        LinkEntry link;
        link.objectId = deriveTempObjectId("link", ci, name);  // 确定性临时句柄（§6.7）
        link.localName = name;

        // —— inertial（§6.3：缺→NotProvided 降级；已提供但 m≤0/非 SPD→
        //     错误项；非法→invalid 态保留原串）。
        if (!src.inertial.present) {
            ImportDefaultItem item;
            item.field = "links[" + std::to_string(ci) + "].body";
            item.appliedValue = "not-provided（mass/com/inertia 全缺省）";
            item.reason = "<inertial> 缺失——NotProvided 走降级路径（§6.3/DYN-06 闭环）";
            item.span = src.span;
            report.defaults.push_back(std::move(item));
        } else {
            if (src.inertial.hasMass) {
                link.body.mass = core::SourcedValue<double>::provided(
                    src.inertial.mass, importProvenance("urdf-import"));
                if (!(src.inertial.mass > 0.0)) {
                    // m≤0：Provided 保留原值＋错误项（应用将被断言①阻断——
                    // §6.3 原文语义；导入期预提示面）。
                    valueIllegal = true;
                    ImportErrorItem item;
                    item.kind = "mass-nonpositive";
                    item.subject = "links[" + std::to_string(ci) + "].body.mass";
                    item.detail = "质量 m≤0（" + formatDoubleStable(src.inertial.mass)
                                  + " kg）——应用将被断言阻断";
                    item.span = src.inertial.massSpan;
                    report.errors.push_back(std::move(item));
                }
                ImportMappedItem mapped;
                mapped.sourcePath = "link '" + name + "'/inertial/mass";
                mapped.targetField = "links[" + std::to_string(ci) + "].body.mass";
                mapped.valueText = formatDoubleStable(src.inertial.mass) + " kg";
                mapped.note = "来源 import-mapped";
                mapped.span = src.inertial.massSpan;
                report.mapped.push_back(std::move(mapped));
            } else {
                ImportDefaultItem item;
                item.field = "links[" + std::to_string(ci) + "].body.mass";
                item.appliedValue = src.inertial.massIllegal
                                        ? "invalid（原串保留）"
                                        : "not-provided";
                item.reason = src.inertial.massIllegal
                                  ? "<mass value> 语法非法——invalid 态保留原串"
                                    "（NFR-COR-03）"
                                  : "<mass> 缺失——NotProvided 降级";
                item.span = src.inertial.span;
                report.defaults.push_back(std::move(item));
                if (src.inertial.massIllegal) {
                    valueIllegal = true;
                    link.body.mass = core::SourcedValue<double>::invalid(
                        src.inertial.massRaw);
                    ImportErrorItem item2;
                    item2.kind = "value-illegal";
                    item2.subject = "links[" + std::to_string(ci) + "].body.mass";
                    item2.detail = "<mass value> 缺失或语法非法（原文保留: "
                                   + src.inertial.massRaw + "）";
                    item2.span = src.inertial.massSpan;
                    report.errors.push_back(std::move(item2));
                }
            }
            // 质心（inertial origin 平移部分——m，连杆系）。
            if (src.inertial.hasOrigin && !src.inertial.originIllegal) {
                link.body.centerOfMass =
                    core::SourcedValue<rw::math::Vector3D<double>>::provided(
                        rw::math::Vector3D<double>(src.inertial.ox, src.inertial.oy,
                                                   src.inertial.oz),
                        importProvenance("urdf-import"));
            } else if (src.inertial.originIllegal) {
                valueIllegal = true;
                link.body.centerOfMass =
                    core::SourcedValue<rw::math::Vector3D<double>>::invalid(
                        src.inertial.originRaw);
                ImportErrorItem item;
                item.kind = "value-illegal";
                item.subject = "links[" + std::to_string(ci) + "].body.centerOfMass";
                item.detail = "inertial origin 数值非法（原文保留: "
                              + src.inertial.originRaw + "）";
                item.span = src.inertial.originSpan;
                report.errors.push_back(std::move(item));
            }
            // 惯量（inertial 系给定——rpy≠0 时旋到连杆系；M-2 基准不变）。
            if (src.inertial.hasInertia) {
                const bool hasRotation = src.inertial.roll != 0.0
                                         || src.inertial.pitch != 0.0
                                         || src.inertial.yaw != 0.0;
                const InertiaTensor tensor =
                    hasRotation
                        ? rotateInertia(src.inertial.inertia,
                                        rpyToRotation(src.inertial.roll,
                                                      src.inertial.pitch,
                                                      src.inertial.yaw))
                        : src.inertial.inertia;
                link.body.inertia = core::SourcedValue<InertiaTensor>::provided(
                    tensor, importProvenance("urdf-import"));
                // I-MDL-5 断言②③（SPD＋三角不等式——InertiaMath 单一实现）
                // 违例→错误项＋不可提交（应用边界断言兜底的导入期预提示）。
                std::vector<InvariantViolation> violations;
                inertiamath::checkInertiaAssertions(violations, link.body.inertia,
                                                    "links[" + std::to_string(ci) + "]");
                for (const InvariantViolation& v : violations) {
                    valueIllegal = true;
                    ImportErrorItem item;
                    item.kind = v.subject.find(".spd") != std::string::npos
                                    ? "inertia-not-spd"
                                    : "inertia-triangle";
                    item.subject = v.subject;
                    item.detail = "惯量断言违例（I-MDL-5）——应用将被断言阻断";
                    item.span = src.inertial.inertiaSpan;
                    report.errors.push_back(std::move(item));
                }
            } else if (src.inertial.inertiaIllegal) {
                valueIllegal = true;
                link.body.inertia = core::SourcedValue<InertiaTensor>::invalid(
                    src.inertial.inertiaRaw);
                ImportErrorItem item;
                item.kind = "value-illegal";
                item.subject = "links[" + std::to_string(ci) + "].body.inertia";
                item.detail = "<inertia> 分量缺失或语法非法（原文保留: "
                              + src.inertial.inertiaRaw + "）";
                item.span = src.inertial.inertiaSpan;
                report.errors.push_back(std::move(item));
            }
        }

        // —— visual/collision → resourceManifest ＋ GeometryRef（§6.3）。
        if (src.visualCount > 1) {
            ImportUnsupportedItem item;
            item.kind = "multiple-visual";
            item.subject = "link '" + name + "'";
            item.reason = "多个 <visual>——schema 单引用，仅首个映射，其余逐条报告";
            item.guidance = "其余视觉几何可在导入后经编辑流登记";
            item.span = src.span;
            report.unsupported.push_back(std::move(item));
        }
        mapAttachment(src.visual, name, ci, "visual", &link.visual);
        if (src.collisionCount > 1) {
            ImportUnsupportedItem item;
            item.kind = "multiple-collision";
            item.subject = "link '" + name + "'";
            item.reason = "多个 <collision>——schema 单引用，仅首个映射，其余逐条报告";
            item.guidance = "碰撞几何判定归 policy（§4.3-B）——合并需求经编辑流处置";
            item.span = src.span;
            report.unsupported.push_back(std::move(item));
        }
        mapAttachment(src.collision, name, ci, "collision", &link.collision);

        // —— material（§6.3：估算密度提示——URDF 材质无密度→NotProvided
        //     报告登记；映射源＝首个带材质的 visual，文档序确定性）。
        if (src.visual.present && src.visual.materialPresent) {
            MaterialRef material;
            material.materialId = src.visual.materialName;  // 模型内键（原文——非 localName 面不净化）
            material.density = core::SourcedValue<double>::notProvided();
            link.body.material = std::move(material);
            ImportMappedItem mapped;
            mapped.sourcePath = "link '" + name + "'/visual/material";
            mapped.targetField = "links[" + std::to_string(ci) + "].body.material";
            mapped.valueText = "materialId=" + src.visual.materialName
                               + " density=not-provided";
            mapped.note = "URDF 材质无密度——估算不可用，报告登记（§6.3；"
                          "估算输入可后续经密度默认表补全——T04）";
            mapped.span = src.visual.span;
            report.mapped.push_back(std::move(mapped));
        }

        draft.links.push_back(std::move(link));
    }
    if (treeMismatch) {
        // mesh 引用不在依赖树＝bytes 与树非同一 io 产物（ValidatedSource
        // 装配契约违约）——无草稿产出（无半成品；调用方修正装配后重试）。
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("stage", "resource-tree");
        err.params.emplace_back("reference", mismatchRef);
        err.detail = "mesh 引用不在依赖树——bytes 与 dependencyTree 非同一 io 产物";
        outcome.error = std::move(err);
        report.submittable = false;
        return finalizeFailure(diagEntries, diags, std::move(outcome));
    }
    report.resources = std::move(resourceRows);

    // -----------------------------------------------------------------
    // 第九步：阻断面汇总＋出口（诊断稳定排序）。
    // outcome.error＝§9.4.3 @错误 行具名阻断条件中**首个**命中者（多阻断
    // 面经报告清单全量可观察）；submittable=false 亦可能仅由 errors 清单
    // 驱动（无具名条件）——两面的判定表见 ImportOutcome 类型注。
    // -----------------------------------------------------------------
    if (axisBlocked) {
        ImportError err;
        err.code = ImportErrorCode::ZeroAxisReported;
        err.params.emplace_back("remedy", "correct-source");
        err.detail = "存在零轴/非有限轴关节——仅报告，含该类关节的草稿不得提交"
                     "修订（MDL-11；prepare 断言兜底）";
        outcome.error = std::move(err);
        report.submittable = false;
    } else if (mimicBlocked) {
        ImportError err;
        err.code = ImportErrorCode::MimicBlocked;
        err.params.emplace_back("remedy", "remove-mimic-or-wait-r2");
        err.detail = "存在 mimic 关节——类型保留不转 FixedFrame，草稿不可提交"
                     "（MDL-12/V-10）";
        outcome.error = std::move(err);
        report.submittable = false;
    }
    if (valueIllegal) {
        report.submittable = false;  // 非法数值/物理违例面（错误项清单承载定位）
    }

    // 诊断稳定排序出口（成功路径——排序与失败出口同一实现，见 flushDiags）。
    flushDiags(diagEntries, diags);

    outcome.draft = std::move(draft);
    return outcome;
}

ImportOutcome ModelImportMapper::mapXacroExpanded(const ValidatedSource& expanded,
                                                  const XacroProvenance& provenance,
                                                  const ImportOptions& options,
                                                  std::vector<core::DiagnosticRecord>& diags) const
{
    // 来源契约校验（调用方违约→值面 SourceInconsistent——无草稿）：
    // provenance 必须携带非零来源摘要与路径文本（§6.5 来源记录语义；
    // T06 展开服务的产出契约面——P-MDL-8 消费侧先行定义）。
    bool digestZero = true;
    for (const std::uint8_t b : provenance.sourceDigest) {
        if (b != 0) { digestZero = false; break; }
    }
    if (digestZero || provenance.sourceAbsPath.empty()) {
        ImportOutcome outcome;
        outcome.report.sourceLabel = expanded.dependencyTree.rootRel;
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("stage", "xacro-provenance");
        err.detail = "XacroProvenance 缺来源摘要/路径——展开服务（T06）产出契约违约";
        outcome.error = std::move(err);
        return outcome;
    }

    // 与 mapUrdf 完全同一边界（§6.5"展开产物进入与 URDF 相同的映射与安全
    // 边界"）——先跑同一映射，再叠加来源留痕面。
    ImportOutcome outcome = mapUrdf(expanded, options, diags);
    if (!outcome.draft.has_value()) {
        return outcome;  // 映射失败——报告已含失败面，来源留痕无从附着
    }

    // 来源留痕 1：原始 .xacro 以 Recorded 资源进草稿 resourceManifest
    // （§6.5"原始 .xacro 的 ResourceSnapshot digest 进草稿 externalRefs"；
    // §6.7 Recorded 态必带 externalRecord）。resourceId 固定键（与几何
    // 资源的相对键空间以 "xacro-source" 隔离——不含路径分隔符）。
    RobotDesign& draft = *outcome.draft;
    ResourceRef sourceRef;
    sourceRef.resourceId = "xacro-source";
    sourceRef.contentDigest = provenance.sourceDigest;
    sourceRef.state = ResourceState::Recorded;
    ExternalResourceRecord record;
    record.absPath = provenance.sourceAbsPath;
    record.recordedDigest = provenance.sourceDigest;
    sourceRef.externalRecord = record;
    draft.resourceManifest.push_back(std::move(sourceRef));

    // 来源留痕 2：报告资源状态表＋逐条替换（构造序——确定性）。
    ImportResourceItem row;
    row.resourceId = "xacro-source";
    row.relPath = provenance.sourceAbsPath;
    row.state = "recorded";
    row.digestHex = digestToHex(provenance.sourceDigest);
    row.span = ImportSourceSpan{outcome.report.sourceLabel, 0, 0};
    outcome.report.resources.push_back(std::move(row));
    for (const auto& sub : provenance.substitutions) {
        ImportMappedItem mapped;
        mapped.sourcePath = "xacro-substitution/" + sub.first;
        mapped.targetField = "(留痕——不入权威字段)";
        mapped.valueText = sub.second;
        mapped.note = "Xacro 展开环境逐条替换留痕（§6.5——随导入报告）";
        mapped.span = ImportSourceSpan{outcome.report.sourceLabel, 0, 0};
        outcome.report.mapped.push_back(std::move(mapped));
    }
    return outcome;
}

ImportOutcome ModelImportMapper::mapWorkCellXml(const ValidatedSource& source,
                                                const ImportOptions& options,
                                                std::vector<core::DiagnosticRecord>& diags) const
{
    (void)options;
    (void)diags;  // R1 无已注册码面（§9.5 无对应行）——引导经报告条目承载
    // MDL-18 R2 边界（§6.6"R1 不实现、不留桩代码"）：稳定错误＋通道引导，
    // 不产草稿；阶段 D 启用时由 WP-13-T17 领取实现（有损提取＋提取报告，
    // 不可表达内容按 MDL-10/12 同一判定规则处置）。
    ImportOutcome outcome;
    outcome.report.sourceLabel = source.dependencyTree.rootRel;
    ImportUnsupportedItem item;
    item.kind = "workcell-channel-r2";
    item.subject = "mapWorkCellXml";
    item.reason = "WorkCell 反向导入为 R2/阶段 D 通道（MDL-18）——R1 不实现、不留桩";
    item.guidance = "请使用 URDF/Xacro 导入通道，或等待阶段 D 启用";
    item.span = ImportSourceSpan{source.dependencyTree.rootRel, 0, 0};
    outcome.report.unsupported.push_back(std::move(item));
    ImportError err;
    err.code = ImportErrorCode::NotImplemented;
    err.params.emplace_back("channel", "mapWorkCellXml");
    err.params.emplace_back("stage", "R2");
    err.detail = "WorkCell 反向导入（有损提取）属 R2/阶段 D——WP-13-T17";
    outcome.error = std::move(err);
    return outcome;
}

}  // namespace sdurws::ird::modeling
