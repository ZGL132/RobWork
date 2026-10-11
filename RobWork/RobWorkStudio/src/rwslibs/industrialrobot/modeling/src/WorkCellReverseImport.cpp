/**
 * @file   WorkCellReverseImport.cpp
 * @brief  WorkCell 反向导入提取引擎的实现——pugixml DOM 解析（O-40 同款
 *         消费面）＋§6.6 提取语义逐条落位：目标设备选择（§6.4 同机制）、
 *         关节类型/限位/位姿/轴/Q 构型提取、DHJoint 全旋转同质链经
 *         dhToExplicit 展开（T09 单一实现复用）、几何/资源 Recorded 登记、
 *         碰撞设置/场景/备选构型等不可表达与未保留内容逐项列出（AT-33）。
 *
 * 设计依据：units/modeling.md §6.6/§6.4/§6.7/§7.4/§9.4.3/§9.5；units/io.md
 * §9.6 v0.10（GenericXml 通道冻结读取面）；需求 MDL-18/AT-33/MDL-03/10/11/
 * 12/22、NFR-COR-01/02/03、CON-03、PA-1；任务契约 WP-13-T17。
 *
 * 实现结构（阅读地图）：
 *   匿名命名空间：诊断条目与稳定排序出口/名称净化/确定性临时句柄/摘要
 *   十六进制/相对键折叠/locale 无关数值解析/源行索引（诊断定位键）/
 *   <Pos>/<RPY> 子元素解析（deg→rad 换算点显式标注）/DHJoint 属性读取
 *   /几何模型解析（Drawable/CollisionModel→Polytope 引用＋图元面）。
 *   主体 extractWorkCellDevice 七步：①DOM 解析＋根检查 ②根下元素分类
 *   扫描（设备候选/场景项/碰撞设置/忽略面——文档序）③目标设备选择
 *   （分支报告＋显式选链——§6.4 同机制）④设备内链扫描＋类型面判定
 *   （Joint/Frame/DHJoint——不可表达无草稿；Depend 从动 mimic 同款阻断）
 *   ⑤草稿构造（普通链逐关节直映；纯 DH 旋转同质链经 dhToExplicit 展开）
 *   ⑥几何/资源 Recorded 登记＋Q 构型→zeroOffset ⑦能力结论＋阻断面汇总
 *   ＋诊断稳定排序出口。报告收尾面（场景/碰撞/多链设备逐项登记）封装为
 *   收尾 lambda——成功/失败出口共用（提取报告是无条件产出面，AT-33）。
 *
 * 确定性（NFR-COR-01/02）：全部数值路径 locale 无关；遍历＝文档序；诊断
 * 按（相对键，行，列，产出序）字典序稳定排序；同输入字节＋同 options→
 * 逐字段相等输出。线程安全：无共享可变状态。
 */

#include <sdurws/ird/modeling/WorkCellReverseImport.hpp>

#include <sdurws/ird/core/Provenance.hpp>    // core::ValueProvenance/ProvenanceKind::ImportMapped——来源标记
#include <sdurws/ird/io/IoDiagnostics.hpp>   // io::errorCodeToken——IO-RES-MISSING 码面唯一来源
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>  // MDL-IMPORT-* 码值常量（唯一书写点）
#include <sdurws/ird/modeling/DhConvert.hpp>  // DhExplicitConverter——DH→显式展开单一实现（T09；§7.4 拆分装配/零位对齐纪律同源）

#include "RpyMath.hpp"  // 单元私有头（R-2）——RPY 正解唯一实现（UI-T53；WorkCell RPY 同一固定轴约定 R=Rz·Ry·Rx）

#include <pugixml.hpp>  // O-40：DOM 解析（vcpkg 经典模式 1.16/x64-windows，modeling PRIVATE）。
                        // 有界性由 io BudgetGuard 前置保证：进入本单元的字节已经 io 预算
                        // 入账，DOM 规模与输入字节同阶；pugixml 默认不解析外部实体
                        // （无 XXE/二次读文件通道——SA-14）。

#include <algorithm>
#include <cctype>
#include <charconv>      // std::from_chars/to_chars——locale 无关数值转换（NFR-COR-02）
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::modeling {

namespace {

// =====================================================================
// 诊断结构（与 Import.cpp 同款形态——WorkCell 通道自持，不动既有 TU；
// 码面唯一来源＝DiagCodes.hpp 常量/io::errorCodeToken——禁字符串拼码）
// =====================================================================

/// 诊断中间条目（DiagnosticRecord＋稳定排序键——（相对键，行，列，产出序））。
struct WcDiagEntry {
    core::DiagnosticRecord record;  ///< 已构造诊断记录（make 工厂产出——C-3 校验）
    std::string relFile;            ///< 排序键：源相对键（空＝无定位，排最前）
    std::uint32_t line = 0;         ///< 排序键：行（0＝无定位）
    std::uint32_t column = 0;       ///< 排序键：列（0＝无定位）
    std::size_t seq = 0;            ///< 排序键：同位置产出序（稳定化兜底）
};

/// 追加一条 MDL-IMPORT-* 诊断（subject 携带草稿临时句柄；context 以
/// "workcell-import@relFile:line:col" 标记通道来源——与 urdf-import 区分）。
void pushWcDiag(std::vector<WcDiagEntry>& entries, std::size_t seq,
                std::string_view code, const core::ObjectId& subject,
                const std::string& localName, std::string cause,
                std::string action, const ImportSourceSpan& span)
{
    WcDiagEntry entry;
    entry.record = core::DiagnosticRecord::make(
        std::string(code), subject, localName, std::nullopt,
        "workcell-import@" + span.relFile + ":" + std::to_string(span.line) + ":"
            + std::to_string(span.column),
        std::move(cause), std::move(action));
    entry.relFile = span.relFile;
    entry.line = span.line;
    entry.column = span.column;
    entry.seq = seq;
    entries.push_back(std::move(entry));
}

/// 诊断稳定排序出口（追加不清空——输出参数契约；成功/失败出口共用）。
void flushWcDiags(std::vector<WcDiagEntry>& entries,
                  std::vector<core::DiagnosticRecord>& diags)
{
    std::sort(entries.begin(), entries.end(),
              [](const WcDiagEntry& a, const WcDiagEntry& b) {
                  if (a.relFile != b.relFile) { return a.relFile < b.relFile; }
                  if (a.line != b.line) { return a.line < b.line; }
                  if (a.column != b.column) { return a.column < b.column; }
                  return a.seq < b.seq;
              });
    for (const WcDiagEntry& entry : entries) {
        diags.push_back(entry.record);
    }
}

// =====================================================================
// locale 无关数值解析（与 Import.cpp 同源小工具——WorkCell 通道自持；
// NFR-COR-01/02：不经 locale，同文本同值）
// =====================================================================

/// 解析十进制浮点文本（from_chars——"nan"/"inf" 亦被接受，有限性由调用方判定）。
bool parseDoubleStable(std::string_view text, double* out)
{
    std::size_t b = 0;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) { ++b; }
    std::size_t e = text.size();
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t')) { --e; }
    if (b >= e) { return false; }
    const auto result = std::from_chars(text.data() + b, text.data() + e, *out);
    return result.ec == std::errc() && result.ptr == text.data() + e;
}

/// 浮点→稳定文本（to_chars 最短往返——报告 valueText 唯一格式化点；
/// 位级同值必产出同串，往返解析还原位级同值）。
std::string formatDoubleStable(double v)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, result.ptr);
}

/// 空白分隔实数序列（Pos/RPY 文本——恰 n 段且全部解析成功）。
bool parseRealSequence(std::string_view text, std::size_t n, double* out)
{
    const auto isSpace = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    std::size_t i = 0;
    for (std::size_t k = 0; k < n; ++k) {
        while (i < text.size() && isSpace(text[i])) { ++i; }
        if (i >= text.size()) { return false; }
        const std::size_t start = i;
        while (i < text.size() && !isSpace(text[i])) { ++i; }
        if (!parseDoubleStable(text.substr(start, i - start), &out[k])) { return false; }
    }
    while (i < text.size() && isSpace(text[i])) { ++i; }
    return i == text.size();
}

// =====================================================================
// 源行索引（pugixml offset_debug 字节偏移→行列——诊断稳定排序键；
// 与 Import.cpp LineIndex 同款语义，WorkCell 通道自持）
// =====================================================================

class WcLineIndex {
public:
    explicit WcLineIndex(std::string_view text)
    {
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') { lineStarts_.push_back(i + 1); }
        }
    }

    /// 字节偏移→(行, 列)（均 1 起；列按字节计——UTF-8 多字节字符的"列"
    /// 为字节列，定位取舍稳定性优先）。
    void locate(std::size_t offset, std::uint32_t* line, std::uint32_t* column) const
    {
        // 二分找最后一个 ≤offset 的行起点（偏移越界按文本尾——防御）。
        std::size_t lo = 0;
        std::size_t hi = lineStarts_.size();
        while (lo + 1 < hi) {
            const std::size_t mid = (lo + hi) / 2;
            if (lineStarts_[mid] <= offset) { lo = mid; } else { hi = mid; }
        }
        *line = static_cast<std::uint32_t>(lo + 1);
        *column = static_cast<std::uint32_t>(offset - lineStarts_[lo] + 1);
    }

private:
    std::vector<std::size_t> lineStarts_;   ///< 各行首字节偏移（0 起）
};

/// pugixml 节点→源位置（offset_debug 字节偏移经行索引换算）。
ImportSourceSpan spanOf(const pugi::xml_node& node, const WcLineIndex& index,
                        const std::string& relFile)
{
    ImportSourceSpan span;
    span.relFile = relFile;
    index.locate(static_cast<std::size_t>(node.offset_debug()), &span.line, &span.column);
    return span;
}

// =====================================================================
// 名称净化/确定性临时句柄/摘要十六进制/相对键折叠（§6.3/§6.7——与
// Import.cpp 同款语义，WorkCell 通道自持）
// =====================================================================

/// localName 合法字符集净化（[A-Za-z0-9_.-] 之外的字节→'_'；最终消歧归
/// runtime，R-4——变化入默认补全清单，不静默）。
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

/// 由（类别，序号，localName）派生确定性临时 ObjectId（§6.7"tmp-<序号>
/// 不入库"——真实身份在命令 prepare 阶段分配，PA-1；双 FNV-1a 64 拼合
/// 128 位——与 Import.cpp 同款算法，同输入同句柄，生成不经随机源）。
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

/// 摘要十六进制（资源状态表呈现——小写；仅编码不哈希，SA-12 不涉）。
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

/// 相对键折叠（io relPath 约定：正斜杠＋ASCII 小写——依赖树查重键）。
std::string foldRelKey(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (const char ch : raw) {
        out.push_back(ch == '\\' ? '/' : static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

// =====================================================================
// WorkCell XML 结构扫描值（中间承载——全部相对源文档定位）
// =====================================================================

/// 关节/帧/DHJoint 元素（设备内链元素统一承载——elementKind 区分形态）。
struct WcLinkElement {
    pugi::xml_node node;        ///< 元素节点（Joint | Frame | DHJoint）
    std::string elementKind;    ///< "Joint"/"Frame"/"DHJoint"
    std::string rawName;        ///< name 属性原文
    std::string typeText;       ///< type 形态文本（Frame 缺省 "Fixed"——框架文法）
    ImportSourceSpan span{};    ///< 元素定位
};

/// 设备候选（SerialDevice——可提取单串联链）。
struct WcDevice {
    pugi::xml_node node;        ///< SerialDevice 元素
    std::string rawName;        ///< name 属性原文
    ImportSourceSpan span{};    ///< 元素定位
};

/// 几何引用候选（Drawable/CollisionModel 内的 <Polytope file>）。
struct WcGeometryRef {
    std::string fileText;       ///< file 属性原文（相对路径——依赖树相对键解析）
    ImportSourceSpan span{};    ///< Polytope 定位
};

/// 几何模型（挂链元素的 Drawable/CollisionModel——visual/collision 候选）。
struct WcModel {
    std::string elementKind;    ///< "Drawable"/"CollisionModel"
    bool hasTransform = false;  ///< 是否携带 <Pos>/<RPY>
    double px = 0.0, py = 0.0, pz = 0.0;          ///< 平移（m；模型系→宿主帧系）
    double roll = 0.0, pitch = 0.0, yaw = 0.0;    ///< 固定轴姿态（rad——源 deg 已换算）
    bool transformDegenerate = false;             ///< Pos/RPY 解析失败（非法值面）
    std::vector<WcGeometryRef> polytopes;         ///< Polytope 引用（文档序）
    bool hasPrimitive = false;                    ///< 出现解析图元（报告面）
    std::string primitiveKind;                    ///< 首个图元元素名（定位描述）
    ImportSourceSpan span{};                      ///< 模型元素定位
};

/// Q 命名构型（设备内 <Q name>——首配置提取 zeroOffset，备选未保留）。
struct WcQConfig {
    std::string name;           ///< name 属性原文（构型名）
    std::vector<double> q;      ///< 构型值序列（rad——框架 rw Q 域）
    ImportSourceSpan span{};    ///< 元素定位
};

/// WorkCell 文档级扫描结果（②步产物）。
struct WcScan {
    std::string wcName;                 ///< WorkCell name 属性原文
    bool wcNamePresent = false;
    ImportSourceSpan rootSpan{};        ///< 根元素定位（文档级条目共用）
    std::vector<WcDevice> devices;      ///< SerialDevice 候选（文档序）
    /// 非单串联设备（ParallelDevice/TreeDevice/MobileDevice）——不可表达面。
    std::vector<std::pair<pugi::xml_node, std::string>> otherDevices;
    /// WorkCell 级场景项（Frame/Drawable/CollisionModel）——未保留面。
    std::vector<std::pair<pugi::xml_node, std::string>> sceneItems;
    /// WorkCell 级碰撞/邻近设置引用——未保留面。
    std::vector<std::pair<pugi::xml_node, std::string>> collisionSetups;
};

/// 设备内扫描结果（④步产物——链元素＋构型＋设备级面）。
struct WcDeviceScan {
    std::vector<WcLinkElement> chain;   ///< 链元素（Joint/Frame/DHJoint——文档序＝链序）
    std::vector<WcQConfig> qConfigs;    ///< 设备内 Q 构型（文档序）
    /// 设备级碰撞/邻近设置引用——未保留面。
    std::vector<std::pair<pugi::xml_node, std::string>> collisionSetups;
    bool hasChainElement = false;       ///< 链非空（无关节设备＝不可提取面）
    bool hasDhJoint = false;            ///< 链含 DHJoint（展开路径判别）
    bool allDhRevolute = true;          ///< 链全部为 Revolute 型 DHJoint（展开前提——
                                        ///<  遇 Joint/Frame 或非 Revolute DHJoint 即假）
};

// =====================================================================
// 解析助手：<Pos>/<RPY> 子元素、DHJoint 属性与几何模型
// =====================================================================

/// deg→rad 换算系数（π/180——WorkCell XML 角度制式为 deg：框架
/// XMLRWLoader 以 Deg2Rad 换算加载，本引擎同口径；换算点注释显式标注，
/// 报告与草稿一律 rad——AGENTS §2.5 制式显式纪律）。
inline constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// <Pos>/<RPY> 子元素解析结果（框架 transform3d_p 文法：Pos/RPY 各至多
/// 一元素；二者均可省略——缺省零位姿）。
struct WcPosRpy {
    bool hasPos = false;        ///< <Pos> 出现
    bool posOk = true;          ///< Pos 文本解析成功（false＝非法值面）
    double px = 0.0, py = 0.0, pz = 0.0;          ///< 平移（m）
    bool hasRpy = false;        ///< <RPY> 出现
    bool rpyOk = true;          ///< RPY 文本解析成功
    double roll = 0.0, pitch = 0.0, yaw = 0.0;    ///< 固定轴姿态（rad——源 deg 已换算）
};

/// 从元素直下子元素解析 <Pos>（m）与 <RPY>（源 deg→rad）。
WcPosRpy parsePosRpy(const pugi::xml_node& element)
{
    WcPosRpy out;
    double buf[3] = {0.0, 0.0, 0.0};
    for (const pugi::xml_node& child : element.children()) {
        const std::string name = child.name();
        if (name == "Pos" && !out.hasPos) {
            out.hasPos = true;
            // Pos 文本＝三实数（单位 m；框架 pos_r 文法）。
            out.posOk = parseRealSequence(child.text().as_string(""), 3, buf);
            if (out.posOk) {
                out.px = buf[0];
                out.py = buf[1];
                out.pz = buf[2];
            }
        } else if (name == "RPY" && !out.hasRpy) {
            out.hasRpy = true;
            // RPY 文本＝三实数（源单位 deg——框架 rpy_r 文法按 Deg2Rad
            // 换算；本处换算为 rad）。
            out.rpyOk = parseRealSequence(child.text().as_string(""), 3, buf);
            if (out.rpyOk) {
                out.roll = buf[0] * kDegToRad;
                out.pitch = buf[1] * kDegToRad;
                out.yaw = buf[2] * kDegToRad;
            }
        }
        // 其余子元素（PosLimit/Depend/Drawable/...）由链扫描各自消费——不猜。
    }
    return out;
}

/// DHJoint 单属性解析（属性缺失/语法非法/非有限→ok=false——调用方按
/// 参数缺省 0 处置并在报告注记；参数完整性由 dhToExplicit 展开结果兜底）。
bool dhAttr(const pugi::xml_node& joint, const char* name, double* out)
{
    const pugi::xml_attribute attr = joint.attribute(name);
    if (!attr) { return false; }
    double v = 0.0;
    if (!parseDoubleStable(attr.as_string(""), &v) || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

/// 几何模型解析（Drawable/CollisionModel 直下：transform＋Polytope/图元）。
WcModel parseWcModel(const pugi::xml_node& model, const std::string& elementKind,
                     const WcLineIndex& index, const std::string& relFile)
{
    WcModel out;
    out.elementKind = elementKind;
    out.span = spanOf(model, index, relFile);
    const WcPosRpy posRpy = parsePosRpy(model);
    out.hasTransform = posRpy.hasPos || posRpy.hasRpy;
    out.px = posRpy.px;
    out.py = posRpy.py;
    out.pz = posRpy.pz;
    out.roll = posRpy.roll;
    out.pitch = posRpy.pitch;
    out.yaw = posRpy.yaw;
    out.transformDegenerate = (posRpy.hasPos && !posRpy.posOk)
                              || (posRpy.hasRpy && !posRpy.rpyOk);
    for (const pugi::xml_node& child : model.children()) {
        const std::string name = child.name();
        if (name == "Polytope") {
            // <Polytope file>——网格文件引用（相对路径；依赖树相对键解析）。
            WcGeometryRef ref;
            ref.fileText = child.attribute("file").as_string("");
            ref.span = spanOf(child, index, relFile);
            if (!ref.fileText.empty()) {
                out.polytopes.push_back(std::move(ref));
            }
        } else if (name == "Box" || name == "Cylinder" || name == "Sphere"
                   || name == "Cone" || name == "Plane" || name == "Tube"
                   || name == "Custom") {
            // 解析图元（框架 geometry_r 文法）：GeometryRef 无参数承载
            // （§4.3-B 仅 mesh/资源引用）——报告面登记（复用 URDF 通道
            // geometry-primitive 词表），不静默降级。
            if (!out.hasPrimitive) {
                out.hasPrimitive = true;
                out.primitiveKind = name;
            }
        }
        // <RGB> 等视觉属性：无落点——ignored 清单由调用方登记。
    }
    return out;
}

}  // namespace

// =====================================================================
// 主体：extractWorkCellDevice（七步——见文件头阅读地图）
// =====================================================================

ImportOutcome extractWorkCellDevice(const ValidatedSource& source,
                                    const ImportOptions& options,
                                    std::vector<core::DiagnosticRecord>& diags)
{
    ImportOutcome outcome;
    std::vector<WcDiagEntry> diagEntries;
    std::size_t diagSeq = 0;

    // 报告源标识（依赖树 rootRel——呈现/追溯；不作身份）。
    const std::string relFile = source.dependencyTree.rootRel;
    outcome.report.sourceLabel = relFile;

    // 来源标记的统一构造点（ImportMapped＋workcell-import 方法标记——
    // methodTag 词法 [a-z0-9./_-] 经 core 工厂校验）。
    const auto importProvenance = []() {
        return core::ValueProvenance::make(core::ProvenanceKind::ImportMapped, {},
                                           std::nullopt, std::string("workcell-import"));
    };

    // 源文本行索引（诊断定位——pugixml 字节偏移→行列）。
    const std::string sourceText(source.bytes.begin(), source.bytes.end());
    const WcLineIndex lineIndex(sourceText);

    // 文档级扫描结果（收尾 lambda 的载体——先于 lambda 声明）。
    WcScan scan;

    // 报告收尾面（场景项/碰撞设置/多链设备/WorkCell 名——成功与失败
    // 出口共用；提取报告是无条件产出面，AT-33"逐项可观察不因失败丢失"）。
    const auto appendReportTail = [&]() {
        // WorkCell 名→displayName 映射（有值时登记——呈现名可编辑）。
        if (scan.wcNamePresent) {
            ImportMappedItem mapped;
            mapped.sourcePath = "WorkCell@name";
            mapped.targetField = "displayName";
            mapped.valueText = scan.wcName;
            mapped.note = "§6.6 设备提取——呈现名取 WorkCell 名（可编辑）";
            mapped.span = scan.rootSpan;
            outcome.report.mapped.push_back(std::move(mapped));
        }
        // WorkCell 级场景项（未保留——单设备草稿不携带）。
        for (const auto& [node, name] : scan.sceneItems) {
            ImportUnsupportedItem u;
            u.kind = std::string(kWcUnsupportedSceneObject);
            u.subject = name + " '" + node.attribute("name").as_string("") + "'";
            u.reason = "WorkCell 场景内容——单设备草稿不携带（未保留）";
            u.guidance = "场景对象可在应用后经场景编辑流另行添加";
            u.span = spanOf(node, lineIndex, relFile);
            outcome.report.unsupported.push_back(std::move(u));
        }
        // WorkCell 级碰撞/邻近设置（未保留——外部文件引用不展开）。
        for (const auto& [node, name] : scan.collisionSetups) {
            ImportUnsupportedItem u;
            u.kind = std::string(kWcUnsupportedCollisionSetup);
            u.subject = name + " file='" + node.attribute("file").as_string("") + "'";
            u.reason = "碰撞/邻近设置为外部文件引用——引用文件不在本通道解析范围"
                       "（排除对清单未提取）";
            u.guidance = "排除对可在应用后经策略草稿候选另行登记";
            u.span = spanOf(node, lineIndex, relFile);
            outcome.report.unsupported.push_back(std::move(u));
        }
        // 多链设备（不可表达——MDL-12 同一判定）。
        for (const auto& [node, name] : scan.otherDevices) {
            ImportUnsupportedItem u;
            u.kind = std::string(kWcUnsupportedDeviceType);
            u.subject = name + " '" + node.attribute("name").as_string("") + "'";
            u.reason = "非单串联设备不可提取（RobotDesign 只承载单串联关节链"
                       "——MDL-12 同一判定）";
            u.guidance = "拆分单串联设备为独立文件后导入";
            u.span = spanOf(node, lineIndex, relFile);
            outcome.report.unsupported.push_back(std::move(u));
        }
    };

    /// 失败出口统一收口（报告收尾＋诊断稳定排序——任何出口同一形态）。
    const auto finalizeWc = [&]() {
        appendReportTail();
        flushWcDiags(diagEntries, diags);
        return std::move(outcome);
    };

    // -----------------------------------------------------------------
    // 第一步：DOM 解析＋根检查。
    // 良构性由 io 通道保证（IO-FORMAT-XML-SYNTAX 在 io 侧拦截）——解析
    // 失败为防御面（装配违约），值面 SourceInconsistent 无草稿。
    // -----------------------------------------------------------------
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed =
        doc.load_buffer(sourceText.data(), sourceText.size(), pugi::parse_default);
    if (!parsed) {
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("channel", "mapWorkCellXml");
        err.params.emplace_back("stage", "xml-parse");
        err.detail = "DOM 解析失败（防御面——io 良构检查应已拦截）: "
                     + std::string(parsed.description());
        outcome.error = std::move(err);
        return finalizeWc();
    }
    const pugi::xml_node root = doc.document_element();

    scan.wcName = root.attribute("name").as_string("");
    scan.wcNamePresent = !scan.wcName.empty();
    scan.rootSpan = spanOf(root, lineIndex, relFile);

    if (std::string(root.name()) != "WorkCell") {
        // 非 WorkCell 根：本通道只消费 .wc.xml（自有规范工件归 MDL-20
        // 保真回读通道——§6.8"其他文件→稳定诊断引导"）。
        ImportUnsupportedItem item;
        item.kind = std::string(kWcUnsupportedSceneObject);
        item.subject = "root <" + std::string(root.name()) + ">";
        item.reason = "根元素不是 WorkCell——.wc.xml 通道只消费 WorkCell 文档";
        item.guidance = "本软件规范模型包请走 MDL-20 导入回读通道";
        item.span = scan.rootSpan;
        outcome.report.unsupported.push_back(std::move(item));
        ImportError err;
        err.code = ImportErrorCode::SourceInconsistent;
        err.params.emplace_back("channel", "mapWorkCellXml");
        err.params.emplace_back("stage", "root-element");
        err.params.emplace_back("actual", std::string(root.name()));
        err.detail = "根元素不是 WorkCell——无草稿产出";
        outcome.error = std::move(err);
        outcome.report.submittable = false;
        return finalizeWc();
    }

    // -----------------------------------------------------------------
    // 第二步：根下元素分类扫描（文档序——确定性；框架 XMLWorkcellParser
    // 文法的 WorkCell 直下元素全集逐类处置——AT-33"逐项"的扫描基础）。
    // -----------------------------------------------------------------
    for (const pugi::xml_node& child : root.children()) {
        const std::string name = child.name();
        if (name == "SerialDevice") {
            WcDevice dev;
            dev.node = child;
            dev.rawName = child.attribute("name").as_string("");
            dev.span = spanOf(child, lineIndex, relFile);
            scan.devices.push_back(std::move(dev));
        } else if (name == "ParallelDevice" || name == "TreeDevice"
                   || name == "MobileDevice") {
            scan.otherDevices.emplace_back(child, name);
        } else if (name == "Frame" || name == "Drawable" || name == "CollisionModel") {
            scan.sceneItems.emplace_back(child, name);
        } else if (name == "CollisionSetup" || name == "ProximitySetup") {
            scan.collisionSetups.emplace_back(child, name);
        } else if (name == "Camera" || name == "Property" || name == "Calibration") {
            // 呈现/元数据面：报告不解释（§6.3 外来扩展同款——ignored）。
            ImportIgnoredItem item;
            item.element = name;
            item.reason = "WorkCell 呈现/元数据元素——提取通道不解释";
            item.span = spanOf(child, lineIndex, relFile);
            outcome.report.ignored.push_back(std::move(item));
        } else {
            // 未知元素：忽略面逐项登记（不静默——NFR-COR-03）。
            ImportIgnoredItem item;
            item.element = name;
            item.reason = "WorkCell 直下未知元素——忽略（不解释）";
            item.span = spanOf(child, lineIndex, relFile);
            outcome.report.ignored.push_back(std::move(item));
        }
    }

    // -----------------------------------------------------------------
    // 第三步：目标设备选择（§6.4 分支报告＋显式选链同机制——§6.6
    // "目标设备选择对应分支报告＋显式选链"；ImportOptions.selectedMain
    // Branch 回填键＝分支报告 branchRoot＝净化后设备名）。
    // -----------------------------------------------------------------
    const WcDevice* selected = nullptr;
    if (scan.devices.size() == 1) {
        selected = &scan.devices[0];            // 单设备直通——无需选链
    } else if (scan.devices.size() > 1) {
        // 多可提取设备：分支报告逐项（对象与原因可观察——§6.4 维度一）。
        for (const WcDevice& dev : scan.devices) {
            ImportBranchItem branch;
            branch.branchRoot = sanitizeLocalName(dev.rawName);
            branch.splitLink = "WorkCell";
            branch.splitJoint = "(设备级选择)";
            branch.disposition = "aux-candidate";
            branch.reason = "SerialDevice '" + dev.rawName
                            + "'——可提取单串联设备（目标设备选择，§6.4 同机制）";
            branch.span = dev.span;
            outcome.report.branches.push_back(std::move(branch));
        }
        if (options.selectedMainBranch.empty()) {
            // 未显式选链：拒绝产出草稿（未经用户显式选择不排除可提取设备
            // ——§6.4 红线；报告完整产出）。
            pushWcDiag(diagEntries, diagSeq++, kMdlImportBranchSelection,
                       core::ObjectId{}, "",
                       "device-count=" + std::to_string(scan.devices.size()),
                       "在向导页显式选择目标设备后重试（MDL-IMPORT-BRANCH-SELECTION）",
                       scan.devices[0].span);
            ImportError err;
            err.code = ImportErrorCode::MultiBranchNeedsSelection;
            err.params.emplace_back("branch-count", std::to_string(scan.devices.size()));
            err.params.emplace_back("branch-roots",
                                    sanitizeLocalName(scan.devices[0].rawName));
            err.detail = "多可提取设备未显式选链——无草稿产出（§6.4 维度一）";
            outcome.error = std::move(err);
            return finalizeWc();
        }
        // 显式选链：匹配净化后设备名。
        for (const WcDevice& dev : scan.devices) {
            if (sanitizeLocalName(dev.rawName) == options.selectedMainBranch) {
                selected = &dev;
                break;
            }
        }
        if (selected == nullptr) {
            // 选链不存在＝调用方契约违约（与分支报告不一致的键）——值面
            // SourceInconsistent（无草稿）。
            ImportError err;
            err.code = ImportErrorCode::SourceInconsistent;
            err.params.emplace_back("selected-main-branch", options.selectedMainBranch);
            err.detail = "显式选链与分支报告不一致（目标设备不存在）——无草稿产出";
            outcome.error = std::move(err);
            return finalizeWc();
        }
        for (ImportBranchItem& branch : outcome.report.branches) {
            if (branch.branchRoot == options.selectedMainBranch) {
                branch.disposition = "selected";
                branch.reason += "；已选为主链";
            }
        }
    }

    // -----------------------------------------------------------------
    // 第四步（a）：无可提取设备的报告面收口（仅多链设备/空 WorkCell）。
    // -----------------------------------------------------------------
    if (selected == nullptr) {
        ImportError err;
        if (!scan.otherDevices.empty()) {
            err.code = ImportErrorCode::UnsupportedJointType;
            err.params.emplace_back("source-type", "multi-chain-device");
            err.detail = "文件仅含非单串联设备——无草稿产出（MDL-12 同一判定）";
        } else {
            err.code = ImportErrorCode::SourceInconsistent;
            err.params.emplace_back("channel", "mapWorkCellXml");
            err.detail = "WorkCell 无 SerialDevice——无草稿产出";
        }
        outcome.error = std::move(err);
        outcome.report.submittable = false;
        return finalizeWc();
    }

    // -----------------------------------------------------------------
    // 第四步（b）：选中设备的链扫描（Joint/Frame/DHJoint 文档序＝链序——
    // 有损语义：链序以文档序承载，refframe/依赖图声明不重排；Q 构型/
    // 设备级碰撞设置/未知子元素逐类处置）。
    // -----------------------------------------------------------------
    WcDeviceScan devScan;
    for (const pugi::xml_node& child : selected->node.children()) {
        const std::string name = child.name();
        if (name == "Joint" || name == "Frame" || name == "DHJoint") {
            WcLinkElement el;
            el.node = child;
            el.elementKind = name;
            el.rawName = child.attribute("name").as_string("");
            if (name == "Joint") {
                el.typeText = child.attribute("type").as_string("");
                devScan.allDhRevolute = false;      // Joint 元素→非 DH 同质链
            } else if (name == "Frame") {
                // Frame type 缺省＝Fixed（框架 FrameParser 文法——type 可选）。
                el.typeText = child.attribute("type").as_string("Fixed");
                devScan.allDhRevolute = false;      // Frame 元素→非 DH 同质链
            } else {  // DHJoint
                // DHJoint 形态：有 d 属性＝Revolute（θ 变量）、有 theta
                // 属性＝Prismatic（d 变量）——框架 DHJoint 文法二选一必现。
                el.typeText = child.attribute("d") ? std::string("Revolute")
                                                   : std::string("Prismatic");
                devScan.hasDhJoint = true;
                if (el.typeText != "Revolute") {
                    devScan.allDhRevolute = false;  // Prismatic DH→不可展开
                }
            }
            el.span = spanOf(child, lineIndex, relFile);
            devScan.hasChainElement = true;
            devScan.chain.push_back(std::move(el));
        } else if (name == "SerialChain") {
            // 嵌套链声明（框架 scope 机制）：扁平文档序已承载链序——嵌套
            // 结构无独立提取语义，登记忽略面。
            ImportIgnoredItem item;
            item.element = "SerialChain '"
                           + std::string(child.attribute("name").as_string("")) + "'";
            item.reason = "嵌套链声明——文档序已承载链序，无独立提取语义";
            item.span = spanOf(child, lineIndex, relFile);
            outcome.report.ignored.push_back(std::move(item));
        } else if (name == "Q") {
            // Q 命名构型（框架 configuration_r 文法：<Q name> 实数序列，
            // rad——框架 rw Q 域）。
            WcQConfig cfg;
            cfg.name = child.attribute("name").as_string("");
            cfg.span = spanOf(child, lineIndex, relFile);
            const std::string text = child.text().as_string("");
            std::vector<double> values;
            const auto isSpace = [](char ch) {
                return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
            };
            std::size_t i = 0;
            bool allOk = !text.empty();
            while (i < text.size()) {
                while (i < text.size() && isSpace(text[i])) { ++i; }
                if (i >= text.size()) { break; }
                const std::size_t start = i;
                while (i < text.size() && !isSpace(text[i])) { ++i; }
                double v = 0.0;
                if (!parseDoubleStable(text.substr(start, i - start), &v)) {
                    allOk = false;
                    break;
                }
                values.push_back(v);
            }
            if (allOk) {
                cfg.q = std::move(values);
                devScan.qConfigs.push_back(std::move(cfg));
            } else {
                // 构型文本解析失败：未保留面登记（非阻断——构型是零位
                // 候选，丢弃不影响关节链提取）。
                ImportUnsupportedItem item;
                item.kind = std::string(kWcUnsupportedQConfigResidual);
                item.subject = "Q '" + cfg.name + "'";
                item.reason = "构型文本数值非法——不映射（原文保留于源文件）";
                item.guidance = "修正 <Q> 文本后重导入";
                item.span = cfg.span;
                outcome.report.unsupported.push_back(std::move(item));
            }
        } else if (name == "CollisionSetup" || name == "ProximitySetup") {
            devScan.collisionSetups.emplace_back(child, name);
        } else if (name == "Property") {
            ImportIgnoredItem item;
            item.element = "Property";
            item.reason = "设备级属性面——提取通道不解释";
            item.span = spanOf(child, lineIndex, relFile);
            outcome.report.ignored.push_back(std::move(item));
        } else if (name == "Drawable" || name == "CollisionModel") {
            // 设备直下几何模型（框架 chainbody_r model_p 文法——按
            // refframe 挂帧）：refframe 匹配本设备链内帧名才归属；无匹配
            // →场景面报告（有损——归属事实入报告）。
            const std::string refframe = child.attribute("refframe").as_string("");
            bool attached = false;
            if (!refframe.empty()) {
                for (const WcLinkElement& el : devScan.chain) {
                    if (el.rawName == refframe) { attached = true; break; }
                }
            }
            if (!attached) {
                ImportUnsupportedItem item;
                item.kind = std::string(kWcUnsupportedSceneObject);
                item.subject = name + " '" + child.attribute("name").as_string("") + "'";
                item.reason = refframe.empty()
                                  ? "设备直下几何模型无 refframe 宿主声明——单设备"
                                    "草稿不携带"
                                  : "refframe='" + refframe
                                        + "' 不在本设备链——按场景内容报告";
                item.guidance = "将模型 refframe 指向本设备链内帧后重导入";
                item.span = spanOf(child, lineIndex, relFile);
                outcome.report.unsupported.push_back(std::move(item));
            }
        } else {
            ImportIgnoredItem item;
            item.element = name;
            item.reason = "设备直下未知元素——忽略（不解释）";
            item.span = spanOf(child, lineIndex, relFile);
            outcome.report.ignored.push_back(std::move(item));
        }
    }

    // -----------------------------------------------------------------
    // 第四步（c）：链元素逐个解析＋类型面判定（不可表达→无草稿——MDL-12
    // 同一判定，不得转 FixedFrame 绕过；Depend 从动→mimic 同款阻断，
    // 类型保留；限位单位换算随类型——Revolute 源 deg→rad）。
    // -----------------------------------------------------------------
    struct ChainItem {
        std::string name;                  ///< 净化后 localName
        std::string rawName;               ///< 源名（报告呈现）
        JointType type = JointType::Revolute;
        bool movable = false;              ///< 可动关节（Revolute/Prismatic）
        bool hasDepend = false;            ///< <Depend> 从动关节（mimic 同款）
        std::string dependOn;              ///< Depend on 属性原文
        WcPosRpy posRpy;                   ///< 位姿（deg 已换算 rad；DHJoint 恒缺省）
        bool isDhJoint = false;            ///< DHJoint 形态（展开期按链元素读取）
        bool hasPosLimit = false;          ///< <PosLimit> 出现
        bool posLimitOk = true;            ///< 限位数值解析成功
        double limitMin = 0.0, limitMax = 0.0;  ///< 限位（已换算：Revolute rad/Prismatic m）
        bool limitIllegalOrder = false;    ///< qmin≥qmax（错误面）
        bool hasVelLimit = false;          ///< <VelLimit max>（未保留面）
        double velLimitMax = 0.0;
        bool hasAccLimit = false;          ///< <AccLimit>（未保留面）
        bool isPassive = false;            ///< state="Passive"（未保留注记）
        std::vector<WcModel> models;       ///< 挂本元素的几何模型（文档序）
        ImportSourceSpan span{};           ///< 元素定位
    };
    std::vector<ChainItem> chain;
    chain.reserve(devScan.chain.size());
    bool unsupportedJointPresent = false;   ///< 不可表达关节（无草稿面）
    bool dhJointIneligible = false;         ///< DHJoint 但非全旋转同质链
    ImportSourceSpan firstUnsupportedSpan;  ///< 首个不可表达定位（诊断锚）

    for (std::size_t ci = 0; ci < devScan.chain.size(); ++ci) {
        const WcLinkElement& el = devScan.chain[ci];
        ChainItem item;
        item.name = sanitizeLocalName(el.rawName);
        item.rawName = el.rawName;
        item.span = el.span;

        // —— 关节类型判定（框架 JointParser/FrameParser 词表逐值；类型面
        //     是"不可表达→无草稿"的判定点——MDL-12 同一判定规则）。
        if (el.elementKind == "Joint") {
            if (el.typeText == "Revolute") {
                item.type = JointType::Revolute;
                item.movable = true;
            } else if (el.typeText == "Prismatic") {
                item.type = JointType::Prismatic;
                item.movable = true;
            } else {
                // Spherical/PrismaticSpherical/Universal/PrismaticUniversal
                // （多自由度）及未知类型：JointType 词表四值之外——不可表达
                // （不得转 FixedFrame——M-6/V-10）。
                unsupportedJointPresent = true;
                if (firstUnsupportedSpan.relFile.empty()) {
                    firstUnsupportedSpan = el.span;
                }
                ImportUnsupportedItem u;
                u.kind = "joint-type";
                u.subject = "joint '" + el.rawName + "'";
                u.reason = "Joint type='" + el.typeText
                           + "' 不可表达（多自由度/词表外——JointType 四值之外）"
                             "——不得转 FixedFrame（M-6）";
                u.guidance = "修改源文件为 Revolute/Prismatic，或拆分设备";
                u.span = el.span;
                outcome.report.unsupported.push_back(std::move(u));
                continue;
            }
        } else if (el.elementKind == "Frame") {
            if (el.typeText == "Fixed") {
                item.type = JointType::Fixed;   // 固定帧＝链上 Fixed 连接（I-MDL-1 允许）
            } else {
                // Movable 等自由帧：6 自由度自由关节——不可表达。
                unsupportedJointPresent = true;
                if (firstUnsupportedSpan.relFile.empty()) {
                    firstUnsupportedSpan = el.span;
                }
                ImportUnsupportedItem u;
                u.kind = "joint-type";
                u.subject = "frame '" + el.rawName + "'";
                u.reason = "Frame type='" + el.typeText
                           + "'（自由帧——6 自由度关节）不可表达——不得转 FixedFrame"
                             "（M-6）";
                u.guidance = "改固定帧或拆分设备";
                u.span = el.span;
                outcome.report.unsupported.push_back(std::move(u));
                continue;
            }
        } else {  // DHJoint
            // DH 提取边界（modeling.md §6.6 增量登记）：仅"全链均为
            // Revolute 型 DHJoint"的同质链经 dhToExplicit 展开（T09 单一
            // 实现——DhChain 只参数化 Revolute 链）；混合链/含 Prismatic
            // DHJoint 不可展开——逐个列入不可表达清单（诚实边界，不绕过）。
            item.isDhJoint = true;
            if (!devScan.allDhRevolute) {
                unsupportedJointPresent = true;
                dhJointIneligible = true;
                if (firstUnsupportedSpan.relFile.empty()) {
                    firstUnsupportedSpan = el.span;
                }
                ImportUnsupportedItem u;
                u.kind = "joint-type";
                u.subject = "DHJoint '" + el.rawName + "'（" + el.typeText + "）";
                u.reason = "DH 参数化关节仅支持全旋转同质链提取（dhToExplicit 契约"
                           "——DH 链只参数化 Revolute）；本链含 Joint/Frame/"
                           "Prismatic DHJoint——不可展开";
                u.guidance = "将 DHJoint 换写为 Joint 显式位姿形态，或拆分同质链";
                u.span = el.span;
                outcome.report.unsupported.push_back(std::move(u));
                continue;
            }
            item.type = JointType::Revolute;
            item.movable = true;
        }

        // —— 子元素扫描（Depend/限位/速度/加速度/几何模型/属性——框架
        //     文法面逐类处置；DHJoint 同样可能挂 PosLimit/Drawable）。
        for (const pugi::xml_node& sub : el.node.children()) {
            const std::string subName = sub.name();
            const std::string refJoint = sub.attribute("refjoint").as_string("");
            const bool refMatches = refJoint.empty() || refJoint == el.rawName;
            if (subName == "Depend") {
                // 从动关节（框架 DependentRevoluteJoint/DependentPrismatic
                // Joint）＝mimic 同义：识别＋报告＋阻断（MDL-12/V-10 同一
                // 判定）——清单在扫描期登记（不可表达收口路径同样产出）；
                // 类型保留的阻断面在草稿构造期汇总。
                item.hasDepend = true;
                item.dependOn = sub.attribute("on").as_string("");
                ImportUnsupportedItem u;
                u.kind = "mimic-joint";
                u.subject = "joint '" + el.rawName + "' → '" + item.dependOn + "'";
                u.reason = "Depend 从动耦合语义不映射（MDL-12/MDL-21 同一判定）"
                           "——关节类型保留（不转 FixedFrame），草稿不可提交（V-10）";
                u.guidance = "移除 Depend 或等待线性耦合通道";
                u.span = el.span;
                outcome.report.unsupported.push_back(std::move(u));
            } else if (subName == "PosLimit") {
                if (!refMatches) {
                    // 设备级共享限位表条目（refjoint 指其他关节）——忽略面。
                    ImportIgnoredItem ig;
                    ig.element = "PosLimit(refjoint=" + refJoint + ")";
                    ig.reason = "设备级共享限位表条目——refjoint 非本关节，忽略";
                    ig.span = spanOf(sub, lineIndex, relFile);
                    outcome.report.ignored.push_back(std::move(ig));
                    continue;
                }
                item.hasPosLimit = true;
                double mn = 0.0, mx = 0.0;
                const bool okMin =
                    parseDoubleStable(sub.attribute("min").as_string(""), &mn);
                const bool okMax =
                    parseDoubleStable(sub.attribute("max").as_string(""), &mx);
                if (okMin && okMax && std::isfinite(mn) && std::isfinite(mx)) {
                    if (item.type == JointType::Revolute) {
                        // Revolute 限位源单位 deg（框架 addLimits convFactor
                        // ＝Deg2Rad）——换算 rad（制式显式纪律）。
                        item.limitMin = mn * kDegToRad;
                        item.limitMax = mx * kDegToRad;
                    } else {
                        // Prismatic 限位源单位 m（convFactor=1）——原样。
                        item.limitMin = mn;
                        item.limitMax = mx;
                    }
                    item.posLimitOk = true;
                    item.limitIllegalOrder = item.limitMin >= item.limitMax;
                } else {
                    item.posLimitOk = false;
                }
            } else if (subName == "VelLimit" && refMatches) {
                item.hasVelLimit = true;
                parseDoubleStable(sub.attribute("max").as_string(""), &item.velLimitMax);
            } else if (subName == "AccLimit" && refMatches) {
                item.hasAccLimit = true;
            } else if (subName == "Drawable" || subName == "CollisionModel") {
                // 链元素直下模型（框架 FrameParser model_p 文法——主挂载点）。
                item.models.push_back(
                    parseWcModel(sub, subName, lineIndex, relFile));
            } else if (subName == "Property") {
                ImportIgnoredItem ig;
                ig.element = "Property";
                ig.reason = "链元素属性面——提取通道不解释";
                ig.span = spanOf(sub, lineIndex, relFile);
                outcome.report.ignored.push_back(std::move(ig));
            }
        }

        // —— 位姿（<Pos> m／<RPY> deg→rad；缺省补零入默认清单——NFR-COR-03；
        //     DHJoint 无位姿子元素——展开期由 dhToExplicit 产出）。
        if (!item.isDhJoint) {
            item.posRpy = parsePosRpy(el.node);
        }

        // —— state="Passive"（被动标记：框架 rw 依赖图语义，RobotDesign
        //     无对应字段——关节照常提取，注记未保留）。
        item.isPassive =
            std::string(el.node.attribute("state").as_string("Active")) == "Passive";

        chain.push_back(std::move(item));
    }

    // —— 链空/不可表达关节：无草稿收口（报告完整——AT-33"提取报告是
    //     无条件产出面"）。
    if (!devScan.hasChainElement || unsupportedJointPresent) {
        if (!devScan.hasChainElement) {
            ImportUnsupportedItem u;
            u.kind = "joint-type";
            u.subject = "SerialDevice '" + selected->rawName + "'";
            u.reason = "设备无链元素（Joint/Frame/DHJoint）——无可提取关节链";
            u.guidance = "确认设备定义后重导入";
            u.span = selected->span;
            outcome.report.unsupported.push_back(std::move(u));
        }
        pushWcDiag(diagEntries, diagSeq++, kMdlImportUnsupportedJoint,
                   core::ObjectId{}, "",
                   dhJointIneligible ? "source-type=dhjoint-mixed-chain"
                                     : "source-type=joint-type-ineligible",
                   "修改源文件为可表达关节类型——不得转 FixedFrame/经选链绕过"
                       "（MDL-12）",
                   firstUnsupportedSpan.relFile.empty() ? selected->span
                                                        : firstUnsupportedSpan);
        ImportError err;
        err.code = ImportErrorCode::UnsupportedJointType;
        err.params.emplace_back("channel", "mapWorkCellXml");
        err.detail = "链含不可表达关节类型——无草稿产出（MDL-12 同一判定）";
        outcome.error = std::move(err);
        outcome.report.submittable = false;
        return finalizeWc();
    }

    // -----------------------------------------------------------------
    // 第五步：草稿构造（authority=Explicit——axis/origin 直映为权威一等
    // 字段，MDL-09；全部 objectId 为确定性临时句柄——§6.7，不经确认不
    // 产生修订、不落盘、不经 project/**，PA-1）。
    // -----------------------------------------------------------------
    RobotDesign draft;
    draft.authority = AuthorityMode::Explicit;
    draft.displayName = scan.wcNamePresent ? scan.wcName : selected->rawName;

    // —— basePlacement：WorkCell 无安装预设语义→ground（MDL-22 同款）。
    draft.basePlacement = BasePlacement{};
    {
        ImportDefaultItem item;
        item.field = "basePlacement";
        item.appliedValue = "ground(preset)";
        item.reason = "WorkCell 无安装预设语义——MDL-22 默认地面";
        item.span = selected->span;
        outcome.report.defaults.push_back(std::move(item));
    }

    // —— 连杆构造（links.size()==joints.size()+1——I-MDL-1）：links[0]＝
    //     设备基座连杆（设备名＋"-base"——WorkCell 无独立连杆元素，帧即
    //     连杆的有损合成）；links[i+1]＝链元素 i 的帧名。
    const std::string baseLinkName = sanitizeLocalName(selected->rawName) + "-base";
    {
        LinkEntry baseLink;
        baseLink.objectId = deriveTempObjectId("link", 0, baseLinkName);
        baseLink.localName = baseLinkName;
        draft.links.push_back(std::move(baseLink));
    }

    // 可动关节的 draft.joints 下标（Q 对位序——框架 rw Q 只含 Active
    // 可动关节：Fixed/Passive/Depend 不占位）。
    std::vector<std::size_t> movableIndex;

    // —— 纯 DH 旋转同质链：整链经 dhToExplicit 展开（T09 单一实现——
    //     §7.4 拆分装配/零位对齐纪律在同一实现内兑现；NFR-MNT-04 不重写
    //     第二份公式）。DhChain 只参数化 Revolute 链——展开前提已由
    //     allDhRevolute 保证。
    if (devScan.hasDhJoint) {
        DhChain dhChain;
        dhChain.joints.reserve(chain.size());
        for (std::size_t ci = 0; ci < chain.size(); ++ci) {
            const ChainItem& item = chain[ci];
            DhChainJoint j;
            j.type = JointType::Revolute;          // 同质链前提（DhChain 契约）
            j.localName = item.name;
            j.objectId = deriveTempObjectId("joint", ci, item.name);
            // DHJoint 属性→DhParameters（框架 dhjoint_r 文法；θ_offset 与
            // zeroOffset 显式分离——§7.4：θ_offset 入几何，zeroOffset 由
            // Q 构型段承载）：
            //   alpha 源单位 deg→rad；a/d 源单位 m；
            //   Revolute：d＝关节变量、offset＝θ 偏置（源 deg→rad）。
            DhParameters p;
            double v = 0.0;
            if (dhAttr(devScan.chain[ci].node, "alpha", &v)) {
                p.alpha = v * kDegToRad;           // deg→rad
            }
            if (dhAttr(devScan.chain[ci].node, "a", &v)) {
                p.a = v;                           // m
            }
            if (dhAttr(devScan.chain[ci].node, "d", &v)) {
                p.d = v;                           // m（Revolute 的关节变量轴）
            }
            if (dhAttr(devScan.chain[ci].node, "offset", &v)) {
                p.thetaOffset = v * kDegToRad;     // θ 偏置：deg→rad
            }
            j.dh = p;
            dhChain.joints.push_back(std::move(j));
        }
        std::vector<core::DiagnosticRecord> dhDiags;
        const DhExplicitConverter converter;
        const ExpandOutcome expanded = converter.dhToExplicit(dhChain, dhDiags);
        if (expanded.ok) {
            // 展开产物即完整 JointEntry（身份/名称/zeroOffset 透传；axis/
            // origin 来源标记＝DerivedReadOnly＋dh-to-explicit——如实表达
            // "由 DH 派生"的提取事实；mapped 清单逐关节登记）。
            std::size_t movableIdx = 0;
            for (std::size_t ci = 0; ci < chain.size(); ++ci) {
                const ChainItem& item = chain[ci];
                if (!item.movable) { continue; }   // 同质链下不出现——防御
                JointEntry entry = expanded.joints[movableIdx];
                if (entry.localName != item.name) {
                    entry.localName = item.name;   // 同名透传（防御性归位）
                }
                ImportMappedItem mapped;
                mapped.sourcePath = "DHJoint '" + item.rawName + "'";
                mapped.targetField = "joints[" + std::to_string(draft.joints.size())
                                     + "]（axis/origin 经 dhToExplicit 展开）";
                mapped.valueText =
                    "thetaOffset="
                    + formatDoubleStable(dhChain.joints[movableIdx].dh.thetaOffset)
                    + " rad; d="
                    + formatDoubleStable(dhChain.joints[movableIdx].dh.d) + " m; a="
                    + formatDoubleStable(dhChain.joints[movableIdx].dh.a) + " m; alpha="
                    + formatDoubleStable(dhChain.joints[movableIdx].dh.alpha) + " rad";
                mapped.note = "DH 参数源（alpha/offset 源单位 deg 已换算 rad）；展开＝"
                              "§7.4 DH→显式（T09 单一实现——零位累积严格还原级联）";
                mapped.span = item.span;
                outcome.report.mapped.push_back(std::move(mapped));
                outcome.report.jointStatuses.push_back(
                    ImportJointStatusItem{item.name, "mapped",
                                          "DHJoint type=Revolute（经 dhToExplicit 展开）",
                                          item.span});
                if (item.movable) {
                    movableIndex.push_back(draft.joints.size());
                }
                draft.joints.push_back(std::move(entry));

                // 子连杆（帧名＋"-link" 合成后缀——帧在模型里既是关节又是
                // 连杆，而 I-MDL-2 的 localName 命名空间是关节∪连杆同域，
                // 同名即冲突；"-link" 后缀是有损合成规则，冲突仍由名称
                // 冲突检查段兜底拦截）。
                LinkEntry link;
                link.objectId = deriveTempObjectId("link", draft.links.size(),
                                                   item.name);
                link.localName = item.name + "-link";
                draft.links.push_back(std::move(link));
                ++movableIdx;
            }
        } else {
            // 展开失败（契约面不应触达——同质链前提已保证）：退回不可表达
            // 面如实报告（不伪造草稿）。
            ImportUnsupportedItem u;
            u.kind = "joint-type";
            u.subject = "SerialDevice '" + selected->rawName + "' DH 链";
            u.reason = "dhToExplicit 展开失败（"
                       + std::string(dhErrorCodeToken(expanded.errorCode))
                       + "）——无草稿产出（不伪造提取结果）";
            u.guidance = "检查 DHJoint 参数完整性后重导入";
            u.span = selected->span;
            outcome.report.unsupported.push_back(std::move(u));
            ImportError err;
            err.code = ImportErrorCode::UnsupportedJointType;
            err.params.emplace_back("channel", "mapWorkCellXml");
            err.detail = "DH 链展开失败——无草稿产出";
            outcome.error = std::move(err);
            outcome.report.submittable = false;
            return finalizeWc();
        }
    } else {
        // —— 普通链（Joint/Frame 显式位姿形态）：逐关节直映。
        for (std::size_t ci = 0; ci < chain.size(); ++ci) {
            const ChainItem& item = chain[ci];
            const core::ObjectId jointTempId =
                deriveTempObjectId("joint", ci, item.name);

            JointEntry entry;
            entry.objectId = jointTempId;
            entry.localName = item.name;
            entry.type = item.type;
            outcome.report.jointStatuses.push_back(
                ImportJointStatusItem{
                    item.name, "mapped",
                    item.type == JointType::Revolute
                        ? std::string("Joint type=Revolute")
                        : item.type == JointType::Prismatic
                              ? std::string("Joint type=Prismatic")
                              : std::string("Frame type=Fixed"),
                    item.span});

            // —— <Depend>：清单已在扫描期登记（不可表达收口路径同样产出）
            //     ——此处仅保留类型（草稿用于呈现与编辑；提交阻断在第七步
            //     dependBlocked 汇总）。

            // —— axis：框架 rw Joint 无轴元素——轴向固定为关节局部系 z 轴
            //     (0,0,1)（框架 RevoluteJoint/PrismaticJoint 语义）；Fixed
            //     关节无轴语义（axis 留缺省——URDF 通道同款）。
            if (item.movable) {
                entry.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                    rw::math::Vector3D<double>(0.0, 0.0, 1.0), importProvenance());
                ImportMappedItem mapped;
                mapped.sourcePath = "chain[" + std::to_string(ci) + "]/axis";
                mapped.targetField =
                    "joints[" + std::to_string(draft.joints.size()) + "].axis";
                mapped.valueText = "0 0 1（无量纲单位向量）";
                mapped.note = "框架 Joint 轴向固定为关节局部系 z 轴（rw 语义）"
                              "——源无轴元素，映射清单注明来源";
                mapped.span = item.span;
                outcome.report.mapped.push_back(std::move(mapped));
            }

            // —— origin（T_parent_joint，m/rad——core §4.6）。
            const bool originIllegal =
                (item.posRpy.hasPos && !item.posRpy.posOk)
                || (item.posRpy.hasRpy && !item.posRpy.rpyOk);
            if (originIllegal) {
                entry.origin = core::SourcedValue<JointPose>::invalid("(Pos/RPY)");
                ImportErrorItem errItem;
                errItem.kind = "value-illegal";
                errItem.subject = "joint '" + item.name + "'.origin";
                errItem.detail = "Pos/RPY 文本数值非法（WorkCell 源：Pos 单位 m、"
                                 "RPY 单位 deg——原文保留于源文件）";
                errItem.span = item.span;
                outcome.report.errors.push_back(std::move(errItem));
                outcome.report.jointStatuses.back().status = "invalid";
                outcome.report.jointStatuses.back().reason = "Pos/RPY 数值非法";
            } else {
                const rw::math::Transform3D<double> t(
                    rw::math::Vector3D<double>(item.posRpy.px, item.posRpy.py,
                                               item.posRpy.pz),
                    rpymath::rpyToRotation(item.posRpy.roll, item.posRpy.pitch,
                                           item.posRpy.yaw));
                entry.origin = core::SourcedValue<JointPose>::provided(
                    JointPose(t), importProvenance());
                ImportMappedItem mapped;
                mapped.sourcePath = "chain[" + std::to_string(ci) + "]/Pos+RPY";
                mapped.targetField =
                    "joints[" + std::to_string(draft.joints.size()) + "].origin";
                mapped.valueText =
                    "xyz(" + formatDoubleStable(item.posRpy.px) + " "
                    + formatDoubleStable(item.posRpy.py) + " "
                    + formatDoubleStable(item.posRpy.pz) + ") m; rpy("
                    + formatDoubleStable(item.posRpy.roll) + " "
                    + formatDoubleStable(item.posRpy.pitch) + " "
                    + formatDoubleStable(item.posRpy.yaw) + ") rad";
                mapped.note = "来源 workcell-import；T_parent_joint（core §4.6）；"
                              "RPY 源单位 deg 已换算 rad（框架 Deg2Rad 同口径）";
                mapped.span = item.span;
                outcome.report.mapped.push_back(std::move(mapped));
                if (!item.posRpy.hasPos || !item.posRpy.hasRpy) {
                    // 缺省补零入默认清单（框架文法 Pos/RPY 均可省略——
                    // 缺省零位姿；NFR-COR-03 不静默）。
                    ImportDefaultItem d;
                    d.field =
                        "joints[" + std::to_string(draft.joints.size()) + "].origin";
                    d.appliedValue =
                        std::string(item.posRpy.hasPos ? "" : "Pos=0 0 0（m）")
                        + (!item.posRpy.hasPos && !item.posRpy.hasRpy ? "；" : "")
                        + (item.posRpy.hasRpy ? "" : "RPY=0 0 0（rad）");
                    d.reason = "<Pos>/<RPY> 省略——WorkCell 文法零位姿缺省";
                    d.span = item.span;
                    outcome.report.defaults.push_back(std::move(d));
                }
            }

            // —— 限位（I-MDL-4：有限限位可动关节 qmin<qmax；rad/m）。
            if (item.type == JointType::Revolute || item.type == JointType::Prismatic) {
                if (item.hasPosLimit && item.posLimitOk) {
                    entry.bounds = core::SourcedValue<JointLimits>::provided(
                        JointLimits{item.limitMin, item.limitMax}, importProvenance());
                    if (!item.limitIllegalOrder) {
                        ImportMappedItem mapped;
                        mapped.sourcePath =
                            "chain[" + std::to_string(ci) + "]/PosLimit";
                        mapped.targetField = "joints["
                            + std::to_string(draft.joints.size()) + "].bounds";
                        mapped.valueText =
                            "qmin=" + formatDoubleStable(item.limitMin) + " qmax="
                            + formatDoubleStable(item.limitMax) + " "
                            + (item.type == JointType::Revolute ? "rad" : "m");
                        mapped.note = "来源 workcell-import；Revolute 源单位 deg 已"
                                      "换算 rad（框架 convFactor 同口径）";
                        mapped.span = item.span;
                        outcome.report.mapped.push_back(std::move(mapped));
                    } else {
                        // qmin≥qmax：Provided 保留原值＋错误面（不静默修复——
                        // NFR-COR-03；应用边界断言兜底）。
                        ImportErrorItem errItem;
                        errItem.kind = "limit-order";
                        errItem.subject = "joint '" + item.name + "'.bounds";
                        errItem.detail =
                            "qmin≥qmax（" + formatDoubleStable(item.limitMin) + " ≥ "
                            + formatDoubleStable(item.limitMax) + " "
                            + (item.type == JointType::Revolute ? "rad" : "m") + "）";
                        errItem.span = item.span;
                        outcome.report.errors.push_back(std::move(errItem));
                        outcome.report.jointStatuses.back().status = "invalid";
                        outcome.report.jointStatuses.back().reason = "限位区间无序";
                    }
                } else {
                    // 缺 PosLimit/解析失败：待确认草稿项（bounds NotProvided
                    // 走降级——§6.3 同款；确认流归向导域侧）。
                    entry.bounds = core::SourcedValue<JointLimits>::notProvided();
                    ImportPendingItem pending;
                    pending.kind = "limit-missing";
                    pending.subject = "joint '" + item.name + "'";
                    pending.question = item.hasPosLimit
                                           ? "PosLimit 数值非法——请修正后确认限位"
                                           : "<PosLimit> 缺失——请录入行程限位后确认";
                    pending.span = item.span;
                    outcome.report.pendingConfirms.push_back(std::move(pending));
                    pushWcDiag(diagEntries, diagSeq++, kMdlImportPendingConfirm,
                               jointTempId, item.name,
                               "item-kind=limit-missing subject=joint '" + item.name
                                   + "'",
                               "逐条确认——缺行程限位（MDL-IMPORT-PENDING-CONFIRM）",
                               item.span);
                }
            }

            // —— VelLimit/AccLimit（无 schema 落点——未保留逐项列出）。
            if (item.hasVelLimit) {
                ImportUnsupportedItem u;
                u.kind = "joint-velocity-limit";
                u.subject = "joint '" + item.rawName + "' VelLimit max="
                            + formatDoubleStable(item.velLimitMax);
                u.reason = "速度上限无 schema 落点（§4.3-A 未登记 maxVelocity——"
                           "不发明字段）——报告承载不静默丢弃";
                u.guidance = "速度上限可经传动设计/策略对象在后续编辑流登记";
                u.span = item.span;
                outcome.report.unsupported.push_back(std::move(u));
            }
            if (item.hasAccLimit) {
                ImportUnsupportedItem u;
                u.kind = std::string(kWcUnsupportedAccLimit);
                u.subject = "joint '" + item.rawName + "' AccLimit";
                u.reason = "加速度上限无 schema 落点（§4.3-A 未登记 maxAcceleration"
                           "——不发明字段）——报告承载不静默丢弃";
                u.guidance = "加速度上限可经传动设计对象在后续编辑流登记";
                u.span = item.span;
                outcome.report.unsupported.push_back(std::move(u));
            }

            // —— Passive 标记（无落点——注记面）。
            if (item.isPassive) {
                outcome.report.jointStatuses.back().reason +=
                    "；state=Passive（被动标记无落点——未保留）";
            }

            if (item.movable) {
                movableIndex.push_back(draft.joints.size());
            }
            draft.joints.push_back(std::move(entry));

            // 子连杆（帧名＋"-link" 合成后缀——帧在模型里既是关节又是连杆，
            // 而 I-MDL-2 的 localName 命名空间是关节∪连杆同域，同名即冲突；
            // "-link" 后缀是有损合成规则，冲突仍由名称冲突检查段兜底拦截）。
            LinkEntry link;
            link.objectId = deriveTempObjectId("link", draft.links.size(), item.name);
            link.localName = item.name + "-link";
            draft.links.push_back(std::move(link));
        }
    }

    // —— 物性默认清单（WorkCell XML 不承载连杆惯性——全部连杆 NotProvided，
    //     降级路径 V15-01/DYN-06；逐连杆登记不静默）。
    for (std::size_t li = 0; li < draft.links.size(); ++li) {
        ImportDefaultItem d;
        d.field = "links[" + std::to_string(li) + "].body";
        d.appliedValue = "not-provided（mass/com/inertia 全缺省）";
        d.reason = "WorkCell XML 不承载连杆惯性数据——NotProvided 走降级路径"
                   "（§6.3/DYN-06 闭环）";
        d.span = selected->span;
        outcome.report.defaults.push_back(std::move(d));
    }

    // -----------------------------------------------------------------
    // 第六步（a）：Q 构型→zeroOffset（首配置；q_authoritative =
    // q_zeroOffset + q_rw——把源设备初始构型设为权威零位，草稿 q=0 呈现
    // 源初始姿态；Q 值域与 zeroOffset 同为 rad。对位序＝可动关节文档序）。
    // -----------------------------------------------------------------
    if (!devScan.qConfigs.empty()) {
        const WcQConfig& first = devScan.qConfigs[0];
        const std::size_t movableCount = movableIndex.size();
        if (first.q.size() == movableCount && movableCount > 0) {
            bool anyNonZero = false;
            for (std::size_t k = 0; k < movableCount; ++k) {
                const double v = first.q[k];
                draft.joints[movableIndex[k]].zeroOffset = v;   // rad（权威零位）
                if (v != 0.0) { anyNonZero = true; }
            }
            ImportMappedItem mapped;
            mapped.sourcePath = "Q '" + first.name + "'";
            mapped.targetField = "joints[*].zeroOffset";
            mapped.valueText = "Q=" + std::to_string(first.q.size()) + " 项（rad）";
            mapped.note = anyNonZero
                              ? "首命名构型提取为权威零位（q_authoritative = "
                                "q_zeroOffset + q_rw）——源初始姿态成为草稿零位"
                              : "首命名构型全零——zeroOffset 维持 0（零位不变）";
            mapped.span = first.span;
            outcome.report.mapped.push_back(std::move(mapped));
        } else {
            // 维数不匹配：构型不映射（未保留——对位失配，诚实报告）。
            ImportUnsupportedItem u;
            u.kind = std::string(kWcUnsupportedQConfigResidual);
            u.subject = "Q '" + first.name + "'";
            u.reason = "构型维数（" + std::to_string(first.q.size())
                       + "）与可动关节数（" + std::to_string(movableCount)
                       + "）不匹配——零位对位失配，不映射";
            u.guidance = "修正 <Q> 序列或确认设备链定义后重导入";
            u.span = first.span;
            outcome.report.unsupported.push_back(std::move(u));
        }
        // 备选构型：未保留逐项列出。
        for (std::size_t qi = 1; qi < devScan.qConfigs.size(); ++qi) {
            const WcQConfig& cfg = devScan.qConfigs[qi];
            ImportUnsupportedItem u;
            u.kind = std::string(kWcUnsupportedQConfigResidual);
            u.subject = "Q '" + cfg.name + "'";
            u.reason = "备选命名构型（仅首配置提取为权威零位）——无落点";
            u.guidance = "构型可在应用后经关节编辑/命名位姿集另行承载";
            u.span = cfg.span;
            outcome.report.unsupported.push_back(std::move(u));
        }
    }

    // -----------------------------------------------------------------
    // 第六步（b）：几何模型→visual/collision＋资源登记（Polytope 网格；
    // Recorded 态——缺失叶 IO-RES-MISSING 事实，"缺失≠不可行"V-08）。
    // -----------------------------------------------------------------
    for (std::size_t ci = 0; ci < chain.size(); ++ci) {
        const ChainItem& item = chain[ci];
        if (item.models.empty()) { continue; }
        LinkEntry& link = draft.links[ci + 1];   // links[i+1]＝链元素 i
        bool visualTaken = false;
        bool collisionTaken = false;
        for (const WcModel& model : item.models) {
            const bool isCollision = model.elementKind == "CollisionModel";
            // 重复模型：仅首个映射（schema 单引用——复用 URDF 词表逐条报告）。
            if ((isCollision && collisionTaken) || (!isCollision && visualTaken)) {
                ImportUnsupportedItem u;
                u.kind = isCollision ? "multiple-collision" : "multiple-visual";
                u.subject = "frame '" + item.rawName + "' " + model.elementKind;
                u.reason = "多几何引用（schema 单引用）——仅首个映射，其余逐条报告";
                u.guidance = "合并模型或选择保留其一";
                u.span = model.span;
                outcome.report.unsupported.push_back(std::move(u));
                continue;
            }
            // 图元：GeometryRef 无参数承载——报告不映射（复用 URDF 词表）。
            if (model.hasPrimitive) {
                ImportUnsupportedItem u;
                u.kind = "geometry-primitive";
                u.subject = "frame '" + item.rawName + "' <" + model.primitiveKind
                            + ">";
                u.reason = "图元几何无映射语义（仅登记 mesh 引用行）——报告不静默"
                           "丢弃";
                u.guidance = "改用 Polytope 网格引用，或待图元映射语义登记后重试";
                u.span = model.span;
                outcome.report.unsupported.push_back(std::move(u));
                continue;
            }
            if (model.polytopes.empty()) {
                ImportIgnoredItem ig;
                ig.element = model.elementKind + "（无 Polytope）";
                ig.reason = "几何模型无网格引用——无可映射几何，忽略（不解释）";
                ig.span = model.span;
                outcome.report.ignored.push_back(std::move(ig));
                continue;
            }
            // 变换非法：不产 GeometryRef（错误面——NFR-COR-03）。
            if (model.transformDegenerate) {
                ImportErrorItem errItem;
                errItem.kind = "value-illegal";
                errItem.subject =
                    "frame '" + item.rawName + "'/" + model.elementKind + "/transform";
                errItem.detail = "Pos/RPY 数值非法（原文保留于源文件）";
                errItem.span = model.span;
                outcome.report.errors.push_back(std::move(errItem));
                continue;
            }
            // 首个 Polytope 映射（单模型多网格——同款"仅首个"纪律）。
            const WcGeometryRef& geo = model.polytopes[0];
            for (std::size_t gi = 1; gi < model.polytopes.size(); ++gi) {
                ImportUnsupportedItem u;
                u.kind = isCollision ? "multiple-collision" : "multiple-visual";
                u.subject = "frame '" + item.rawName + "' Polytope '"
                            + model.polytopes[gi].fileText + "'";
                u.reason = "单模型多网格引用——仅首个映射，其余逐条报告";
                u.guidance = "合并网格资产或拆分模型";
                u.span = model.span;
                outcome.report.unsupported.push_back(std::move(u));
            }
            // 相对键折叠＋依赖树节点查找（io relPath 约定同款——正斜杠＋
            // 折叠小写）。.wc.xml 的 Polytope 引用不产生 io 树边（依赖树
            // XML 边抽取只认 include/mesh 元素）——节点缺失是常态：资源
            // 如实登记 Recorded＋缺失事实（"缺失≠不可行"，§6.7/V-08）。
            const std::string relKey = foldRelKey(geo.fileText);
            const io::ResourceNode* node = nullptr;
            for (const io::ResourceNode& n : source.dependencyTree.nodes) {
                if (n.relPath == relKey) {
                    node = &n;
                    break;
                }
            }
            const bool missing = node == nullptr || !node->exists;
            // 资源清单条目（同键去重；Recorded 必带 externalRecord——
            // I-MDL-10；缺失叶 digest 空＋absPath 以相对键登记——io probe
            // 届时报 Missing）。
            bool duplicated = false;
            for (const ResourceRef& existing : draft.resourceManifest) {
                if (existing.resourceId == relKey) { duplicated = true; break; }
            }
            if (!duplicated) {
                ResourceRef ref;
                ref.resourceId = relKey;
                ref.state = ResourceState::Recorded;
                ExternalResourceRecord record;
                record.absPath = (node != nullptr && node->exists)
                                     ? node->snapshot->finalPath.string()
                                     : relKey;
                if (node != nullptr && node->exists) {
                    ref.contentDigest = node->snapshot->contentDigest;
                    record.recordedDigest = node->snapshot->contentDigest;
                }
                ref.externalRecord = record;
                draft.resourceManifest.push_back(std::move(ref));
            }
            // 资源状态表行（缺失事实一次一行）。
            ImportResourceItem row;
            row.resourceId = relKey;
            row.relPath = relKey;
            row.state = missing ? "missing" : "recorded";
            row.digestHex = (!missing && node != nullptr)
                                ? digestToHex(node->snapshot->contentDigest)
                                : std::string();
            row.span = geo.span;
            outcome.report.resources.push_back(std::move(row));
            if (missing) {
                // IO-RES-MISSING 事实诊断（不置阻断——V-08 应用可过（Warning）；
                // 码面＝io 注册码——io::errorCodeToken 唯一来源）。
                pushWcDiag(diagEntries, diagSeq++,
                           io::errorCodeToken(io::IoErrorCode::ResMissing),
                           link.objectId, link.localName,
                           "path=" + relKey
                               + "（依赖树缺失叶/无叶——资源事实，§2.5 正交）",
                           "重关联文件或固化资源——草稿可保存（就绪校验 L6 呈现）",
                           geo.span);
            }
            // GeometryRef（T_link_geom＝"geom 系相对 link 系"，m/rad——core §4.6）。
            GeometryRef geometry;
            geometry.resourceRefId = relKey;
            geometry.kind = GeometryKind::Mesh;
            geometry.localTransform = rw::math::Transform3D<double>(
                rw::math::Vector3D<double>(model.px, model.py, model.pz),
                rpymath::rpyToRotation(model.roll, model.pitch, model.yaw));
            if (isCollision) {
                link.collision = std::move(geometry);
                collisionTaken = true;
            } else {
                link.visual = std::move(geometry);
                visualTaken = true;
            }
            ImportMappedItem mapped;
            mapped.sourcePath = "frame '" + item.rawName + "'/" + model.elementKind
                                + "/Polytope";
            mapped.targetField = "links[" + std::to_string(ci + 1) + "]."
                                 + (isCollision ? "collision" : "visual")
                                 + " → resourceManifest[" + relKey + "]";
            mapped.valueText = missing ? relKey + "（缺失——Recorded 草稿可携带）"
                                       : relKey;
            mapped.note = "身份＝内容摘要（路径不作身份，§4.8）；模型位姿＝"
                          "T_link_geom（RPY 源 deg 已换算 rad）";
            mapped.span = model.span;
            outcome.report.mapped.push_back(std::move(mapped));
        }
    }

    // —— 设备级碰撞/邻近设置引用（未保留——外部文件不在本通道解析范围）。
    for (const auto& [node, name] : devScan.collisionSetups) {
        ImportUnsupportedItem u;
        u.kind = std::string(kWcUnsupportedCollisionSetup);
        u.subject = name + " file='" + node.attribute("file").as_string("") + "'";
        u.reason = "碰撞/邻近设置为外部文件引用——引用文件不在本通道解析范围"
                   "（排除对清单未提取）";
        u.guidance = "排除对可在应用后经策略草稿候选另行登记";
        u.span = spanOf(node, lineIndex, relFile);
        outcome.report.unsupported.push_back(std::move(u));
    }

    // —— 设备基帧锚点/位姿（未保留——安装预设语义归 runtime 单一权威产出
    //     点 P-RT-4；basePlacement 已按 MDL-22 默认地面）。
    {
        ImportUnsupportedItem u;
        u.kind = std::string(kWcUnsupportedBasePlacement);
        u.subject = "SerialDevice '" + selected->rawName + "' base";
        u.reason = "设备基帧安装位姿/锚点不映射（BasePlacement 安装预设语义归"
                   " runtime 单一权威产出点——P-RT-4）——按 MDL-22 默认地面";
        u.guidance = "安装布置可在应用后经基座安装编辑调整";
        u.span = selected->span;
        outcome.report.unsupported.push_back(std::move(u));
    }

    // -----------------------------------------------------------------
    // 第七步：能力结论＋阻断面汇总＋出口（judgeChainCapability 复用——
    // MDL-12 同一判定的单一实现；类型保留不降级——V12-01）。
    // -----------------------------------------------------------------
    std::vector<JointType> chainTypes;
    chainTypes.reserve(draft.joints.size());
    for (const JointEntry& j : draft.joints) { chainTypes.push_back(j.type); }
    outcome.report.chainCapability = judgeChainCapability(chainTypes);
    if (outcome.report.chainCapability.kind == ChainCapabilityKind::BeyondTemplateRange) {
        pushWcDiag(diagEntries, diagSeq++, kMdlImportTemplateRange, core::ObjectId{},
                   "",
                   "axes=" + std::to_string(outcome.report.chainCapability.movableAxes)
                       + " prismatic="
                       + (outcome.report.chainCapability.containsPrismatic ? "yes"
                                                                           : "no"),
                   "草稿兼容编辑可用；模板创建与正式计算/报告阻断"
                       "（MDL-IMPORT-TEMPLATE-RANGE）",
                   selected->span);
    }

    // —— Depend 阻断面（存在→不可提交；草稿仍产出——呈现与编辑用）。
    bool dependBlocked = false;
    for (const ChainItem& item : chain) {
        if (item.hasDepend) { dependBlocked = true; break; }
    }
    if (dependBlocked) {
        ImportError err;
        err.code = ImportErrorCode::MimicBlocked;
        err.params.emplace_back("channel", "mapWorkCellXml");
        err.detail = "链含 Depend 从动关节——草稿不可提交修订（类型保留，V-10）";
        outcome.error = std::move(err);
        outcome.report.submittable = false;
    }

    // —— 名称冲突检查（净化后 localName 重复——I-MDL-2：命名空间为
    //     关节∪连杆同域，应用边界前拦截；逐对登记不静默）。合成后缀
    //     （"-base"/"-link"）与帧名同撞的情形也在此兜底。
    {
        std::vector<std::pair<std::string, std::string>> allNames;  // (名, 类别)
        allNames.reserve(draft.joints.size() + draft.links.size());
        for (const JointEntry& j : draft.joints) {
            allNames.emplace_back(j.localName, "joint");
        }
        for (const LinkEntry& l : draft.links) {
            allNames.emplace_back(l.localName, "link");
        }
        for (std::size_t i = 0; i < allNames.size(); ++i) {
            for (std::size_t j = i + 1; j < allNames.size(); ++j) {
                if (allNames[i].first == allNames[j].first) {
                    ImportErrorItem errItem;
                    errItem.kind = "name-conflict";
                    errItem.subject = allNames[i].second + " '"
                                      + allNames[i].first + "'";
                    errItem.detail = "localName 重复（I-MDL-2 关节∪连杆同域——与 "
                                     + allNames[j].second + " 同名）——应用边界前"
                                                      "拦截";
                    errItem.span = selected->span;
                    outcome.report.errors.push_back(std::move(errItem));
                    outcome.report.submittable = false;
                }
            }
        }
    }

    // —— 非法数值面（任一关节状态 invalid→不可提交；语义与 URDF 通道
    //     一致：值面错误项在报告，应用边界断言兜底）。
    for (const ImportJointStatusItem& status : outcome.report.jointStatuses) {
        if (status.status == "invalid") {
            outcome.report.submittable = false;
            break;
        }
    }

    outcome.draft = std::move(draft);

    // 公共收尾＋诊断稳定排序出口（成功/失败共用——NFR-COR-02）。
    return finalizeWc();
}

}  // namespace sdurws::ird::modeling
