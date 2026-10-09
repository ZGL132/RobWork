/**
 * @file   Settings.cpp
 * @brief  用户级设置存储实现（PM-14——§7.7）：canonical JSON 经 io
 *         通道的读入与原子写出，损坏安全（诊断＋缺省默认值）。
 *
 * 设计依据：
 *   - units/workflow.md §7.7/§10.2/§10.3（Settings.hpp 头注逐条承接，
 *     此处不重复——本文件注释聚焦**实现序**与每步的失败语义落点）
 *   - units/io.md §5.9.2（JsonProfile 声明式校验——版本判定先于 schema、
 *     必填不注默认值、未知键 Reject）、§5.9.3（canonical 写出——声明序
 *     键序＋to_chars 最短数值＋2 空格缩进＋LF＋UTF-8 无 BOM）、§9.5
 *     （makeStructuredDataReader/makeJsonWriter 工厂）
 *   - 任务契约 tasks/foundation/WP-22-T10.json acceptance 1~3（JSON
 *     canonical 经 io 写出；损坏给诊断不崩溃；P-WF-5 安全默认）
 *
 * 实现总览（load/store 两路径的步骤分解——与类注契约一一对应）：
 *   - load()：①文件存在性预检（不存在＝首次使用→默认值零诊断）→
 *     ②io reader 按 profile 解析（版本判定＋结构校验一气呵成）→
 *     ③逐字段映射（可选键缺失＝"无此事实"，非缺省——io 不注入默认值，
 *     业务侧自判）→ ④容量防御性截断（P-WF-5 安全默认＋诊断留痕）。
 *     任何一步失败→Dev 诊断留痕→返回缺省默认值（零异常零崩溃）。
 *   - store()：①组装受限 DOM（编程构造——span 全零＝无源定位）→
 *     ②父目录自持创建（用户设置目录是产品自有面，非项目目录——无
 *     "不动项目"红线；io 设施不代建目录，调用方即本类）→ ③io writer
 *     canonical 写出（文件目标＝同目录暂存＋rename 原子替换）。任何
 *     一步失败→Dev 诊断留痕→WorkflowError 上抛（io 码与 detail 原样
 *     透传——零吞错；先前文件经原子语义原样保留）。
 *
 * 线程约束：会话内单线程（Settings.hpp §10.3 口径）。
 */

#include "sdurws/ird/workflow/Settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <utility>

#include <sdurws/ird/io/IoError.hpp>  // io::IoError（错误三要素——透传入异常消息）
#include <sdurws/ird/io/IoFwd.hpp>    // io::IoResult（读通道返回容器）
#include <sdurws/ird/io/Json.hpp>     // io JSON 通道（profile/reader/writer/DOM——白名单 io 边）
#include <sdurws/ird/workflow/Types.hpp>  // workflow::WorkflowError（调用方错误 fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {
namespace {

// =====================================================================
// io 码词形（透传用——码值权威归 io IoErrorCode；本文件零自造码）
// =====================================================================

/// io 稳定码的词形表（IoErrorCode → 文本；io.md §9.12 词形——异常消息
/// 与 Dev 诊断中透传对端码所需的只读映射。io 单元未导出码→文本函数，
/// 本表仅覆盖设置通道可能出现的码集（资源族＋JSON 格式族＋内部码），
/// 未覆盖码回退枚举序数值文本——码词形不伪造，缺失即如实标 unknown）。
const char* ioCodeText(io::IoErrorCode code)
{
    switch (code) {
    case io::IoErrorCode::Ok:                       return "Ok";
    case io::IoErrorCode::ResNotFound:              return "IO-RES-NOT-FOUND";
    case io::IoErrorCode::ResAccessDenied:          return "IO-RES-ACCESS-DENIED";
    case io::IoErrorCode::ResReadonly:              return "IO-RES-READONLY";
    case io::IoErrorCode::ResLockConflict:          return "IO-RES-LOCK-CONFLICT";
    case io::IoErrorCode::FormatJsonEncoding:       return "IO-FORMAT-JSON-ENCODING";
    case io::IoErrorCode::FormatJsonDupKey:         return "IO-FORMAT-JSON-DUPKEY";
    case io::IoErrorCode::FormatJsonNumber:         return "IO-FORMAT-JSON-NUMBER";
    case io::IoErrorCode::FormatJsonVersionMissing: return "IO-FORMAT-JSON-VERSION-MISSING";
    case io::IoErrorCode::FormatJsonVersionType:    return "IO-FORMAT-JSON-VERSION-TYPE";
    case io::IoErrorCode::FormatJsonVersionFuture:  return "IO-FORMAT-JSON-VERSION-FUTURE";
    case io::IoErrorCode::FormatJsonVersionLegacy:  return "IO-FORMAT-JSON-VERSION-LEGACY";
    case io::IoErrorCode::FormatJsonUnknown:        return "IO-FORMAT-JSON-UNKNOWN";
    case io::IoErrorCode::FormatJsonRequired:       return "IO-FORMAT-JSON-REQUIRED";
    case io::IoErrorCode::FormatJsonType:           return "IO-FORMAT-JSON-TYPE";
    case io::IoErrorCode::FormatJsonRange:          return "IO-FORMAT-JSON-RANGE";
    case io::IoErrorCode::FormatInternal:           return "IO-FORMAT-INTERNAL";
    default:                                        return "IO-<unmapped-code>";
    }
}

/// io 错误的透传词形（"码 params detail"单行——D-WF-7 对端零加工精神；
/// params 键值对按构造序拼接，敏感值脱敏归日志管线——IDevLogSink 注释）。
std::string ioErrorText(const io::IoError& error)
{
    std::string text = ioCodeText(error.code);
    for (const auto& kv : error.params) {
        text += " ";
        text += kv.first;
        text += "=";
        text += kv.second;
    }
    if (!error.detail.empty()) {
        text += " (";
        text += error.detail;
        text += ")";
    }
    return text;
}

// =====================================================================
// profile 构造（格式所有者声明——user-settings/1 的结构契约）
// =====================================================================

/// 便捷构造：声明一个对象属性（key/required/子 shape——JsonProfile 声明
/// 序即 canonical 写出序，故各键按冻结声明序登记）。
io::JsonProperty objectProperty(const char* key, bool required,
                                std::shared_ptr<const io::JsonShape> shape)
{
    io::JsonProperty prop;
    prop.key.assign(key);
    prop.required = required;
    prop.shape = std::move(shape);
    return prop;
}

/// 便捷构造：无子约束的叶属性（仅键名与必填位——类型由 shape 给出）。
io::JsonProperty leafProperty(const char* key, bool required, io::JsonValueType type)
{
    auto shape = std::make_shared<io::JsonShape>();
    shape->type = type;
    return objectProperty(key, required, std::move(shape));
}

/// user-settings/1 的根结构契约（与 Settings.hpp 类注 JSON 形状逐键对应；
/// 未知键策略 Reject＝schema 字段集封闭——I-WF-5 的 io 侧落实）。
io::JsonProfile makeUserSettingsProfile()
{
    // 热键条目结构：{commandId: String, key: String}——两键必填
    //（解绑登记以 key 空串表达，条目本身不可缺键）。
    auto hotkeyItem = std::make_shared<io::JsonShape>();
    hotkeyItem->type = io::JsonValueType::Object;
    hotkeyItem->properties = {
        leafProperty("commandId", true, io::JsonValueType::String),
        leafProperty("key", true, io::JsonValueType::String),
    };

    // 勾选记忆结构：三 bool 全必填（写侧恒写全三键——读侧缺键即损坏，
    // 不猜测部分勾选的语义）。
    auto selectionShape = std::make_shared<io::JsonShape>();
    selectionShape->type = io::JsonValueType::Object;
    selectionShape->properties = {
        leafProperty("includeResults", true, io::JsonValueType::Boolean),
        leafProperty("includeReports", true, io::JsonValueType::Boolean),
        leafProperty("includeDrafts", true, io::JsonValueType::Boolean),
    };

    // 最近项目列表：字符串数组。容量上限 10 不在 io 层声明（数组长度
    // 越界会判整个文档损坏——全量丢弃过于激进）；容量在业务映射层
    // 防御性截断（保留头部 LRU 最近 10 条——与溢出淘汰同语义，P-WF-5
    // 安全默认，截断事实经 Dev 诊断留痕）。
    auto recentItems = std::make_shared<io::JsonShape>();
    recentItems->type = io::JsonValueType::Array;
    recentItems->items = [] {
        auto s = std::make_shared<io::JsonShape>();
        s->type = io::JsonValueType::String;
        return s;
    }();

    // 热键绑定列表：条目对象数组（数量无冻结上限——P-WF-5 不发明数值；
    // io 解析预算 JsonDocBytes/JsonStringChars 仍在解析层生效）。
    auto hotkeyList = std::make_shared<io::JsonShape>();
    hotkeyList->type = io::JsonValueType::Array;
    hotkeyList->items = std::move(hotkeyItem);

    // 根对象：五键冻结集。packageExportSelection 是唯一可选键
    //（缺失＝无记忆——nullopt 与"记忆了全选"是不同事实，io 不注入
    // 默认值，缺失语义归业务侧自判）；其余四键恒写出（写即完整快照）。
    io::JsonProfile profile;
    profile.profileId.assign(kUserSettingsProfileId);
    profile.supportedVersions = {kUserSettingsSchemaVersion};
    profile.rootShape.type = io::JsonValueType::Object;
    profile.rootShape.properties = {
        leafProperty(kUserSettingsKeySchemaVersion, true, io::JsonValueType::Integer),
        objectProperty(kUserSettingsKeyRecentProjects, true, std::move(recentItems)),
        objectProperty(kUserSettingsKeyPackageSelection, false, std::move(selectionShape)),
        leafProperty(kUserSettingsKeyLastOpenDirectory, true, io::JsonValueType::String),
        objectProperty(kUserSettingsKeyHotkeyBindings, true, std::move(hotkeyList)),
    };
    // unknownKeyPolicy 保持默认 Reject（io.md §5.9.2——schema 演进必须
    // 走版本升级，不允许静默吞字段；I-WF-5 的第二道闸）。
    return profile;
}

// =====================================================================
// DOM 组装（store 侧——UserSettings → 受限 DOM，编程构造无源定位）
// =====================================================================

/// 便捷构造：字符串值节点（编程构造——span 全零＝"无定位"哨兵）。
io::JsonValue stringValue(std::string text)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::String;
    v.stringValue = std::move(text);
    return v;
}

/// 便捷构造：布尔值节点。
io::JsonValue boolValue(bool flag)
{
    io::JsonValue v;
    v.type = io::JsonValue::Type::Boolean;
    v.boolValue = flag;
    return v;
}

/// 便捷构造：成员（键＋值——保文件出现序，即下方组装的声明序）。
io::JsonMember member(const char* key, io::JsonValue value)
{
    io::JsonMember m;
    m.key.assign(key);
    m.value = std::move(value);
    return m;
}

/// 组装设置文档的受限 DOM（键序＝profile 冻结声明序——canonical 写出
/// 经 JsonWriteOptions{profile} 采纳声明序，同语义同字节）。
io::JsonDocument buildDocument(const UserSettings& settings)
{
    io::JsonDocument doc;

    // ① 版本字段（先写——版本判定先于 schema 校验的读侧惯例同源）。
    io::JsonValue version;
    version.type = io::JsonValue::Type::Integer;
    version.integerValue = kUserSettingsSchemaVersion;
    doc.root.members.push_back(member(kUserSettingsKeySchemaVersion, std::move(version)));

    // ② 最近项目（LRU 序字符串数组——透明透传，零脱敏零重排）。
    io::JsonValue recent;
    recent.type = io::JsonValue::Type::Array;
    recent.items.reserve(settings.recentProjects.size());
    for (const std::string& path : settings.recentProjects) {
        recent.items.push_back(stringValue(path));
    }
    doc.root.members.push_back(member(kUserSettingsKeyRecentProjects, std::move(recent)));

    // ③ 包导出勾选记忆（可选键——nullopt 不写该键＝"无记忆"事实；
    // 有值写全三 bool 子键）。
    if (settings.packageExportSelection.has_value()) {
        io::JsonValue selection;
        selection.type = io::JsonValue::Type::Object;
        selection.members.push_back(member(
            "includeResults", boolValue(settings.packageExportSelection->includeResults)));
        selection.members.push_back(member(
            "includeReports", boolValue(settings.packageExportSelection->includeReports)));
        selection.members.push_back(member(
            "includeDrafts", boolValue(settings.packageExportSelection->includeDrafts)));
        doc.root.members.push_back(
            member(kUserSettingsKeyPackageSelection, std::move(selection)));
    }

    // ④ 上次打开目录（恒写——空串＝无记录，也是快照事实）。
    doc.root.members.push_back(member(
        kUserSettingsKeyLastOpenDirectory, stringValue(settings.lastOpenDirectory)));

    // ⑤ 快捷键绑定（词形快照——commandId/key 透明透传；空串 key＝解绑
    // 登记，照写）。
    io::JsonValue hotkeys;
    hotkeys.type = io::JsonValue::Type::Array;
    hotkeys.items.reserve(settings.hotkeyBindings.size());
    for (const HotkeyBindingRecord& record : settings.hotkeyBindings) {
        io::JsonValue item;
        item.type = io::JsonValue::Type::Object;
        item.members.push_back(member("commandId", stringValue(record.commandId)));
        item.members.push_back(member("key", stringValue(record.keyPortableText)));
        hotkeys.items.push_back(std::move(item));
    }
    doc.root.members.push_back(member(kUserSettingsKeyHotkeyBindings, std::move(hotkeys)));

    doc.root.type = io::JsonValue::Type::Object;
    return doc;
}

// =====================================================================
// DOM 映射（load 侧——受限 DOM → UserSettings；profile 已过校验，
// 本段只做形状提取与业务防御）
// =====================================================================

/// 便捷取值：按键取对象成员（未命中＝nullptr——profile 已保证必填键
/// 在，防御性判空仅为健壮，不改变契约）。
const io::JsonValue* memberOf(const io::JsonValue& object, const char* key)
{
    return object.findMember(key);
}

/// 便捷提取：字符串成员值（前提：成员存在且 profile 已判 String——
/// 不满足返回缺省空串，属防御分支非契约路径）。
std::string stringOf(const io::JsonValue& object, const char* key)
{
    if (const io::JsonValue* v = memberOf(object, key); v != nullptr && v->isString()) {
        return v->stringValue;
    }
    return {};
}

/// 便捷提取：布尔成员值（前提同上）。
bool boolOf(const io::JsonValue& object, const char* key)
{
    if (const io::JsonValue* v = memberOf(object, key); v != nullptr && v->isBoolean()) {
        return v->boolValue;
    }
    return false;
}

/// 从已校验 DOM 提取设置值（业务防御段：容量截断——见函数内注释）。
UserSettings mapDocument(const io::JsonDocument& doc, diagnostics::IDevLogSink* devLog)
{
    UserSettings settings;
    const io::JsonValue& root = doc.root;

    // ② 最近项目：逐条透传（LRU 序保序）。容量防御（P-WF-5 安全默认）：
    // 持久化值超出需求冻结上限（kRecentProjectsCapacity=10，PM-10 同源
    // 值）时**截断保留头部**——头部即最近使用条目，语义与 RecentProjects
    // Service 的 LRU 溢出淘汰完全一致（数据损失最小化）；不判整体损坏
    //（超容量不是结构损坏，全量丢弃过于激进）。截断事实经 Dev 诊断
    // 留痕（不吞不静默——验收 acceptance 3"按安全默认＋留痕"）。
    if (const io::JsonValue* recent = memberOf(root, kUserSettingsKeyRecentProjects);
        recent != nullptr && recent->isArray()) {
        for (const io::JsonValue& item : recent->items) {
            if (item.isString()) {
                settings.recentProjects.push_back(item.stringValue);
            }
        }
        if (settings.recentProjects.size() > kRecentProjectsCapacity) {
            if (devLog != nullptr) {
                devLog->logDev(kUserSettingsDevChannel,
                               "recentProjects 超出容量上限，截断保留最近 "
                               + std::to_string(kRecentProjectsCapacity)
                               + " 条（P-WF-5 安全默认；实际条数="
                               + std::to_string(settings.recentProjects.size()) + "）");
            }
            settings.recentProjects.resize(kRecentProjectsCapacity);
        }
    }

    // ③ 勾选记忆：缺失/null＝无记忆（nullopt——io 不注入默认值，缺失
    // 语义归业务自判）；存在即 profile 已判 Object 且三子键齐备。
    if (const io::JsonValue* selection =
            memberOf(root, kUserSettingsKeyPackageSelection);
        selection != nullptr && selection->isObject()) {
        PackageSelectionFlags flags;
        flags.includeResults = boolOf(*selection, "includeResults");
        flags.includeReports = boolOf(*selection, "includeReports");
        flags.includeDrafts = boolOf(*selection, "includeDrafts");
        settings.packageExportSelection = flags;
    }

    // ④ 上次打开目录（必填键——profile 已保证；提取即得）。
    settings.lastOpenDirectory = stringOf(root, kUserSettingsKeyLastOpenDirectory);

    // ⑤ 快捷键绑定：词形快照透传（commandId/key 字符串对——键序语义
    // 归 ui，本存储零解析）。
    if (const io::JsonValue* hotkeys = memberOf(root, kUserSettingsKeyHotkeyBindings);
        hotkeys != nullptr && hotkeys->isArray()) {
        for (const io::JsonValue& item : hotkeys->items) {
            if (!item.isObject()) {
                continue;  // profile 已判条目为 Object——防御分支，不达
            }
            HotkeyBindingRecord record;
            record.commandId = stringOf(item, "commandId");
            record.keyPortableText = stringOf(item, "key");
            settings.hotkeyBindings.push_back(std::move(record));
        }
    }

    return settings;
}

}  // namespace

// =====================================================================
// UserSettingsStore（公共接口实现——契约见 Settings.hpp 方法注）
// =====================================================================

UserSettingsStore::UserSettingsStore(std::filesystem::path filePath,
                                     diagnostics::IDevLogSink* devLog)
    : m_filePath(std::move(filePath)), m_devLog(devLog)
{
    // 调用方契约违约 fail-fast（§10.3）：空路径＝无文件可绑定的存储。
    if (m_filePath.empty()) {
        throw WorkflowError("UserSettingsStore: 设置文件路径为空（装配缺陷"
                            "——须注入用户目录路径或缺省工厂产物）");
    }
}

UserSettings UserSettingsStore::load() const
{
    // 缺省默认值（首次使用零配置——load 契约的失败兜底形态）。
    const UserSettings defaults;

    // ① 存在性预检：文件不存在＝首次使用，**非错误**——返回默认值且
    // 零诊断（error_code 重载不抛——路径不可达等环境事实按"不存在"
    // 处理，与首次使用同语义，不崩溃）。
    std::error_code ec;
    if (!std::filesystem::exists(m_filePath, ec) || ec) {
        return defaults;
    }

    // ② io reader 按 profile 解析（版本判定先于 schema 校验——io §5.9.2；
    // profile 注册表本存储每次 load 自持（装配期注册、解析期只读——
    // io §9.5 注册表纪律），无跨单元共享面。
    auto registry = std::make_shared<io::JsonProfileRegistry>();
    registry->registerProfile(makeUserSettingsProfile());

    const auto reader = io::makeStructuredDataReader(registry);
    const std::string profileId{kUserSettingsProfileId};
    io::JsonReadOptions options;
    options.profileId = &profileId;

    const io::IoResult<io::JsonDocument> parsed =
        reader->parse(m_filePath, options, /*budget=*/nullptr, /*cancel=*/nullptr);

    if (!parsed) {
        // 损坏/不可读路径（语法/版本/schema/预算/资源错误）：给诊断不
        // 崩溃（契约 acceptance 3）——Dev 诊断留痕（码与 detail 透传）
        // 后返回缺省默认值。首使用之外的存在性失败（如恰好被删除的
        // 竞态窗口）同走此路径——同为"读不到可用数据"，语义一致。
        if (m_devLog != nullptr) {
            m_devLog->logDev(kUserSettingsDevChannel,
                             "设置文件读取/校验失败，按缺省默认值继续："
                             + ioErrorText(parsed.error));
        }
        return defaults;
    }

    // ③④ 逐字段映射＋容量防御截断（P-WF-5——截断留痕在 mapDocument 内）。
    return mapDocument(parsed.value, m_devLog);
}

void UserSettingsStore::store(const UserSettings& settings)
{
    // 防御性再断言（构造已挡——直通路径不可达，fail-fast 双保险）。
    if (m_filePath.empty()) {
        throw WorkflowError("store: 设置文件路径为空（构造契约已拒绝空路径"
                            "——本分支不可达，属实现缺陷）");
    }

    // ② 父目录自持创建（用户设置目录是产品自有面——缺省工厂的深路径
    // 如 %APPDATA%/RobWork/industrialrobot/ 首次写入即自持；io 设施
    // 不代建目录，本类是 io 的调用方故由本类承担。失败不吞：诊断＋上抛，
    // 原文件（若有）零接触）。
    if (const std::filesystem::path parent = m_filePath.parent_path();
        !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (m_devLog != nullptr) {
                m_devLog->logDev(kUserSettingsDevChannel,
                                 "设置目录创建失败：" + ec.message()
                                 + "（path=" + parent.string() + "）");
            }
            throw WorkflowError("store: 设置目录创建失败（" + ec.message()
                                + "）——设置未持久化");
        }
    }

    // ①③ canonical 写出：DOM 组装（声明序）→ io writer 原子替换
    //（文件目标＝同目录暂存＋rename——write 成功＝目标完整就位，失败/
    // 中断＝先前文件原样保留，io §4.6）。写出选项携带 profile＝键序
    // 采纳声明序（canonical 语义，同语义文档同字节——NFR-COR-01 通道落点）。
    const io::JsonDocument doc = buildDocument(settings);
    const io::JsonProfile profile = makeUserSettingsProfile();
    io::JsonWriteOptions writeOptions;
    writeOptions.profile = &profile;

    const io::IoResult<void> written =
        io::makeJsonWriter()->write(io::JsonOutputTarget::file(m_filePath),
                                    doc, writeOptions);
    if (!written) {
        // 环境错误零吞错（§10.3）：io 码与 detail 原样透传入异常消息
        //（对端零加工——D-WF-7；本单元零新增稳定码）。先前文件经原子
        // 替换语义原样保留——失败不损坏既有设置。
        const std::string reason = ioErrorText(written.error);
        if (m_devLog != nullptr) {
            m_devLog->logDev(kUserSettingsDevChannel,
                             "设置写出失败：" + reason);
        }
        throw WorkflowError("store: 设置写出失败（" + reason + "）"
                            "——先前文件保持原状（io 原子写出语义）");
    }
}

std::filesystem::path defaultUserSettingsFilePath()
{
    // Windows 主面：%APPDATA%（Roaming——用户级、随用户漫游）。目录段
    // 词形＝industrialrobot（产品自有目录段，与 .rwdesign 项目目录零
    // 交集——PM-14；注：产品面源码零 RobWork 字面量是 R-4 红线扫描的
    // 登记口径，目录段命名同守该口径）。
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "industrialrobot"
               / "user-settings.json";
    }
    // 非 Windows / 无 APPDATA 面：$HOME 下的点目录词形。
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".industrialrobot"
               / "user-settings.json";
    }
    // 两环境变量皆不可得：返回空路径——构造空路径即 fail-fast（不静默
    // 落当前目录；装配层应注入显式路径）。
    return {};
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
