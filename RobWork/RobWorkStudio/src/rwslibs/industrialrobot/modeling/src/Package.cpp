/**
 * @file   Package.cpp
 * @brief  规范模型包导出/导入的实现（ModelPackagePort）——ZIP 容器装配
 *         （STORED 最小 APPNOTE 写侧）＋manifest canonical JSON＋原子写出
 *         与预提交自检＋导入三道校验（来源/schema/清单哈希）＋roundtrip
 *         五组逐项核对清单。
 *
 * 设计依据：units/modeling.md §6.8/§9.4.9（Package.hpp 文件头注全文）；
 * 任务契约 tasks/foundation/WP-13-T13.json acceptance 1~5。
 *
 * 实现注（逐段职责，供 review 对照）：
 *   ① ZIP 写侧（appendZip* 与 buildZipContainer）：仅 STORED 条目的最小
 *      容器——本地文件头＋中央目录＋EOCD，CRC-32 为容器规范自带字段，
 *      UTF-8 名标志（GP bit 11），固定 DOS 时间戳（确定性——NFR-COR-02，
 *      同条目集→同容器字节）；条目数 <65535、容量 <4 GiB（超限防御性
 *      拒绝——本包承载单一模型，正常不可达）。
 *   ② manifest：io canonicalizeJson（§5.9.3 canonical 唯一实现点）产出
 *      确定性字节；contentDigest＝对 manifest 所列对象清单的规范序列摘要
 *      （SHA-256，core ContentDigester——SA-12 唯一摘要算法）。
 *   ③ 导出：闭包 canonical 字节**原样**入包（不重编码——CON-01/PA-2 字节
 *      权威），io AtomicFile prepare→写入→IZipChannel 重开自检→commit；
 *      自检在暂存位上执行（commit 前目标不变），失败即 abort——先前输出
 *      完整保留（V-29 文件层观测）。
 *   ④ 导入：openZipChannel（IO-T04 落位面）→ manifest 来源标识/schema
 *      校验（fail-closed）→归档与清单条目集比对＋逐条目哈希复算→
 *      Codec 解码装配报告。非本软件工件→PackageUnknown＋
 *      MDL-IMPORT-PACKAGE-UNKNOWN（引导 MDL-18/R2——不代替其解析）。
 *   ⑤ 核对清单 roundtripChecklist：五组逐项（authority/physics/resource/
 *      collision/pose），导出侧与导入侧共用本唯一实现（NFR-MNT-04）；
 *      数值文本 std::to_chars 最短往返（locale 无关——NFR-COR-01/02）。
 */

#include <sdurws/ird/modeling/Package.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <utility>

#include <sdurws/ird/core/Provenance.hpp>    // core::FieldState（SourcedValue 四态清单文本）
#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken——io 码 token 唯一来源（Import.cpp 同款）
#include <sdurws/ird/io/Json.hpp>            // JsonValue/canonicalizeJson/digestCanonicalJson/makeStructuredDataReader
#include <sdurws/ird/io/TempArea.hpp>        // ITempAreaManager/TempAreaRole（导出暂存区——io §7.5）
#include <sdurws/ird/io/ZipChannel.hpp>      // openZipChannel/IZipChannel/ZipManifestEntry（IO-T04 落位面）
#include <sdurws/ird/modeling/DiagCodes.hpp>     // MDL-* 码常量（禁字符串拼码——§9.5）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // 五对象 token/schema 常量

namespace sdurws::ird::modeling {
namespace {

// =====================================================================
// 小工具：字节/摘要/文本格式化（全部纯函数——确定性）
// =====================================================================

/// 32 字节摘要 → 64 字符小写 hex（manifest 文本承载形态；SHA-256 唯一
/// 摘要算法不变——本函数只是序列化格式化，不是第二哈希）。
std::string toHex(const core::Digest256& digest)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (std::uint8_t byte : digest) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

/// 64 字符小写 hex → 32 字节摘要（manifest 解析侧；严格小写——与 toHex
/// 互逆，大小写混排/长度违约＝工件破损）。
std::optional<core::Digest256> fromHex(std::string_view hex)
{
    if (hex.size() != 64) { return std::nullopt; }
    core::Digest256 out{};
    for (std::size_t i = 0; i < hex.size(); ++i) {
        const char c = hex[i];
        int value;
        if (c >= '0' && c <= '9') { value = c - '0'; }
        else if (c >= 'a' && c <= 'f') { value = c - 'a' + 10; }
        else { return std::nullopt; }   // 大写/非 hex 字符＝破损（严格小写契约）
        if (i % 2 == 0) {
            out[i / 2] = static_cast<std::uint8_t>(value << 4);
        } else {
            out[i / 2] |= static_cast<std::uint8_t>(value);
        }
    }
    return out;
}

/// 对字节序列计算 SHA-256（core ContentDigester——SA-12 唯一摘要入口）。
core::Digest256 digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    if (!bytes.empty()) {
        digester.update(bytes.data(), bytes.size());
    }
    return digester.finalize();
}

/// double → 最短往返十进制文本（std::to_chars——不经 locale，NFR-COR-02；
/// 非有限值不该出现在建模对象中，防御性输出占位并保持确定性）。
std::string fmtDouble(double v)
{
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
    if (result.ec != std::errc()) {
        // 非有限值（NaN/Inf）缺省占位：确定性优先，不抛（清单构造是纯函数；
        // 建模对象在不变量层已拒非有限值——此处仅防御）。
        return "nan";
    }
    return std::string(buffer, result.ptr);
}

/// SourcedValue<T> → 稳定文本（四态显式：provided:<v>/not-provided/
/// not-applicable/invalid:<raw>——缺失≠零的类型化承载不塌缩，§4.3 表）。
template <class T, class Fmt>
std::string fmtSourced(const core::SourcedValue<T>& v, Fmt format)
{
    switch (v.state()) {
    case core::FieldState::Provided:
        return "provided:" + format(v.value());
    case core::FieldState::NotProvided:
        return "not-provided";
    case core::FieldState::NotApplicable:
        return "not-applicable";
    case core::FieldState::Invalid:
        return "invalid:" + v.invalidRawInput();   // 原始输入串（NFR-COR-03 不静默转写）
    }
    return "not-provided";   // 全枚举已覆盖（防御性——不达此处）
}

/// Vector3D → "x,y,z"（无空格——稳定文本；单位由条目 valueText 尾注承载）。
std::string fmtVector(const rw::math::Vector3D<double>& v)
{
    return fmtDouble(v[0]) + "," + fmtDouble(v[1]) + "," + fmtDouble(v[2]);
}

/// 位姿 → 平移＋旋转行主序 12 元组（逐元素精确文本化——附录 D 第 12 项
/// 位姿身份无容差的文本面；JointPose 经 Transform3D 同构转换入参）。
std::string fmtPose(const rw::math::Transform3D<double>& t)
{
    std::string out = fmtDouble(t.P()[0]) + "," + fmtDouble(t.P()[1]) + "," + fmtDouble(t.P()[2]);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            out += "," + fmtDouble(t.R()(row, col));
        }
    }
    return out;
}

/// JointPose（Transform3D 同构值类型——冒烟 header-only 纪律）→ 文本
/// （经 operator Transform3D 转换，与消费侧零语义差异）。
std::string fmtJointPose(const JointPose& p)
{
    return fmtPose(rw::math::Transform3D<double>(p));
}

/// 限位对 → "(qmin,qmax)"（rad/m 随关节类型——单位由条目尾注承载）。
std::string fmtLimits(const JointLimits& limits)
{
    return "(" + fmtDouble(limits.first) + "," + fmtDouble(limits.second) + ")";
}

// =====================================================================
// ZIP 容器写侧（STORED 最小 APPNOTE——背景说明见文件头注实现注①）
// =====================================================================

/// CRC-32（IEEE 802.3 反射多项式 0xEDB88320，初值/终值 0xFFFFFFFF——ZIP
/// 容器规范字段；容器传输校验，非内容身份，SA-12 不受影响）。
std::uint32_t crc32(const std::vector<std::uint8_t>& bytes)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            // 反射算法：最低位为 1 则右移并异或多项式，否则仅右移。
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/// 小端追加 u16/u32（ZIP 规范字段序——APPNOTE 全部小端）。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

/// 包内一个条目（写侧中间形态；digest 即时计算供 manifest 装配）。
struct PackageEntry {
    std::string path;                    ///< 条目名（UTF-8 正斜杠——容器层约定）
    std::vector<std::uint8_t> bytes;     ///< 未压缩原文
    core::Digest256 digest{};            ///< SHA-256（对 bytes——manifest/自检同源）
    std::uint32_t crc = 0;               ///< CRC-32（容器字段）
};

/// 构造条目（digest/crc 一次性计算——同 bytes 多次使用不重复计算）。
PackageEntry makeEntry(std::string path, std::vector<std::uint8_t> bytes)
{
    PackageEntry entry;
    entry.path = std::move(path);
    entry.bytes = std::move(bytes);
    entry.digest = digestBytes(entry.bytes);
    entry.crc = crc32(entry.bytes);
    return entry;
}

/// 固定 DOS 时间/日期（1980-01-01 00:00:00——容器时间戳与内容无关且恒定，
/// 确定性来源；服务不取时钟，§9.0 确定注）。
constexpr std::uint16_t kDosTime = 0;
constexpr std::uint16_t kDosDate = 0x0021;   // 年偏移 0（1980）｜月 1｜日 1

/**
 * @brief 装配 ZIP 容器字节（STORED 条目；条目序＝传入序——调用方已按
 *        路径字典序排序，容器确定性由此承接）。
 *
 * 布局（APPNOTE）：[本地文件头＋数据]×N → 中央目录 → EOCD。字段取值：
 * versionNeeded=20（2.0——STORED 最低规范要求）、GP flag 0x0800（UTF-8
 * 名）、method=0（STORED）、外部属性 0（FAT 宿主、无 symlink/设备位——
 * 导入侧 SP-4 预检自然通过）。
 */
std::vector<std::uint8_t> buildZipContainer(const std::vector<PackageEntry>& entries)
{
    std::vector<std::uint8_t> out;
    // 容量预reserve（本地头 30+名＋数据＋中央目录 46+名＋EOCD 22——避免
    // 反复扩容；上界估算即可）。
    std::size_t upper = 22;
    for (const PackageEntry& e : entries) {
        upper += 30 + e.path.size() + e.bytes.size() + 46 + e.path.size();
    }
    out.reserve(upper);

    struct CentralRecord {
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint64_t localOffset = 0;
        const std::string* path = nullptr;
    };
    std::vector<CentralRecord> centrals;
    centrals.reserve(entries.size());

    // ---- 第一遍：本地文件头＋数据 ----
    for (const PackageEntry& e : entries) {
        CentralRecord record;
        record.crc = e.crc;
        record.size = static_cast<std::uint32_t>(e.bytes.size());
        record.localOffset = static_cast<std::uint32_t>(out.size());
        record.path = &e.path;
        centrals.push_back(record);

        appendU32(out, 0x04034B50u);    // 本地文件头签名
        appendU16(out, 20);             // version needed（2.0）
        appendU16(out, 0x0800u);        // GP flag bit 11＝UTF-8 名
        appendU16(out, 0);              // method＝STORED
        appendU16(out, kDosTime);
        appendU16(out, kDosDate);
        appendU32(out, e.crc);
        appendU32(out, record.size);    // 压缩大小（STORED＝原大小）
        appendU32(out, record.size);    // 未压缩大小
        appendU16(out, static_cast<std::uint16_t>(e.path.size()));
        appendU16(out, 0);              // extra 长度（无）
        out.insert(out.end(), e.path.begin(), e.path.end());
        out.insert(out.end(), e.bytes.begin(), e.bytes.end());
    }

    // ---- 第二遍：中央目录 ----
    const std::uint64_t centralOffset = out.size();
    for (const CentralRecord& r : centrals) {
        appendU32(out, 0x02014B50u);    // 中央目录头签名
        appendU16(out, (3u << 8) | 20u);// version made by（宿主 UNIX×核心 2.0——形态不影响读侧）
        appendU16(out, 20);             // version needed
        appendU16(out, 0x0800u);        // UTF-8 名（与本地头一致）
        appendU16(out, 0);              // STORED
        appendU16(out, kDosTime);
        appendU16(out, kDosDate);
        appendU32(out, r.crc);
        appendU32(out, r.size);
        appendU32(out, r.size);
        appendU16(out, static_cast<std::uint16_t>(r.path->size()));
        appendU16(out, 0);              // extra（无）
        appendU16(out, 0);              // 注释（无）
        appendU16(out, 0);              // 起始盘号
        appendU16(out, 0);              // 内部属性
        appendU32(out, 0);              // 外部属性（FAT 宿主 0——无 symlink/设备位）
        appendU32(out, static_cast<std::uint32_t>(r.localOffset));
        out.insert(out.end(), r.path->begin(), r.path->end());
    }
    const std::uint64_t centralSize = out.size() - centralOffset;

    // ---- EOCD ----
    appendU32(out, 0x06054B50u);        // EOCD 签名
    appendU16(out, 0);                  // 本盘号
    appendU16(out, 0);                  // 中央目录起始盘
    appendU16(out, static_cast<std::uint16_t>(centrals.size()));
    appendU16(out, static_cast<std::uint16_t>(centrals.size()));
    appendU32(out, static_cast<std::uint32_t>(centralSize));
    appendU32(out, static_cast<std::uint32_t>(centralOffset));
    appendU16(out, 0);                  // 注释长度
    return out;
}

// =====================================================================
// manifest 装配（canonical JSON——io §5.9.3 唯一实现点消费）
// =====================================================================

/// manifest 对象清单条目（canonical JSON DOM 侧形态——path 字典序由调用方保证）。
struct ManifestItem {
    std::string path;
    std::uint64_t size = 0;
    core::Digest256 sha256{};
};

/// 对 manifest 清单计算 contentDigest（"对象清单与 digest"的 digest 面：
/// 对清单规范序列逐条 path '\n' size '\n' sha256hex '\n' 拼接后 SHA-256
/// ——清单语义身份；与逐条目 sha256（内容身份）互补，防清单被改）。
core::Digest256 manifestListDigest(const std::vector<ManifestItem>& items)
{
    core::ContentDigester digester;
    for (const ManifestItem& item : items) {
        const std::string line = item.path + '\n' + std::to_string(item.size) + '\n'
            + toHex(item.sha256) + '\n';
        digester.update(line.data(), line.size());
    }
    return digester.finalize();
}

/// 构造 manifest 的受限 JSON DOM（io canonicalizeJson 输入；null profile
/// ＝键名字典序——确定性键序，与 io §7.1 manifest 按 path 字典序同风格）。
io::JsonValue buildManifestValue(const std::vector<ManifestItem>& items,
                                 const std::string& createdAtUtcIso8601)
{
    io::JsonValue root;
    root.type = io::JsonValue::Type::Object;

    auto addString = [&root](std::string key, std::string value) {
        io::JsonMember member;
        member.key = std::move(key);
        member.value.type = io::JsonValue::Type::String;
        member.value.stringValue = std::move(value);
        root.members.push_back(std::move(member));
    };
    auto addInt = [&root](std::string key, std::int64_t value) {
        io::JsonMember member;
        member.key = std::move(key);
        member.value.type = io::JsonValue::Type::Integer;
        member.value.integerValue = value;
        root.members.push_back(std::move(member));
    };

    // 字段书写序无关紧要——canonicalizeJson 按 null profile 键名字典序重排
    //（确定性口径）；这里按语义分组书写便于阅读。
    addString("contentDigest", "sha256-" + toHex(manifestListDigest(items)));
    if (!createdAtUtcIso8601.empty()) {
        // 调用方传入才携带（服务不取时钟——NFR-COR-02；仅记录不判定）。
        addString("createdAtUtc", createdAtUtcIso8601);
    }

    io::JsonMember entriesMember;
    entriesMember.key = "entries";
    entriesMember.value.type = io::JsonValue::Type::Array;
    for (const ManifestItem& item : items) {
        io::JsonValue entry;
        entry.type = io::JsonValue::Type::Object;
        io::JsonMember pathMember;
        pathMember.key = "path";
        pathMember.value.type = io::JsonValue::Type::String;
        pathMember.value.stringValue = item.path;
        io::JsonMember shaMember;
        shaMember.key = "sha256";
        shaMember.value.type = io::JsonValue::Type::String;
        shaMember.value.stringValue = "sha256-" + toHex(item.sha256);
        io::JsonMember sizeMember;
        sizeMember.key = "size";
        sizeMember.value.type = io::JsonValue::Type::Integer;
        sizeMember.value.integerValue = static_cast<std::int64_t>(item.size);
        // 成员按键字典序写入（path/sha256/size）——与 canonical 重排一致，
        // 避免"书写序≠最终序"的阅读歧义。
        entry.members.push_back(std::move(pathMember));
        entry.members.push_back(std::move(shaMember));
        entry.members.push_back(std::move(sizeMember));
        entriesMember.value.items.push_back(std::move(entry));
    }
    root.members.push_back(std::move(entriesMember));

    addString("formatId", std::string(kModelPackageFormatId));
    addString("producer", std::string(kModelPackageProducer));
    addInt("schemaVersion", kModelPackageSchemaVersion);
    return root;
}

// =====================================================================
// 诊断与错误构造（产码唯一经 DiagCodes.hpp 常量——禁字符串拼码）
// =====================================================================

/// 追加 MDL-EXPORT-FAILED 诊断（stage＝原子写出链路段位；ioCodeToken＝io
/// 侧稳定错误码 token——环境四分类定位要素，与描述符 paramSchema 对齐）。
void pushExportFailedDiag(std::vector<core::DiagnosticRecord>& diags, std::string stage,
                          std::string ioCodeToken, std::string cause)
{
    diags.push_back(core::DiagnosticRecord::make(
        std::string(kMdlExportFailed),
        std::nullopt,                                   // 无定位对象（文件级失败）
        std::nullopt,
        std::nullopt,
        "package-export@" + stage,                      // context：链路段位
        std::move(cause),
        "项目状态不变且旧输出文件完好；检查目标路径/预算后重试（MDL-20）"));
    (void)ioCodeToken;   // io 码经 ModelingError.params 携带（io-code 键）——诊断 params
                         // 面随 reporting 文案层消费；此处保留形参位以约束两路同源。
}

/// 追加 MDL-IMPORT-PACKAGE-UNKNOWN 诊断（checkKind/foundValue＝被拒定位
/// 要素；引导动作＝§9.5 T13 行语义"使用 MDL-18/R2 通道"）。
void pushPackageUnknownDiag(std::vector<core::DiagnosticRecord>& diags, std::string checkKind,
                            std::string foundValue)
{
    diags.push_back(core::DiagnosticRecord::make(
        std::string(kMdlImportPackageUnknown),
        std::nullopt,                                   // 非本软件工件——无建模对象可定位
        std::nullopt,
        std::nullopt,
        "package-import",                               // context：导入门
        "工件未通过来源标识校验（check-kind=" + checkKind
            + ", found=" + foundValue + "）——仅识别本软件导出的规范工件（MDL-20）",
        "使用 MDL-18/R2 WorkCell 反向导入通道（R1 引导提示）"));
}

/// 构造 ModelingError（params 保序键值——确定性；detail 未经脱敏不得直达
/// 用户文案，ModelingError 契约同款约束）。
ModelingError makeError(ModelingErrorCode code, std::string detail,
                        std::vector<std::pair<std::string, std::string>> params = {})
{
    ModelingError error;
    error.code = code;
    error.detail = std::move(detail);
    error.params = std::move(params);
    return error;
}

/// io 失败→错误参数（io-code 键＝io 稳定码 token，io::errorCodeToken 取串；
/// io 侧 params 原样透传为 io-param-<n> 键——定位要素不丢）。
std::vector<std::pair<std::string, std::string>> ioErrorParams(const io::IoError& error)
{
    std::vector<std::pair<std::string, std::string>> params;
    params.emplace_back("io-code", std::string(io::errorCodeToken(error.code)));
    for (std::size_t i = 0; i < error.params.size(); ++i) {
        // io 侧 params 是键值对——逐对展开透传（键加 io- 前缀防与本端口
        // 自有键混淆——定位要素不丢，脱敏归 diagnostics 层）。
        params.emplace_back("io-" + error.params[i].first, error.params[i].second);
    }
    return params;
}

// =====================================================================
// 条目路径规则（包内布局——Package.hpp 文件头注；导入侧按本规则反解）
// =====================================================================

/// 根对象条目路径（固定——单根）。
constexpr std::string_view kRootEntryPath = "objects/robot-design/object.bin";

/// 部件对象条目路径（objects/<token>/<oid 规范文本>.bin——token 反解路由）。
std::string partEntryPath(std::string_view token, const core::ObjectId& oid)
{
    return std::string("objects/") + std::string(token) + "/" + oid.toCanonical() + ".bin";
}

/// Solidified 资源副本条目路径（resources/<resource 对象 oid>.bin——按对象
/// 身份编址而非 resourceId 字符串：路径安全，resourceId 仅入报告）。
std::string resourceEntryPath(const core::ObjectId& oid)
{
    return std::string("resources/") + oid.toCanonical() + ".bin";
}

/// 取建模对象变体的稳定身份（四部件备择均有 objectId 字段——ARC-04；
/// ★ RobotDesign 根对象无 objectId 成员（D-MDL-1——根对象随修订锚定，不是
/// 独立存储对象）：本函数只服务部件解引用，根分支防御性返回保留值）。
core::ObjectId objectIdOf(const ObjectVariant& variant)
{
    if (std::holds_alternative<ToolDefinition>(variant)) {
        return std::get<ToolDefinition>(variant).objectId;
    }
    if (std::holds_alternative<SceneObject>(variant)) {
        return std::get<SceneObject>(variant).objectId;
    }
    if (std::holds_alternative<PoseSet>(variant)) {
        return std::get<PoseSet>(variant).objectId;
    }
    if (std::holds_alternative<DrivetrainDesign>(variant)) {
        return std::get<DrivetrainDesign>(variant).objectId;
    }
    return core::ObjectId{};   // 根对象分支——不达（保留值语义，core §4.1 U-1）
}

/// 取建模对象变体的类型 token（路由/条目路径反解——ObjectTypes.hpp 词表）。
std::string_view objectTypeTokenOf(const ObjectVariant& variant)
{
    if (std::holds_alternative<RobotDesign>(variant)) {
        return kRobotDesignObjectType;    // runtime 权威常量（using 引入）
    }
    if (std::holds_alternative<ToolDefinition>(variant)) {
        return kToolDefinitionObjectType;
    }
    if (std::holds_alternative<SceneObject>(variant)) {
        return kSceneObjectObjectType;
    }
    if (std::holds_alternative<PoseSet>(variant)) {
        return kNamedPoseSetObjectType;
    }
    return kRobotDrivetrainObjectType;
}

}  // namespace

// =====================================================================
// roundtripChecklist——五组逐项核对清单（导出/导入共用唯一实现）
// =====================================================================

std::vector<PackageCheckItem> roundtripChecklist(const RobotDesign& design,
                                                 const std::vector<ObjectVariant>& parts)
{
    std::vector<PackageCheckItem> items;

    // ---- 组一：authority（权威参数化——模式＋逐关节权威字段）----
    // 双权威语义：Explicit 态 axis/origin 权威、dhDerived 为派生展示（不入
    // 编码身份——D-MDL-5）；StandardDH 态 dhDerived 权威、axis 为派生（编码
    // 后 NotProvided）。清单严格按"编码权威语义"取值——两侧同构。
    auto push = [&items](std::string group, std::string path, std::string value) {
        items.push_back(PackageCheckItem{std::move(group), std::move(path), std::move(value)});
    };
    push("authority", "root.mode", std::string(authorityModeToken(design.authority)));
    for (std::size_t i = 0; i < design.joints.size(); ++i) {
        const JointEntry& joint = design.joints[i];
        const std::string base = "root.joints[" + std::to_string(i) + "]";
        push("authority", base + ".type", std::string(jointTypeToken(joint.type)));
        // axis：Explicit 态为权威一等字段（清单承载）；StandardDH 态编码后
        // 为 NotProvided（派生待重算）——四态文本如实呈现两侧一致。
        push("authority", base + ".axis",
              fmtSourced(joint.axis, [](const rw::math::Vector3D<double>& v) {
                  return fmtVector(v);
              }));
        push("authority", base + ".origin",
              fmtSourced(joint.origin,
                         [](const JointPose& p) { return fmtJointPose(p); }));
        push("authority", base + ".zeroOffset",
              fmtDouble(joint.zeroOffset) + (joint.type == JointType::Revolute ? " rad" : " m"));
        // bounds：Revolute/Prismatic 权威；Continuous＝NotApplicable（I-MDL-4）。
        push("authority", base + ".bounds", fmtSourced(joint.bounds, fmtLimits));
        // workingRange：仅 Continuous 消费的分析属性（MDL-12），编码权威内。
        push("authority", base + ".workingRange", fmtSourced(joint.workingRange, fmtLimits));
        if (design.authority == AuthorityMode::StandardDH && joint.dhDerived.has_value()) {
            // DH 权威四参数逐项（rad·m——§7.4；派生展示值在 Explicit 态不入
            // 清单——D-MDL-5：两侧解码产物均无该值，比对面一致）。
            const DhParameters& dh = *joint.dhDerived;
            push("authority", base + ".dh.thetaOffset", fmtDouble(dh.thetaOffset) + " rad");
            push("authority", base + ".dh.d", fmtDouble(dh.d) + " m");
            push("authority", base + ".dh.a", fmtDouble(dh.a) + " m");
            push("authority", base + ".dh.alpha", fmtDouble(dh.alpha) + " rad");
        }
    }

    // ---- 组二：physics（物性——连杆＋工具的 BodyData 逐项）----
    auto pushBody = [&push](const std::string& base, const BodyData& body) {
        push("physics", base + ".mass",
             fmtSourced(body.mass, [](double v) { return fmtDouble(v) + " kg"; }));
        push("physics", base + ".centerOfMass",
             fmtSourced(body.centerOfMass, [](const rw::math::Vector3D<double>& v) {
                 return fmtVector(v) + " m";
             }));
        push("physics", base + ".inertia",
             fmtSourced(body.inertia, [](const InertiaTensor& t) {
                 // 六分量逐项（kg·m²；质心基准/连杆系参考姿态——M-2）。
                 return fmtDouble(t.ixx) + "," + fmtDouble(t.iyy) + "," + fmtDouble(t.izz)
                     + "," + fmtDouble(t.ixy) + "," + fmtDouble(t.ixz) + ","
                     + fmtDouble(t.iyz) + " kg·m²";
             }));
        if (body.material.has_value()) {
            push("physics", base + ".material.id", body.material->materialId);
            push("physics", base + ".material.density",
                 fmtSourced(body.material->density,
                            [](double v) { return fmtDouble(v) + " kg/m³"; }));
        } else {
            push("physics", base + ".material", "absent");
        }
    };
    for (std::size_t i = 0; i < design.links.size(); ++i) {
        pushBody("root.links[" + std::to_string(i) + "]", design.links[i].body);
    }

    // ---- 组三：resource（资源引用——状态机与内容身份）----
    // resourceId 键＋contentDigest（身份要素）＋状态；Recorded 态附加外部
    // 记录（absPath+digest），Solidified 态附加固化对象身份/版本。
    for (std::size_t i = 0; i < design.resourceManifest.size(); ++i) {
        const ResourceRef& resource = design.resourceManifest[i];
        const std::string base = "root.resources[" + std::to_string(i) + "]";
        push("resource", base + ".resourceId", resource.resourceId);
        push("resource", base + ".contentDigest", toHex(resource.contentDigest));
        push("resource", base + ".state", std::string(resourceStateToken(resource.state)));
        if (resource.externalRecord.has_value()) {
            push("resource", base + ".external.digest",
                 toHex(resource.externalRecord->recordedDigest));
            // absPath 为敏感值（NFR-SEC-01 路径只在引用记录层）——清单承载
            // 登记摘要而非路径原文（回读一致性由 digest 判定）。
        }
        if (resource.solidifiedObject.has_value()) {
            push("resource", base + ".solidified.oid",
                 resource.solidifiedObject->objectId.toCanonical());
            push("resource", base + ".solidified.cv",
                 resource.solidifiedObject->contentVersion.toCanonical());
        }
    }

    // ---- 组四：collision（碰撞规则——连杆碰撞几何＋场景碰撞画像提示）----
    // selfCollisionHints 不入编码权威（§4.3-B——设计使然），不在清单。
    for (std::size_t i = 0; i < design.links.size(); ++i) {
        const LinkEntry& link = design.links[i];
        const std::string base = "root.links[" + std::to_string(i) + "]";
        if (link.collision.has_value()) {
            push("collision", base + ".collision.ref", link.collision->resourceRefId);
            push("collision", base + ".collision.kind",
                 std::string(geometryKindToken(link.collision->kind)));
            push("collision", base + ".collision.localTransform",
                 fmtPose(link.collision->localTransform));
        } else {
            push("collision", base + ".collision", "absent");
        }
    }

    // ---- 部件对象组内定位：按对象身份字典序（集合语义——确定性序）----
    struct PartRef {
        std::string oid;               ///< 对象身份规范文本（排序键）
        const ObjectVariant* part;     ///< 部件对象（非 owning——入参存活期）
    };
    std::vector<PartRef> ordered;
    ordered.reserve(parts.size());
    for (const ObjectVariant& variant : parts) {
        PartRef ref;
        ref.oid = objectIdOf(variant).toCanonical();
        ref.part = &variant;
        ordered.push_back(std::move(ref));
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const PartRef& a, const PartRef& b) { return a.oid < b.oid; });

    // 部件分组吸收进五组：工具→physics/collision、场景→collision/pose 外的
    // 画像提示、位姿集→pose、传动→physics（摩擦/力矩限值——MDL-16 输入层）。
    for (const PartRef& ref : ordered) {
        const ObjectVariant& variant = *ref.part;
        if (const auto* tool = std::get_if<ToolDefinition>(&variant)) {
            const std::string base = "tool[" + ref.oid + "]";
            push("authority", base + ".localName", tool->localName);
            push("authority", base + ".mountInterface", fmtPose(tool->mountInterface));
            for (std::size_t i = 0; i < tool->tcpList.size(); ++i) {
                push("authority", base + ".tcp[" + std::to_string(i) + "].key",
                     tool->tcpList[i].key);
                push("authority", base + ".tcp[" + std::to_string(i) + "].offset",
                     fmtPose(tool->tcpList[i].offset));
            }
            pushBody(base, tool->body);
            if (tool->geometry.has_value()) {
                push("collision", base + ".geometry.ref", tool->geometry->resourceRefId);
            }
        } else if (const auto* scene = std::get_if<SceneObject>(&variant)) {
            const std::string base = "scene[" + ref.oid + "]";
            push("authority", base + ".localName", scene->localName);
            push("authority", base + ".worldPose", fmtPose(scene->worldPose));
            push("collision", base + ".role", std::string(sceneObjectRoleToken(scene->role)));
            if (scene->geometry.has_value()) {
                push("collision", base + ".geometry.ref", scene->geometry->resourceRefId);
            }
            if (scene->collisionProfileHint.has_value()) {
                // 碰撞规则组（Codec 编码 scene 行 collisionProfileHint——
                // 导入的自碰撞分组提示，仅报告用途）。
                push("collision", base + ".collisionProfileHint",
                     *scene->collisionProfileHint);
            }
        } else if (const auto* poseSet = std::get_if<PoseSet>(&variant)) {
            // 命名位姿组（§6.8"语义保留范围含命名位姿"——MDL-20 保留面；
            // 条目按 key 字典序已由解码规范化，逐条目 key/配置/备注）。
            for (std::size_t i = 0; i < poseSet->entries.size(); ++i) {
                const PoseSetEntry& entry = poseSet->entries[i];
                const std::string base = "pose.entries[" + std::to_string(i) + "]";
                push("pose", base + ".key", entry.key);
                std::string config;
                for (std::size_t k = 0; k < entry.jointConfiguration.size(); ++k) {
                    if (k != 0) { config += ","; }
                    config += fmtDouble(entry.jointConfiguration[k]);
                }
                push("pose", base + ".jointConfiguration", config + " rad/m");
                push("pose", base + ".note", entry.note);
            }
        } else if (const auto* drivetrain = std::get_if<DrivetrainDesign>(&variant)) {
            const std::string base = "drivetrain[" + ref.oid + "]";
            // 传动设计逐项（MDL-16 输入层）：传动比/摩擦/力矩限值按关节序
            //（有序不排序）；R1 下 coupling 禁止配置（I-MDL-12）——存在性
            // 如实承载（存在即另一层面的违例，清单不掩盖事实）。
            for (std::size_t i = 0; i < drivetrain->ratioPerJoint.size(); ++i) {
                push("physics", base + ".ratio[" + std::to_string(i) + "]",
                     fmtSourced(drivetrain->ratioPerJoint[i],
                                [](double v) { return fmtDouble(v) + " 1"; }));
            }
            push("physics", base + ".coupling",
                 drivetrain->coupling.has_value() ? "present" : "absent");
            for (std::size_t i = 0; i < drivetrain->frictionPerJoint.size(); ++i) {
                const FrictionEntry& friction = drivetrain->frictionPerJoint[i];
                const std::string fbase = base + ".friction[" + std::to_string(i) + "]";
                push("physics", fbase + ".viscous",
                     fmtSourced(friction.viscous,
                                [](double v) { return fmtDouble(v) + " N·m·s/rad|N·s/m"; }));
                push("physics", fbase + ".coulomb",
                     fmtSourced(friction.coulomb,
                                [](double v) { return fmtDouble(v) + " N·m|N"; }));
                push("physics", fbase + ".bias",
                     fmtSourced(friction.bias,
                                [](double v) { return fmtDouble(v) + " N·m|N"; }));
            }
            for (std::size_t i = 0; i < drivetrain->torqueLimitsPerJoint.size(); ++i) {
                const TorqueLimitEntry& torque = drivetrain->torqueLimitsPerJoint[i];
                const std::string tbase = base + ".torqueLimit[" + std::to_string(i) + "]";
                push("physics", tbase + ".rated",
                     fmtSourced(torque.rated, [](double v) { return fmtDouble(v) + " N·m|N"; }));
                push("physics", tbase + ".peak",
                     fmtSourced(torque.peak, [](double v) { return fmtDouble(v) + " N·m|N"; }));
            }
        }
    }

    return items;
}

// =====================================================================
// ModelPackagePort::exportPackage——导出（§6.8 原子写出链路）
// =====================================================================

PackageExportOutcome
    ModelPackagePort::exportPackage(const ObjectClosureView& closure,
                                    const PackageExportTarget& target,
                                    std::vector<core::DiagnosticRecord>& diags) const
{
    PackageExportOutcome outcome;
    RobotDesignCodec codec;   // 无状态编解码器（栈持有——每调用独立）

    // ---- ① 取根对象并解码（闭包缺根＝调用方数据错误——RefMissing 值面，
    //         不落诊断：诊断纪律见 Package.hpp 接口注）----
    const std::optional<ClosureObject> rootClosure =
        closure.tryObjectByToken(kRobotDesignObjectType);
    if (!rootClosure.has_value()) {
        outcome.error = makeError(ModelingErrorCode::RefMissing,
                                  "闭包内无 robot-design 根对象——导出须以含根对象的修订闭包为输入",
                                  {{"object-type", "robot-design"}});
        return outcome;
    }
    auto rootDecoded = codec.decode(rootClosure->bytes, kCurrentFormatVersion);
    if (!rootDecoded.ok()) {
        outcome.error = rootDecoded.error();   // 解码失败原样透传（MalformedPayload 族）
        return outcome;
    }
    const RobotDesign& design = std::get<RobotDesign>(rootDecoded.get());

    // 根对象条目（canonical 字节原样入包——CON-01/PA-2；固定路径单根）。
    std::vector<PackageEntry> entries;
    entries.push_back(makeEntry(std::string(kRootEntryPath), rootClosure->bytes));

    // ---- ② 解引用部件对象（canonical 字节原样入包——CON-01/PA-2 不重编码；
    //         同时解码做清单装配与引用完整性预检）----
    std::vector<ObjectVariant> parts;

    auto fetchPart = [&](const core::ObjectId& oid, const char* refKind) -> bool {
        const std::optional<ClosureObject> part = closure.tryObject(oid);
        if (!part.has_value()) {
            outcome.error = makeError(ModelingErrorCode::RefMissing,
                                      std::string("闭包内缺失被引") + refKind + "对象",
                                      {{"object-id", oid.toCanonical()}});
            return false;
        }
        auto decoded = codec.decode(part->bytes, kCurrentFormatVersion);
        if (!decoded.ok()) {
            outcome.error = decoded.error();
            return false;
        }
        ObjectVariant variant = decoded.get();
        const core::ObjectId decodedOid = objectIdOf(variant);
        if (!(decodedOid == oid)) {
            // 字节身份与引用定位不一致＝闭包数据违例（ARC-04——身份路由
            // 失配属数据错误面，值面拒绝）。
            outcome.error = makeError(ModelingErrorCode::RefMissing,
                                      "被引对象的解码身份与请求身份不一致",
                                      {{"object-id", oid.toCanonical()},
                                       {"decoded-id", decodedOid.toCanonical()}});
            return false;
        }
        // token 复核（ClosureObject.objectTypeToken 与解码产物类型一致——
        // CanonicalBridge builder 同款预演，路由失配不静默）。
        if (part->objectTypeToken != objectTypeTokenOf(variant)) {
            outcome.error = makeError(ModelingErrorCode::RefMissing,
                                      "被引对象的存储类型 token 与解码产物不一致",
                                      {{"object-id", oid.toCanonical()},
                                       {"stored-token", part->objectTypeToken},
                                       {"decoded-token", std::string(objectTypeTokenOf(variant))}});
            return false;
        }
        entries.push_back(makeEntry(partEntryPath(objectTypeTokenOf(variant), oid),
                                    part->bytes));
        parts.push_back(std::move(variant));
        return true;
    };

    for (const core::ObjectId& toolOid : design.toolRefs) {
        if (!fetchPart(toolOid, "tool-definition")) { return outcome; }
    }
    for (const core::ObjectId& sceneOid : design.sceneRefs) {
        if (!fetchPart(sceneOid, "scene-object")) { return outcome; }
    }
    if (design.poseSetRef.has_value()) {
        // 命名位姿集（MDL-17/MDL-20：语义保留范围含命名位姿——位姿集随包
        // 保留，roundtrip 后逐条目一致）。
        if (!fetchPart(*design.poseSetRef, "named-pose-set")) { return outcome; }
    }
    if (design.drivetrainRef.has_value()) {
        if (!fetchPart(*design.drivetrainRef, "robot-drivetrain")) { return outcome; }
    }

    // ---- ③ Solidified 资源副本（Recorded 态不复制——absPath 保留在根对象
    //         字节内；Solidified 态逐个把固化 resource 对象字节入包）----
    for (const ResourceRef& resource : design.resourceManifest) {
        if (resource.state != ResourceState::Solidified) { continue; }
        if (!resource.solidifiedObject.has_value()) {
            // I-MDL-10：Solidified 必带固化引用——缺失＝闭包数据违例。
            outcome.error = makeError(ModelingErrorCode::RefMissing,
                                      "Solidified 资源缺少固化对象引用（I-MDL-10）",
                                      {{"resource-id", resource.resourceId}});
            return outcome;
        }
        const core::ObjectId resourceOid = resource.solidifiedObject->objectId;
        const std::optional<ClosureObject> resourceObject = closure.tryObject(resourceOid);
        if (!resourceObject.has_value()) {
            outcome.error = makeError(ModelingErrorCode::RefMissing,
                                      "闭包内缺失 Solidified 资源对象字节",
                                      {{"resource-id", resource.resourceId},
                                       {"object-id", resourceOid.toCanonical()}});
            return outcome;
        }
        entries.push_back(makeEntry(resourceEntryPath(resourceOid), resourceObject->bytes));
    }

    // ---- ④ manifest 装配（对象清单按路径字典序——确定性；条目路径重复
    //         ＝闭包身份违例，防御性拒绝）----
    std::sort(entries.begin(), entries.end(),
              [](const PackageEntry& a, const PackageEntry& b) { return a.path < b.path; });
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (!(entries[i - 1].path < entries[i].path)) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "包条目路径重复（对象身份应唯一）");
            return outcome;
        }
    }
    std::vector<ManifestItem> manifestItems;
    manifestItems.reserve(entries.size());
    for (const PackageEntry& entry : entries) {
        manifestItems.push_back(
            ManifestItem{entry.path, entry.bytes.size(), entry.digest});
    }
    const io::JsonValue manifestValue =
        buildManifestValue(manifestItems, target.createdAtUtcIso8601);
    const io::JsonWriteOptions canonicalOptions{};   // null profile＝键名字典序
    // canonicalizeJson 的入参是整个文档（JsonDocument＝根值＋解析报告面）
    // ——程序化构造的报告为缺省值（观测面不参与语义）。
    io::JsonDocument manifestDocument;
    manifestDocument.root = manifestValue;
    auto manifestBytes = io::canonicalizeJson(manifestDocument, canonicalOptions);
    if (!manifestBytes) {
        outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                  "manifest canonical 化失败（非有限数值？）");
        return outcome;
    }
    // manifest 自身摘要（对 canonical 字节——导入侧重开自检同源比对）。
    outcome.manifestDigest = digestBytes(std::vector<std::uint8_t>(
        manifestBytes.value.begin(), manifestBytes.value.end()));
    // manifest 也是包内条目（不在自身清单内——清单只承载对象/资源条目）。
    entries.push_back(makeEntry(std::string(kModelPackageManifestEntry),
                                std::vector<std::uint8_t>(manifestBytes.value.begin(),
                                                          manifestBytes.value.end())));
    // 全条目最终按路径字典序排序（manifest.json 的 'm' 排在 'o'/'r' 前——
    // 字典序自然落位，不依赖书写序）。
    std::sort(entries.begin(), entries.end(),
              [](const PackageEntry& a, const PackageEntry& b) { return a.path < b.path; });

    // ---- ⑤ ZIP 容器装配（STORED 确定性容器——实现注①）----
    const std::vector<std::uint8_t> zipBytes = buildZipContainer(entries);

    // ---- ⑥ 临时区暂存＋预提交自检（io §7.2 导出协议同构：临时区组装→
    //         压缩→⑤完整性自检→原子替换；"临时区→校验→替换"的 ACC2 口径）----
    // 自检须以 IZipChannel 重开暂存文件（IO-T04 读侧权威）；io AtomicTarget
    // 在 commit 前独占持有暂存句柄（libzip 打不开）——故自检对象是**临时区
    // 内**的已闭合暂存文件（io ITempAreaManager PackExport 会话根），而非
    // 目标旁的 AtomicTarget 暂存位。目标文件只在最后一步被原子替换触碰。
    io::ITempAreaManagerPtr tempAreas = io::makeTempAreaManager();
    io::TempAreaSpec tempSpec;
    tempSpec.role = io::TempAreaRole::PackExport;   // 导出暂存区（目标同目录→同卷）
    tempSpec.baseDir = target.targetFile.parent_path();
    tempSpec.nameHint = target.targetFile.stem().string();
    auto tempSession = tempAreas->create(tempSpec, nullptr);
    if (!tempSession) {
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlExportFailed), std::nullopt, std::nullopt, std::nullopt,
            "package-export@temp-area",
            "导出暂存区创建失败：" + tempSession.error.detail,
            "项目状态不变且旧输出文件完好；检查目标路径/预算后重试（MDL-20）"));
        outcome.error = makeError(ModelingErrorCode::ExportFailed,
                                  std::move(tempSession.error.detail),
                                  ioErrorParams(tempSession.error));
        return outcome;
    }
    io::TempAreaSession session = tempSession.value;
    // 统一失败出口：清理临时区（幂等）→产诊断→返回错误（目标从未被触碰）。
    auto failExport = [&](std::string stage, io::IoError error) {
        (void)tempAreas->cleanup(session);
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kMdlExportFailed), std::nullopt, std::nullopt, std::nullopt,
            "package-export@" + stage,
            "导出在 " + stage + " 段失败：" + error.detail,
            "项目状态不变且旧输出文件完好；检查目标路径/预算后重试（MDL-20）"));
        outcome.error = makeError(ModelingErrorCode::ExportFailed, std::move(error.detail),
                                  ioErrorParams(error));
    };

    // ---- ⑥a 暂存文件写入（暂存位经 IAtomicFileWriter 原子就位——本路径
    //         在本会话临时区内，不触碰用户目标；写完句柄闭合，可被自检重开）----
    const std::filesystem::path stagingFile = session.rootPath() / "package.zip";
    {
        io::IAtomicFileWriterPtr stagingWriter = io::makeAtomicFileWriter();
        auto stagingPrepared = stagingWriter->prepare(stagingFile, io::ReplacePolicy::NeverOverwrite);
        if (!stagingPrepared) {
            failExport("staging-prepare", stagingPrepared.error);
            return outcome;
        }
        io::AtomicTarget stagingTarget = stagingPrepared.value;
        if (auto written = stagingTarget.write(std::string_view(
                reinterpret_cast<const char*>(zipBytes.data()), zipBytes.size()));
            !written) {
            failExport("staging-write", written.error);
            return outcome;
        }
        if (auto committed = stagingWriter->commit(stagingTarget); !committed) {
            failExport("staging-commit", committed.error);
            return outcome;
        }
    }

    // ---- ⑥b 预提交自检（§7.2⑤）：对暂存文件重开 IZipChannel，逐条目复算
    //         哈希＋manifest 自校验——"写已知、读权威"闭环。自检不绿＝拒绝
    //         替换目标（先前输出完好，V-29）。----
    {
        auto channel = io::openZipChannel(stagingFile);
        if (!channel) {
            failExport("self-check", channel.error);
            return outcome;
        }
        std::vector<io::ZipManifestEntry> verifyList;
        verifyList.reserve(manifestItems.size());
        for (const ManifestItem& item : manifestItems) {
            verifyList.push_back(
                io::ZipManifestEntry{item.path, item.size, item.sha256});
        }
        auto verified = channel.value->verifyManifestEntries(verifyList, nullptr, {}, nullptr);
        if (!verified) {
            failExport("self-check", verified.error);
            return outcome;
        }
        for (const io::ZipEntryVerifyReport& report : verified.value) {
            if (report.error.code != io::IoErrorCode::Ok) {
                // 条目级失败（缺失/大小/哈希不符）＝自检不绿——拒绝提交。
                failExport("self-check", report.error);
                return outcome;
            }
        }
        // manifest 字节自校验（重读比对 manifestDigest——manifest 未被容器
        // 层改写的直接证据）。
        auto manifestRead = channel.value->readEntryBytes(
            io::IoString(kModelPackageManifestEntry), nullptr, {}, nullptr);
        if (!manifestRead) {
            failExport("self-check", manifestRead.error);
            return outcome;
        }
        const core::Digest256 manifestDigest =
            digestBytes(std::vector<std::uint8_t>(manifestRead.value.begin(),
                                                  manifestRead.value.end()));
        if (!(manifestDigest == outcome.manifestDigest)) {
            io::IoError mismatch;
            mismatch.code = io::IoErrorCode::Ok;   // 占位（仅用 detail 通道）
            mismatch.detail = "manifest 重读摘要与装配摘要不一致";
            failExport("self-check", std::move(mismatch));
            return outcome;
        }
    }

    // ---- ⑦ 目标原子替换（io AtomicFile prepare→写入→commit——Windows 同
    //         卷原子替换；失败即 abort，先前输出完好）----
    {
        io::IAtomicFileWriterPtr writer = io::makeAtomicFileWriter();
        auto prepared = writer->prepare(target.targetFile, target.replace);
        if (!prepared) {
            // 目标路径不可写/父目录缺失/NeverOverwrite 命中等环境面失败。
            failExport("prepare", prepared.error);
            return outcome;
        }
        io::AtomicTarget atomicTarget = prepared.value;
        if (auto written = atomicTarget.write(
                std::string_view(reinterpret_cast<const char*>(zipBytes.data()),
                                 zipBytes.size()));
            !written) {
            failExport("write", written.error);
            return outcome;
        }
        if (auto committed = writer->commit(atomicTarget); !committed) {
            failExport("commit", committed.error);
            return outcome;
        }
    }

    // ---- ⑧ 清理临时区（幂等；清理失败仅降级登记——目标已正确就位）----
    (void)tempAreas->cleanup(session);

    outcome.ok = true;
    outcome.entryCount = entries.size();
    outcome.totalBytes = 0;
    for (const PackageEntry& entry : entries) {
        outcome.totalBytes += entry.bytes.size();
    }
    return outcome;
}

// =====================================================================
// ModelPackagePort::importPackage——导入（三道校验：来源/schema/清单哈希）
// =====================================================================

PackageImportOutcome
    ModelPackagePort::importPackage(const ValidatedSource& source,
                                    std::vector<core::DiagnosticRecord>& diags) const
{
    PackageImportOutcome outcome;
    RobotDesignCodec codec;

    // ---- ① 打开 ZIP 通道（IO-T04 落位面——modeling 不自行读文件，SA-14：
    //         包文件唯一经 io openZipChannel 于已验证路径打开）----
    // finalPath 空＝调用方未装配 io 产物——fail-closed 按未知工件拒绝
    // （不猜测、不兜底读文件系统）。
    if (source.entrySnapshot.finalPath.empty()) {
        pushPackageUnknownDiag(diags, "container", "empty-final-path");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  "ValidatedSource 未携带已验证包文件路径（entrySnapshot.finalPath 空）");
        return outcome;
    }
    auto channel = io::openZipChannel(source.entrySnapshot.finalPath);
    if (!channel) {
        // 打不开（不存在/不可读/非 ZIP 容器）＝不识别的工件——引导换通道。
        pushPackageUnknownDiag(diags, "container",
                               std::string(io::errorCodeToken(channel.error.code)));
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  std::move(channel.error.detail),
                                  ioErrorParams(channel.error));
        return outcome;
    }
    io::IZipChannel* zip = channel.value.get();

    auto listEntries = zip->listEntries();
    if (!listEntries) {
        pushPackageUnknownDiag(diags, "container", "listing-failed");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  std::move(listEntries.error.detail),
                                  ioErrorParams(listEntries.error));
        return outcome;
    }
    const std::vector<io::ZipEntryInfo> archiveEntries = listEntries.value;

    // ---- ② manifest 读取与来源标识/schema 校验（fail-closed：任何"不是
    //         本软件工件"的字节不得被解释——§6.8 导入门）----
    auto readManifest = zip->readEntryBytes(io::IoString(kModelPackageManifestEntry),
                                            nullptr, {}, nullptr);
    if (!readManifest) {
        pushPackageUnknownDiag(diags, "manifest-missing", "manifest.json-absent");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  std::move(readManifest.error.detail),
                                  ioErrorParams(readManifest.error));
        return outcome;
    }
    auto reader = io::makeStructuredDataReader(nullptr);
    auto manifestDoc = reader->parseBytes(
        std::string_view(readManifest.value.data(), readManifest.value.size()),
        io::JsonReadOptions{},   // null profileId＝仅语法/安全层——包格式语义
                                 // 校验（来源/schema/字段形）是本单元自有职责
        nullptr, nullptr);
    if (!manifestDoc) {
        pushPackageUnknownDiag(diags, "manifest-unreadable", "json-syntax");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  std::move(manifestDoc.error.detail),
                                  ioErrorParams(manifestDoc.error));
        return outcome;
    }
    const io::JsonValue& manifestRoot = manifestDoc.value.root;

    auto requireString = [](const io::JsonValue& object, const char* key,
                            std::string* out) -> bool {
        const io::JsonValue* member = object.findMember(key);
        if (member == nullptr || !member->isString()) { return false; }
        *out = member->stringValue;
        return true;
    };

    if (!manifestRoot.isObject()) {
        pushPackageUnknownDiag(diags, "manifest-unreadable", "root-not-object");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  "manifest 根不是 JSON 对象");
        return outcome;
    }
    std::string formatId;
    if (!requireString(manifestRoot, "formatId", &formatId)
        || formatId != kModelPackageFormatId) {
        // 格式标识不符＝异构工件（如 .rwpack 或任意 zip）——引导 MDL-18/R2。
        pushPackageUnknownDiag(diags, "format-id", formatId);
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  "formatId 不是 " + std::string(kModelPackageFormatId)
                                      + "——非本软件规范工件");
        return outcome;
    }
    std::string producer;
    if (!requireString(manifestRoot, "producer", &producer)
        || producer != kModelPackageProducer) {
        pushPackageUnknownDiag(diags, "producer", producer);
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  "producer 不是 " + std::string(kModelPackageProducer)
                                      + "——非本软件导出的规范工件");
        return outcome;
    }
    const io::JsonValue* schemaVersion = manifestRoot.findMember("schemaVersion");
    if (schemaVersion == nullptr || !schemaVersion->isInteger()) {
        pushPackageUnknownDiag(diags, "schema-version", "missing-or-not-integer");
        outcome.error = makeError(ModelingErrorCode::PackageUnknown,
                                  "manifest 缺少 schemaVersion 或其类型非法");
        return outcome;
    }
    if (schemaVersion->integerValue > kModelPackageSchemaVersion) {
        // 未来版本包：是"本软件工件但版本超前"——值面拒绝引导升级程序，
        // 不产诊断（MDL-READINESS-SCHEMA-UNSUPPORTED 语义是"对象 schema
        // 主版本"，包格式版本不入其义——Package.hpp 接口注）。
        outcome.error = makeError(
            ModelingErrorCode::SchemaVersionUnsupported,
            "包格式 schemaVersion 高于本程序支持",
            {{"schema-version", std::to_string(schemaVersion->integerValue)},
             {"supported", std::to_string(kModelPackageSchemaVersion)}});
        return outcome;
    }
    if (schemaVersion->integerValue != kModelPackageSchemaVersion) {
        // 低于当前值的旧版本：本格式自 v1 起登记、无历史版本——按破损拒。
        outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                  "包格式 schemaVersion 非当前支持值",
                                  {{"schema-version", std::to_string(schemaVersion->integerValue)}});
        return outcome;
    }

    // ---- ③ manifest 清单解析（结构破损＝MalformedPayload——工件"像我们
    //         的"但内容非法）----
    const io::JsonValue* entriesValue = manifestRoot.findMember("entries");
    if (entriesValue == nullptr || !entriesValue->isArray()) {
        outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                  "manifest 缺少 entries 数组");
        return outcome;
    }
    std::vector<io::ZipManifestEntry> manifestEntries;
    for (const io::JsonValue& item : entriesValue->items) {
        if (!item.isObject()) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "manifest entries 条目不是对象");
            return outcome;
        }
        std::string path;
        std::string sha256Text;
        const io::JsonValue* sizeValue = item.findMember("size");
        if (!requireString(item, "path", &path)
            || !requireString(item, "sha256", &sha256Text)
            || sizeValue == nullptr || !sizeValue->isInteger()
            || sizeValue->integerValue < 0) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "manifest 条目字段缺失或类型非法（path/sha256/size）");
            return outcome;
        }
        const std::string shaPrefix = "sha256-";
        if (sha256Text.rfind(shaPrefix, 0) != 0) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "manifest 条目 sha256 缺少 sha256- 前缀");
            return outcome;
        }
        auto digest = fromHex(std::string_view(sha256Text).substr(shaPrefix.size()));
        if (!digest.has_value()) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "manifest 条目 sha256 非合法小写 hex");
            return outcome;
        }
        manifestEntries.push_back(
            io::ZipManifestEntry{path, static_cast<std::uint64_t>(sizeValue->integerValue),
                                 *digest});
    }

    // ---- ④ 归档条目集与清单精确比对（多/少条目＝容器与清单失配）----
    {
        std::vector<std::string> archiveNames;
        archiveNames.reserve(archiveEntries.size());
        for (const io::ZipEntryInfo& info : archiveEntries) {
            if (info.name == kModelPackageManifestEntry) {
                continue;   // manifest 自身不入自身清单——比对前排除（固定名）
            }
            archiveNames.push_back(info.name);
        }
        std::sort(archiveNames.begin(), archiveNames.end());
        std::vector<std::string> manifestPaths;
        manifestPaths.reserve(manifestEntries.size());
        for (const io::ZipManifestEntry& entry : manifestEntries) {
            manifestPaths.push_back(entry.path);
        }
        std::sort(manifestPaths.begin(), manifestPaths.end());
        // 归档额外条目＝可疑注入（清单外内容不得进入业务层——fail-closed）。
        std::vector<std::string> extra;
        std::set_difference(archiveNames.begin(), archiveNames.end(),
                            manifestPaths.begin(), manifestPaths.end(),
                            std::back_inserter(extra));
        std::vector<std::string> missing;
        std::set_difference(manifestPaths.begin(), manifestPaths.end(),
                            archiveNames.begin(), archiveNames.end(),
                            std::back_inserter(missing));
        if (!extra.empty() || !missing.empty()) {
            std::string detail = "归档条目与 manifest 清单失配";
            if (!extra.empty()) { detail += "；清单外条目 " + extra.front(); }
            if (!missing.empty()) { detail += "；清单缺失条目 " + missing.front(); }
            outcome.error = makeError(ModelingErrorCode::MalformedPayload, detail,
                                      {{"archive-count", std::to_string(archiveNames.size())},
                                       {"manifest-count", std::to_string(manifestPaths.size())}});
            return outcome;
        }
    }

    // ---- ⑤ 逐条目哈希复算（容器层权威——libzip 解压＋core ContentDigester
    //         SHA-256；任一条目不符＝内容被改/损坏）----
    auto verified = zip->verifyManifestEntries(manifestEntries, nullptr, {}, nullptr);
    if (!verified) {
        outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                  std::move(verified.error.detail),
                                  ioErrorParams(verified.error));
        return outcome;
    }
    for (const io::ZipEntryVerifyReport& report : verified.value) {
        if (report.error.code != io::IoErrorCode::Ok) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      std::move(report.error.detail),
                                      ioErrorParams(report.error));
            return outcome;
        }
    }

    // ---- ⑥ 逐条目读取与解码装配（按包内路径序——确定性报告序）----
    auto readExact = [&](const std::string& path,
                         std::vector<std::uint8_t>* out) -> bool {
        auto bytes = zip->readEntryBytes(path, nullptr, {}, nullptr);
        if (!bytes) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      std::move(bytes.error.detail),
                                      ioErrorParams(bytes.error));
            return false;
        }
        out->assign(bytes.value.begin(), bytes.value.end());
        return true;
    };

    // 根对象（固定条目路径；token 路由反解失败＝包布局破损）。
    std::vector<std::uint8_t> rootBytes;
    if (!readExact(std::string(kRootEntryPath), &rootBytes)) { return outcome; }
    auto rootDecoded = codec.decode(rootBytes, kCurrentFormatVersion);
    if (!rootDecoded.ok()) {
        outcome.error = rootDecoded.error();
        return outcome;
    }
    if (!std::holds_alternative<RobotDesign>(rootDecoded.get())) {
        outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                  "根条目解码产物不是 RobotDesign");
        return outcome;
    }
    outcome.report.design = std::get<RobotDesign>(std::move(rootDecoded.get()));
    const RobotDesign& design = outcome.report.design;

    // 部件/资源条目（按归档序遍历 objects/ 与 resources/——归档序＝我们
    // 写出的路径字典序，确定性报告序的来源）。
    std::vector<std::string> expectedPaths;
    for (const io::ZipManifestEntry& entry : manifestEntries) {
        if (entry.path != kModelPackageManifestEntry) {
            expectedPaths.push_back(entry.path);
        }
    }
    // 期望路径集＝按根对象引用表重建（与导出同源规则），与归档清单比对
    // ——任何包内对象不被根引用＝孤儿条目（篡改面，拒绝）。
    {
        std::vector<std::string> wanted;
        wanted.push_back(std::string(kRootEntryPath));
        for (const core::ObjectId& oid : design.toolRefs) {
            wanted.push_back(partEntryPath(kToolDefinitionObjectType, oid));
        }
        for (const core::ObjectId& oid : design.sceneRefs) {
            wanted.push_back(partEntryPath(kSceneObjectObjectType, oid));
        }
        if (design.poseSetRef.has_value()) {
            wanted.push_back(partEntryPath(kNamedPoseSetObjectType, *design.poseSetRef));
        }
        if (design.drivetrainRef.has_value()) {
            wanted.push_back(partEntryPath(kRobotDrivetrainObjectType, *design.drivetrainRef));
        }
        for (const ResourceRef& resource : design.resourceManifest) {
            if (resource.state == ResourceState::Solidified
                && resource.solidifiedObject.has_value()) {
                wanted.push_back(resourceEntryPath(resource.solidifiedObject->objectId));
            }
        }
        std::sort(wanted.begin(), wanted.end());
        std::sort(expectedPaths.begin(), expectedPaths.end());
        if (wanted != expectedPaths) {
            outcome.error = makeError(
                ModelingErrorCode::MalformedPayload,
                "包内对象条目集与根对象引用表不一致（孤儿条目或缺引用）");
            return outcome;
        }
    }

    // 逐部件读取＋解码（同导出侧的 token/身份复核——回读与写入互为镜像）。
    auto decodePart = [&](const std::string& path, const core::ObjectId& oid) -> bool {
        std::vector<std::uint8_t> bytes;
        if (!readExact(path, &bytes)) { return false; }
        auto decoded = codec.decode(bytes, kCurrentFormatVersion);
        if (!decoded.ok()) {
            outcome.error = decoded.error();
            return false;
        }
        ObjectVariant variant = decoded.get();
        if (!(objectIdOf(variant) == oid)) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "部件条目解码身份与路径编址不一致",
                                      {{"entry", path}});
            return false;
        }
        outcome.report.parts.push_back(std::move(variant));
        return true;
    };
    for (const core::ObjectId& oid : design.toolRefs) {
        if (!decodePart(partEntryPath(kToolDefinitionObjectType, oid), oid)) { return outcome; }
    }
    for (const core::ObjectId& oid : design.sceneRefs) {
        if (!decodePart(partEntryPath(kSceneObjectObjectType, oid), oid)) { return outcome; }
    }
    if (design.poseSetRef.has_value()) {
        if (!decodePart(partEntryPath(kNamedPoseSetObjectType, *design.poseSetRef),
                        *design.poseSetRef)) {
            return outcome;
        }
    }
    if (design.drivetrainRef.has_value()) {
        if (!decodePart(partEntryPath(kRobotDrivetrainObjectType, *design.drivetrainRef),
                        *design.drivetrainRef)) {
            return outcome;
        }
    }

    // Solidified 资源副本（字节不透明——digest 已由清单校验背书，原样交还
    // 调用方按身份重新登记，CON-03 回读面）。
    for (const ResourceRef& resource : design.resourceManifest) {
        if (resource.state != ResourceState::Solidified) { continue; }
        if (!resource.solidifiedObject.has_value()) {
            outcome.error = makeError(ModelingErrorCode::MalformedPayload,
                                      "Solidified 资源缺少固化对象引用（I-MDL-10）",
                                      {{"resource-id", resource.resourceId}});
            return outcome;
        }
        SolidifiedResourceCopy copy;
        copy.resourceId = resource.resourceId;
        copy.objectId = resource.solidifiedObject->objectId;
        if (!readExact(resourceEntryPath(copy.objectId), &copy.bytes)) { return outcome; }
        outcome.report.solidifiedResources.push_back(std::move(copy));
    }

    // ---- ⑦ 逐项清单（与导出侧同源唯一实现——V-20 核对形态）----
    outcome.report.checkItems = roundtripChecklist(outcome.report.design, outcome.report.parts);

    outcome.ok = true;
    return outcome;
}

}  // namespace sdurws::ird::modeling
