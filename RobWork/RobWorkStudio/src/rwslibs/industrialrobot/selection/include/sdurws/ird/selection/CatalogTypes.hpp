/**
 * @file   CatalogTypes.hpp
 * @brief  目录包业务模型（selection 单元）——版本化 CSV 目录包的四表数据
 *         模型、字段字典、解析输入、校验报告与不可变目录快照。
 *
 * 设计依据：
 *   - units/selection.md §4.1（目录包数据模型——本头全部核心类型的签名
 *     基线）、§4.2（目录快照与项目绑定——写入即锁定只增；包 canonical
 *     字节摘要）、§4.3（身份关系表——(catalogId, version, modelId) 三元
 *     组唯一确定一个目录条目实例）、§4.4（单位表——SI 域比较；单位进入
 *     诊断比较字段）、§5.2（目录包文件清单 schema——P-IO-7 注册义务）、
 *     §5.3（业务校验清单——八码逐行）、§6.1（曲线模型——CapabilityPoint/
 *     PerformanceCurve）、§6.3（曲线校验表——四码）
 *   - 需求 SEL-01（版本化电机/减速器 CSV 目录包：清单/型号主表/能力曲线
 *     表/兼容关系表＋字段字典、版本和来源信息）、SEL-02（导入校验：文件
 *     清单/文件间引用/单位/必填字段/型号唯一性/数值范围）、ERR-01（稳定
 *     诊断码＋比较型字段）、NFR-COR-03（非有限数/非法单位/引用缺失不静默
 *     通过）、CON-05（内容寻址身份）
 *   - 任务契约 tasks/foundation/WP-19-T03.json（本头即其 outputs
 *     "implementation" 的模型面：目录包模型与导入校验的公共类型层）
 *
 * ★ 分工边界（与 io 的双层交接——卡 §5.1，acceptance 3 的核心语义）：
 *   本头是 selection 业务层的纯数据模型——零文件系统访问、零 io 类型依赖
 *   （selection 依赖白名单仅 core＋evidence 编译边，卡 §3.2；io 在"运行
 *   时注入/端口"列）。io 负责文件层（SafePath/BudgetGuard/CSV/JSON 语法
 *   解析、清单核对与文件间引用存在性——io 卡 §7.8）；本头的
 *   ParsedCatalogInput 是"io 解析产物"的纯 std 承载形态——L5 装配层把
 *   io::RawTable/JsonDocument 映射为 ParsedCatalogInput/CatalogManifest
 *   后交给本单元，字段名与行号语义与 io 通道对齐（rowNo＝1 起物理行号，
 *   含方言标识行偏移——io 卡 §5.6 口径）。selection 不绕过 io 直接读取
 *   文件（卡 §5.1 边界纪律）。
 *
 * 落位细化登记（卡 §19.3——DTB §5.4 精神，随本任务同步登记于单元卡）：
 *   1. 卡 §4.1 写 `core::StableId`：core 单元未落位该类型（core include
 *      树实测无 StableId——2026-10-06）。本头以域内强语义别名 ModelId/
 *      CurveId（std::string 承载，包内唯一）落位；core 侧 StableId 落位
 *      后收编（包内唯一性语义不变）。
 *   2. 卡 §4.1 `CatalogManifest::fieldDictionary` 为单数：CSV 目录包有
 *      两份型号主表＋曲线表＋兼容表，字段字典按"每 CSV 文件一份"承载为
 *      std::vector<FieldDictionary>（FieldDictionary::targetFile 定位），
 *      语义不变（列名→语义/单位/必填性——SEL-01）。
 *   3. 字段字典单位词表＝core Units 已注册 token（core/src/Units.cpp 词表
 *      实测：m/mm/rad/deg/kg/s/N/N*m/kg*m^2/W/m/s/rad/s/m/s^2/rad/s^2/
 *      V/1）。rpm/arcmin/h/°C 未注册——v1 目录模板（formatVersion "1"）
 *      的数值列单位冻结为 SI 口径（rad/s、rad、循环数按 "1"），"目录若以
 *      rpm 提供"的换算支持随 core Units 词表扩展（换算唯一经 core——卡
 *      §5.3，本域不自建换算表）。
 *
 * 线程安全：全部为纯值类型（不可变共享安全）；校验器/装配器见
 * CatalogProvider.hpp 头注。
 */

#ifndef IRD_SELECTION_CATALOGTYPES_HPP
#define IRD_SELECTION_CATALOGTYPES_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>     // core::ContentIdentity——包/曲线内容身份
#include <sdurws/ird/core/Identity.hpp>   // core::ObjectId——CatalogVersion 锁定对象 ID

namespace sdurws::ird::selection {

// =====================================================================
// 强语义 ID 别名（卡 §4.1 core::StableId 的落位承载——见文件头注登记 1）
// =====================================================================

/// 型号稳定 ID（包内唯一；跨版本同 ID 同型号——卡 §4.1 modelId 注）。
/// 字符串承载但语义是"稳定对象标识"：显示名（displayName）永不替代它
/// （卡 §4.3——显示名称永不替代稳定对象 ID，ARC-04/CON-01 纪律）。
using ModelId = std::string;

/// 能力曲线稳定 ID（包内唯一；capability_curves.csv 的 curve_id 列）。
using CurveId = std::string;

// =====================================================================
// §4.1 目录身份与来源（显示名称永不替代身份——ARC-04/CON-01）
// =====================================================================

/**
 * @brief 目录身份五元组的业务承载（卡 §4.1 CatalogIdentity）。
 *
 * 身份关系（卡 §4.3）：(catalogId, version, 包内容摘要) 唯一确定一个目录
 * 版本实例。catalogId/version 由 manifest.json 声明（企业分配）；同一
 * catalogId 允许多版本并存（版本化目录包——SEL-01）。
 */
struct CatalogIdentity {
    std::string catalogId;                 ///< 稳定目录 ID（企业分配；非文件路径）
    std::string version;                   ///< 目录版本号（同 catalogId 多版本并存）
    core::ContentIdentity contentIdentity; ///< 包 canonical 字节摘要（SHA-256——CON-05；
                                           ///<   装配入口按快照规范序列化计算回填，见
                                           ///<   CatalogProvider.hpp 的 assemble 契约）
    std::string source;                    ///< 来源信息（SEL-01：企业来源描述——非文件路径身份）

    /// 严格相等（含内容摘要字节——身份完整性）。
    bool operator==(const CatalogIdentity& o) const
    {
        return catalogId == o.catalogId && version == o.version
            && contentIdentity == o.contentIdentity && source == o.source;
    }
    bool operator!=(const CatalogIdentity& o) const { return !(*this == o); }
};

/**
 * @brief 锁定版本引用（卡 §4.1 CatalogVersion）——项目内
 *        catalog/<catalogId>/<version>/ 的引用形态。
 *
 * lockObjectId 是 project 对象库内的锁定对象 ID（对象落位经 project 存储
 * 端口——selection 零 project 编译边，故本类型只承载 ID 值语义；落位与
 * 引用保护由 project 承载，管理动作随 WP-19-T07）。文件路径不作为目录
 * 身份（卡 §4.2）。
 */
struct CatalogVersion {
    CatalogIdentity identity;      ///< 被锁定版本的目录身份
    core::ObjectId lockObjectId;   ///< 锁定对象 ID（project 对象库内；切片 Object 条目引用）

    bool operator==(const CatalogVersion& o) const
    {
        return identity == o.identity && lockObjectId == o.lockObjectId;
    }
    bool operator!=(const CatalogVersion& o) const { return !(*this == o); }
};

/**
 * @brief 来源与导入追溯（卡 §4.1 CatalogSource——SEL-01"来源信息"）。
 */
struct CatalogSource {
    std::string description;                    ///< 企业来源描述（显示用）
    core::ContentIdentity importedFromRevision; ///< 导入时项目修订内容身份（追溯；
                                                ///<   全零＝未记录——不伪造）
};

// =====================================================================
// §5.2 目录包文件清单 schema（P-IO-7 注册义务的物化数据）
// =====================================================================

/// 文件角色词表（v1 冻结——与卡 §5.2 表"角色"列一一对应；L5 装配把该
/// 注册数据交给 io 清单核对框架〔io 卡 §7.8〕执行文件层核对）。
struct ManifestEntry {
    std::string fileName;  ///< 文件名（包内相对名；v1 冻结五个固定名——kCatalogFile* 常量）
    std::string role;      ///< 角色（manifest/motors/gearboxes/curves/compatibility 词表）
    bool required = true;  ///< 必备性（v1 五文件全部必备；曲线表/兼容表允许零数据行——卡 §5.2）
    std::string sha256Hex; ///< 文件字节 SHA-256（64 位小写十六进制；io 文件层产物——
                           ///<   本域不校验其与磁盘字节一致〔那是 io 清单核对职责〕，
                           ///<   仅承载与透传）

    bool operator==(const ManifestEntry& o) const
    {
        return fileName == o.fileName && role == o.role && required == o.required
            && sha256Hex == o.sha256Hex;
    }
    bool operator!=(const ManifestEntry& o) const { return !(*this == o); }
};

/**
 * @brief 单列字段规格（字段字典的原子条目——SEL-01"字段字典"）。
 *
 * 单位语义（登记 3）：unit 为空串＝文本列（不进单位词表校验）；非空＝
 * core Units 已注册 token（UnitToken::find 命中），校验期逐列核对量纲
 * （列语义→期望 QuantityKind——CatalogValidation 内冻结的 v1 列注册表）。
 */
struct FieldSpec {
    std::string column;    ///< CSV 列名（与表头逐字符一致——区分大小写）
    std::string semantic;  ///< 语义描述（字段字典"列名→语义"——显示/文档用）
    std::string unit;      ///< 单位 token（core 词表；空＝文本列）；数值列进 SI 域比较
    bool required = true;  ///< 必填性：true＝必填（缺失→SEL-CATALOG-FIELD-MISSING）；
                           ///<   false＝可缺失（缺失→条目 missing 清单显式标记——ERR-01）

    bool operator==(const FieldSpec& o) const
    {
        return column == o.column && semantic == o.semantic && unit == o.unit
            && required == o.required;
    }
    bool operator!=(const FieldSpec& o) const { return !(*this == o); }
};

/**
 * @brief 单个 CSV 文件的字段字典（卡 §4.1 FieldDictionary 的按文件承载
 *        ——落位细化登记 2）。
 */
struct FieldDictionary {
    std::string targetFile;         ///< 适用 CSV 文件名（kCatalogFile* 之一）
    std::vector<FieldSpec> fields;  ///< 列定义（序＝CSV 列序——schema 校验按序逐列比对）

    bool operator==(const FieldDictionary& o) const
    {
        return targetFile == o.targetFile && fields == o.fields;
    }
    bool operator!=(const FieldDictionary& o) const { return !(*this == o); }
};

/**
 * @brief 包清单（卡 §4.1 CatalogManifest——selection 注册给 io 的结构契约，
 *        P-IO-7 消账面）。
 *
 * formatVersion 语义（卡 §5.2）：未知格式版本 → 拒绝导入并给升级指引
 * （不自动升级——PM-06 精神）。校验器对未知 formatVersion 以 schema 级
 * 致命结构错误拒绝（std::invalid_argument——卡 §14.2 note"致命结构错误
 * 以异常 fail-fast（schema 级）"），不进入行级报告（后续校验的字典语义
 * 全部依赖格式版本，继续校验无意义）。
 */
struct CatalogManifest {
    std::string formatVersion;              ///< 目录包 schema 版本（v1＝kCatalogFormatVersion）
    CatalogIdentity identity;               ///< catalogId/version/contentIdentity/source
    std::vector<ManifestEntry> files;       ///< 文件清单（名称/角色/必备性/字节哈希）
    std::vector<FieldDictionary> fieldDictionary; ///< 字段字典（每 CSV 文件一份——登记 2）

    bool operator==(const CatalogManifest& o) const
    {
        return formatVersion == o.formatVersion && identity == o.identity
            && files == o.files && fieldDictionary == o.fieldDictionary;
    }
    bool operator!=(const CatalogManifest& o) const { return !(*this == o); }
};

// =====================================================================
// §4.1 条目辅助类型（卡面只给名字的形态补全——语义随逐字段注释登记）
// =====================================================================

/// 过载能力（卡 §4.1 OverloadSpec）：过载转矩与允许持续时间。
struct OverloadSpec {
    double torque = 0.0;      ///< 过载转矩，单位 N·m（>0——范围校验）
    double duration = 0.0;    ///< 允许过载持续时间，单位 s（>0——范围校验）

    bool operator==(const OverloadSpec& o) const
    {
        return torque == o.torque && duration == o.duration;
    }
};

/// 温度降额（卡 §4.1 ThermalDerating）：环境温度→能力系数。
/// 单位登记（登记 3）：°C 未入 core Units 词表——v1 目录模板以无量纲
/// 档位值承载（refTemp 为档位序值、factorPerRef 为每档能力系数），温度
/// 量纲的完整承载随 core Units 扩展；筛选语义（SEL-03）归 WP-19-T04。
struct ThermalDerating {
    double refTemp = 0.0;        ///< 参考环境温度档位值（v1 无量纲档位；°C 语义）
    double factorPerRef = 0.0;   ///< 每档能力系数（无量纲 (0,1]——范围校验）

    bool operator==(const ThermalDerating& o) const
    {
        return refTemp == o.refTemp && factorPerRef == o.factorPerRef;
    }
};

/// 外形和安装接口（卡 §4.1 MountSpec——兼容性筛选用；词表值由字段字典
/// 登记，v1 仅要求非空文本）。
struct MountSpec {
    std::string flangeKind;  ///< 法兰接口词表值（如法兰规格名；空＝未提供）
    std::string shaftKind;   ///< 轴伸接口词表值（如轴径/键槽规格名；空＝未提供）

    bool operator==(const MountSpec& o) const
    {
        return flangeKind == o.flangeKind && shaftKind == o.shaftKind;
    }
};

/// 允许外载荷（卡 §4.1 ExternalLoadSpec）：减速器输出轴允许的悬臂载荷。
struct ExternalLoadSpec {
    double radial = 0.0;     ///< 允许径向力，单位 N（>=0——范围校验）
    double axial = 0.0;      ///< 允许轴向力，单位 N（>=0——范围校验）
    double distance = 0.0;   ///< 作用点到输出轴肩的距离，单位 m（>=0；0＝未标注作用点）

    bool operator==(const ExternalLoadSpec& o) const
    {
        return radial == o.radial && axial == o.axial && distance == o.distance;
    }
};

/// 能力曲线引用（卡 §4.1 CurveRef——条目→曲线的归属关系；语义校验
/// （存在性/owner 匹配）在导入校验期执行——SEL-CATALOG-REF-DANGLING）。
struct CurveRef {
    CurveId curveId;      ///< 引用的曲线稳定 ID（capability_curves.csv curve_id 列）
    std::string xQuantity; ///< 横坐标量纲 token（speed/torque/power 词表——§6.1）
    std::string yQuantity; ///< 纵坐标量纲 token（同上）

    bool operator==(const CurveRef& o) const
    {
        return curveId == o.curveId && xQuantity == o.xQuantity && yQuantity == o.yQuantity;
    }
};

/// 缺失字段显式标记（卡 §4.1 MissingField——不伪造数值，ERR-01）。
struct MissingField {
    std::string column;  ///< 缺失的 CSV 列名（字段字典内登记的可缺失列）
    std::string reason;  ///< 缺失原因（如 "cell-empty"——空白单元格）

    bool operator==(const MissingField& o) const
    {
        return column == o.column && reason == o.reason;
    }
};

/// 条目校验状态（卡 §4.1 ValidationStatus）。
enum class ValidationStatus {
    Valid,    ///< 校验通过且无可缺失字段缺失
    Partial,  ///< 校验通过但存在可缺失字段缺失（missing 清单非空）
    Invalid,  ///< 条目存在校验失败（导入期报告非空则整体拒绝；本态保留给
              ///<   部分导入语义——卡 §5.1"不完整导入＝删除重导"，R1 不启用）
};

// =====================================================================
// §4.1 型号主表行（业务模型，非 CSV 行）
// =====================================================================

/**
 * @brief 电机型号主表行（卡 §4.1 MotorCatalogEntry——业务模型）。
 *
 * 数值单位全部 SI（卡 §4.4 单位表）：转矩 N·m、角速度 rad/s、功率 W、
 * 惯量 kg·m²、质量 kg、电压 V、时间 s。缺失字段显式入 missing 清单
 * （不伪造数值——ERR-01）。
 */
struct MotorCatalogEntry {
    ModelId modelId;            ///< 稳定型号 ID（包内唯一）
    std::string vendor;         ///< 厂商（显示用；不参与身份）
    std::string displayName;    ///< 型号显示名（可重复——不参与身份）
    CatalogIdentity catalog;    ///< 所属目录版本（装配时回填＝manifest.identity）
    double ratedTorque = 0.0;   ///< 额定连续转矩，单位 N·m（>0）
    double peakTorque = 0.0;    ///< 峰值转矩，单位 N·m（>0 且 >= ratedTorque）
    double ratedSpeed = 0.0;    ///< 额定转速，单位 rad/s（>0；目录若以 rpm 提供经
                                ///<   字段字典声明换算口径——v1 冻结 rad/s，登记 3）
    double maxSpeed = 0.0;      ///< 最高转速，单位 rad/s（>0 且 >= ratedSpeed）
    double ratedPower = 0.0;    ///< 额定功率，单位 W（>0）
    std::optional<OverloadSpec> overload;    ///< 过载能力＋持续时间 s（可缺失→missing 标记）
    std::string dutyClass;      ///< 工作制词表值（S1/S2/…——词表登记归字段字典语义列）
    std::optional<double> ratedVoltage;      ///< 额定电压，单位 V（可缺失→missing 标记）
    std::optional<ThermalDerating> thermal;  ///< 温度降额（可缺失→missing 标记）
    std::optional<double> brakeTorque;       ///< 制动能力，单位 N·m（可缺失→missing 标记）
    std::optional<double> holdingTorque;     ///< 保持能力，单位 N·m（可缺失→missing 标记）
    double rotorInertia = 0.0;  ///< 转子惯量，单位 kg·m²（>0；回填/映射输入——§12.4）
    double mass = 0.0;          ///< 质量，单位 kg（>0；壳体合成输入）
    MountSpec mounting;         ///< 外形和安装接口（兼容性筛选用；可缺失字段）
    std::vector<CurveRef> curves;   ///< 能力曲线引用（§6；owner 匹配校验导入期执行）
    std::vector<MissingField> missing; ///< 缺失字段清单（显式标记——ERR-01）
    ValidationStatus status = ValidationStatus::Valid; ///< 条目校验状态

    bool operator==(const MotorCatalogEntry& o) const;
    bool operator!=(const MotorCatalogEntry& o) const { return !(*this == o); }
};

/**
 * @brief 减速器型号主表行（卡 §4.1 GearboxCatalogEntry——业务模型）。
 *
 * 转矩/转速字段全部为输出轴系/输入侧 SI 口径（卡 §4.1 逐字段注）：
 * 额定/峰值输出转矩 N·m（输出轴系）、允许输入转速 rad/s、速比无量纲
 * 正值、效率∈(0,1] 无量纲。
 */
struct GearboxCatalogEntry {
    ModelId modelId;                  ///< 稳定型号 ID（包内唯一）
    std::string vendor;               ///< 厂商（显示用）
    std::string displayName;          ///< 型号显示名（可重复）
    CatalogIdentity catalog;          ///< 所属目录版本
    double ratedOutputTorque = 0.0;   ///< 额定输出转矩，单位 N·m（输出轴系；>0）
    double peakOutputTorque = 0.0;    ///< 峰值输出转矩，单位 N·m（>0 且 >= 额定）
    double maxInputSpeed = 0.0;       ///< 允许输入转速，单位 rad/s（>0）
    double ratio = 0.0;               ///< 速比（无量纲；>0——方向语义按卡 §9.4 与
                                      ///<   drivetrain c 口径换算，筛选消费归 T05）
    double efficiency = 0.0;          ///< 效率（无量纲；∈(0,1]）
    std::optional<double> backlash;   ///< 回程间隙（v1 冻结单位 rad——登记 3；>=0）
    std::optional<double> ratedLife;  ///< 额定寿命（v1 冻结口径＝循环数，无量纲 "1"
                                      ///<   ——登记 3；>0；小时口径待 core 词表扩展）
    std::string mountingOrientation;  ///< 安装方向词表值（字段字典登记；必填）
    std::optional<ExternalLoadSpec> extLoad;  ///< 允许外载荷（可缺失→missing 标记）
    double mass = 0.0;                ///< 质量，单位 kg（>0）
    std::optional<double> housingInertia; ///< 壳体（相关）惯量，单位 kg·m²（可缺失→标记；
                                          ///<   §12.4 合成时与转子区分、不重复计入）
    MountSpec mounting;               ///< 安装接口（可缺失字段）
    std::vector<CurveRef> curves;     ///< 能力曲线引用
    std::vector<MissingField> missing; ///< 缺失字段清单
    ValidationStatus status = ValidationStatus::Valid;

    bool operator==(const GearboxCatalogEntry& o) const;
    bool operator!=(const GearboxCatalogEntry& o) const { return !(*this == o); }
};

/**
 * @brief 兼容关系表行（卡 §4.1 CompatibilityRecord——电机—减速器）。
 *
 * 零行语义（卡 §5.2）：兼容表零数据行＝包内无预声明兼容对，组合兼容
 * 校核按"无记录即不兼容"执行（SEL-COMBO-INCOMPATIBLE 的判定基础）。
 */
struct CompatibilityRecord {
    ModelId motorId;     ///< 电机型号稳定 ID（必须存在于电机主表——REF-DANGLING 校验）
    ModelId gearboxId;   ///< 减速器型号稳定 ID（必须存在于减速器主表）
    std::string mountKind; ///< 安装关系词表值（法兰/轴伸……；同型号对多行且本值
                           ///<   矛盾→SEL-CATALOG-COMPAT-CONFLICT）

    bool operator==(const CompatibilityRecord& o) const
    {
        return motorId == o.motorId && gearboxId == o.gearboxId && mountKind == o.mountKind;
    }
};

// =====================================================================
// §6.1 能力曲线模型
// =====================================================================

/// 曲线采样点（卡 §6.1 CapabilityPoint）：单位随 PerformanceCurve 的
/// xUnit/yUnit 声明（SI 域比较）。
struct CapabilityPoint {
    double x = 0.0;  ///< 横坐标（SI；单位随 xUnit）
    double y = 0.0;  ///< 纵坐标（SI；单位随 yUnit）

    bool operator==(const CapabilityPoint& o) const { return x == o.x && y == o.y; }
};

/**
 * @brief 能力曲线（卡 §6.1 PerformanceCurve——分段线性插值的载体）。
 *
 * 构造纪律（卡 §6.1/§6.3）：点集在构造入口校验——采样点必须按 x 严格
 * 升序提交（UNORDERED 拒绝、DUP-X 拒绝——排序会掩盖目录错误，构造入口
 * 不代排序）、全部有限（NONFINITE）、x_min < x_max 且点数 >= 2（单点
 * 曲线走"固定额定值"口径，INTERVAL-INVALID——卡 §6.4）。
 * 统一构造入口＝tryMakePerformanceCurve（CatalogProvider.hpp 声明）。
 */
struct PerformanceCurve {
    CurveId curveId;         ///< 曲线稳定 ID（包内唯一）
    std::string xQuantity;   ///< 横坐标量纲 token（v1 词表：speed/torque/power——§6.1）
    std::string yQuantity;   ///< 纵坐标量纲 token（同上）
    std::string xUnit;       ///< 横坐标 SI 单位 token（core 词表，如 "rad/s"）
    std::string yUnit;       ///< 纵坐标 SI 单位 token（如 "N*m"）
    std::vector<CapabilityPoint> points; ///< 采样点（x 严格升序；点数 >= 2）
    CatalogIdentity catalog; ///< 曲线版本＝所属目录版本（卡 §6.1）
    core::ContentIdentity contentIdentity; ///< 曲线内容身份（点集规范序列化摘要）

    bool operator==(const PerformanceCurve& o) const;
    bool operator!=(const PerformanceCurve& o) const { return !(*this == o); }
};

// =====================================================================
// §4.2 不可变目录快照（导入成功产物；写入即锁定、只增）
// =====================================================================

/**
 * @brief 目录包不可变快照（卡 §4.2 CatalogPackageSnapshot＝四表业务模型
 *        的规范序列化承载）。
 *
 * 不变性纪律（卡 §4.2）：导入成功后本对象即冻结——调用方不得修改
 * （业务模型无 const 化是 C++ 值语义的取舍：不可变性由持有纪律与
 * InMemoryCatalogProvider 的锁定语义保证，见 CatalogProvider.hpp）。
 * 内容身份：contentIdentity＝canonicalPackageText(snapshot) 的 SHA-256
 * （变更任何业务字段→新内容身份→依赖该目录的切片失效，卡 §4.2）。
 */
struct CatalogPackageSnapshot {
    CatalogManifest manifest;                        ///< 清单（含回填后的包内容身份）
    std::vector<MotorCatalogEntry> motors;           ///< 电机主表（装配后 modelId 升序——确定性序）
    std::vector<GearboxCatalogEntry> gearboxes;      ///< 减速器主表（modelId 升序）
    std::vector<PerformanceCurve> curves;            ///< 能力曲线（curveId 升序）
    std::vector<CompatibilityRecord> compatibility;  ///< 兼容关系（motorId→gearboxId→mountKind 升序）
    core::ContentIdentity contentIdentity;           ///< 包 canonical 序列化摘要（装配入口计算回填）

    /// 快照相等＝五成员全等（内容身份一致性由 canonical 文本派生保证）。
    bool operator==(const CatalogPackageSnapshot& o) const
    {
        return manifest == o.manifest && motors == o.motors && gearboxes == o.gearboxes
            && curves == o.curves && compatibility == o.compatibility
            && contentIdentity == o.contentIdentity;
    }
    bool operator!=(const CatalogPackageSnapshot& o) const { return !(*this == o); }
};

// =====================================================================
// io 解析产物的 selection 承载形态（ParsedCatalogInput——§14.2 输入面）
// =====================================================================

/**
 * @brief 单个 CSV 文件的解析后表（io::RawTable 的纯 std 对齐形态——
 *        L5 装配层映射，selection 零 io 类型依赖）。
 */
struct ParsedFileTable {
    std::string fileName;              ///< 文件名（kCatalogFile* 之一——与 manifest.files 对位）
    std::vector<std::string> header;   ///< 表头（io 剥离转义后原文；列序＝CSV 物理列序）
    std::vector<std::string> cells;    ///< 数据区单元格原文（行优先展开＝row-major：
                                       ///<   row i 的 col j ＝ cells[i*columnCount + j]；
                                       ///<   行尾缺列由 io PadTrailing 补空——io 卡 §5.4）
    std::uint64_t columnCount = 0;     ///< 数据区列数（= header.size()，io 行列数策略后一致）
    std::uint64_t firstDataRowNo = 0;  ///< 首条数据行的物理行号（1 起，含方言标识行/
                                       ///<   表头行偏移——io 卡 §5.6 口径；错误定位用）

    bool operator==(const ParsedFileTable& o) const
    {
        return fileName == o.fileName && header == o.header && cells == o.cells
            && columnCount == o.columnCount && firstDataRowNo == o.firstDataRowNo;
    }
    bool operator!=(const ParsedFileTable& o) const { return !(*this == o); }
};

/**
 * @brief 目录包解析输入（卡 §14.2 ICatalogValidator::validate 的 parsed
 *        参数形态）。
 *
 * 前置条件（调用方契约）：本结构承载的是 io 文件层校验**通过**的解析
 * 结果（清单核对/文件间引用存在性/路径与预算防护已由 io §7.8 完成）；
 * 校验器对"必备表缺失"以调用方契约违约 fail-fast（std::invalid_argument
 * ——io 应已拦下，收到不完整输入＝装配层违约），不重复 io 的文件层
 * 职责（卡 §5.1 边界纪律——分工不越界）。
 */
struct ParsedCatalogInput {
    std::vector<ParsedFileTable> files;  ///< 各 CSV 解析表（必备五表；可含零数据行）

    /// 按文件名查找解析表；未命中返回 nullptr（只读借用——本结构持有数据）。
    const ParsedFileTable* find(const std::string& fileName) const noexcept;
};

// =====================================================================
// §5.3 业务校验报告（逐项可定位到文件/行/列）
// =====================================================================

/**
 * @brief 单条校验发现（卡 §14.2 返回值 CatalogValidationReport 的原子项；
 *        卡 §5.3"逐项可定位到文件/行/列"的承载）。
 *
 * 比较型字段（ERR-01）：单位/范围类码（SEL-CATALOG-UNIT-INVALID/
 * RANGE-INVALID/SEL-CURVE-EXTRAPOLATION-DENIED）要求"实际/期望/单位"
 * 三要素齐备——actualValue/expectedValue 承载数值侧（SI 域），actualText/
 * expectedText 承载文本侧（如实际单位 token vs 期望单位 token），unit
 * 承载比较单位（core 词表 token；文本比较可为空）。四组字段按码语义
 * 至少一组非空，允许同时携带（数值＋原文——NFR-COR-04 可追溯）。
 */
struct CatalogIssue {
    std::string code;        ///< SEL-* 稳定码（DiagCodes.hpp 常量——唯一书写点引用）
    std::string file;        ///< 定位：文件名（kCatalogFile*；manifest 级为 kCatalogFileManifest）
    std::uint64_t rowNo = 0; ///< 定位：物理行号（1 起；0＝文件级/清单级——卡 §5.3 定位纪律）
    std::string column;      ///< 定位：列名（空＝行级/文件级）
    std::string modelId;     ///< 关联型号稳定 ID（可空——曲线/兼容行携带 owner）
    std::string message;     ///< 语义描述（中文；诊断呈现文案权威在 diagnostics/ui——
                             ///<   此处为校验上下文素材，不注册第二文案源）
    std::optional<std::string> actualText;    ///< 比较型实际侧（原文/token）
    std::optional<std::string> expectedText;  ///< 比较型期望侧（token/词表值）
    std::optional<double> actualValue;        ///< 比较型实际值（SI 域）
    std::optional<double> expectedValue;      ///< 比较型期望值（SI 域；范围类＝边界）
    std::string unit;        ///< 比较单位 token（core 词表；文本比较可空）

    bool operator==(const CatalogIssue& o) const;
    bool operator!=(const CatalogIssue& o) const { return !(*this == o); }
};

/**
 * @brief 目录包业务校验报告（卡 §14.2 返回值——空报告＝通过）。
 *
 * 确定性：issues 经校验器按 (file, rowNo, column, code) 稳定排序后返回
 * （NFR-COR-02——同输入同报告序；卡 §5.3 逐项定位的读取纪律）。
 */
struct CatalogValidationReport {
    std::vector<CatalogIssue> issues;  ///< 全部发现（稳定序；空＝通过）

    /// 通过判定：零发现即通过（§14.2"空报告＝通过"）。
    bool ok() const noexcept { return issues.empty(); }

    bool operator==(const CatalogValidationReport& o) const { return issues == o.issues; }
    bool operator!=(const CatalogValidationReport& o) const { return !(*this == o); }
};

// =====================================================================
// v1 目录模板契约常量（formatVersion/文件名/词表——卡 §5.2 表的物化；
// P-IO-7 注册面：catalogPackageFileSchema() 声明见 CatalogProvider.hpp）
// =====================================================================

/// v1 目录包 schema 版本（NFR-DEP-04 精神——未知版本拒绝＋升级指引）。
inline constexpr const char* kCatalogFormatVersion = "1";

/// 包清单文件名（io JSON 通道）。
inline constexpr const char* kCatalogFileManifest = "manifest.json";
/// 电机型号主表文件名（io CSV 通道）。
inline constexpr const char* kCatalogFileMotors = "motors.csv";
/// 减速器型号主表文件名。
inline constexpr const char* kCatalogFileGearboxes = "gearboxes.csv";
/// 能力曲线表文件名（可含零数据行——卡 §5.2）。
inline constexpr const char* kCatalogFileCurves = "capability_curves.csv";
/// 兼容关系表文件名（可含零数据行＝无预声明兼容对——卡 §5.2）。
inline constexpr const char* kCatalogFileCompatibility = "compatibility.csv";

/// 能力曲线 owner 类别词表（capability_curves.csv owner_kind 列——卡 §5.2）。
inline constexpr const char* kCurveOwnerMotor = "motor";
inline constexpr const char* kCurveOwnerGearbox = "gearbox";

/// 量纲 token 词表（v1——卡 §6.1"字段字典词表：speed/torque/power/…"）。
inline constexpr const char* kQuantitySpeed = "speed";    ///< 角速度（SI rad/s）
inline constexpr const char* kQuantityTorque = "torque";  ///< 转矩（SI N·m）
inline constexpr const char* kQuantityPower = "power";    ///< 功率（SI W）

// =====================================================================
// 规范序列化与内容身份（卡 §4.2——CON-05 内容寻址）
// =====================================================================

/**
 * @brief 快照规范序列化文本（确定性——NFR-COR-02）。
 *
 * 序列化规则：固定成员顺序＋条目排序（motors/gearboxes 按 modelId 升序、
 * curves 按 curveId 升序、compatibility 按 motorId→gearboxId→mountKind
 * 升序）＋定点十进制数值格式化（17 位有效数字 round-trip 形态，无区域
 * 设置依赖）。同快照恒同文本；任何业务字段变更→不同文本→不同内容身份
 * （卡 §4.2"变更任何字节→新内容身份"）。
 *
 * @param snapshot [in] 目录快照（只读借用；调用方持有）
 * @return 规范文本（UTF-8；仅作摘要输入，不是持久化格式）
 */
std::string canonicalPackageText(const CatalogPackageSnapshot& snapshot);

/**
 * @brief 计算包内容身份＝SHA-256(canonicalPackageText(snapshot))
 *        （core ContentDigester 唯一哈希路径——卡 §4.2）。
 */
core::ContentIdentity computePackageContentIdentity(const CatalogPackageSnapshot& snapshot);

/**
 * @brief 计算曲线内容身份＝点集规范序列化摘要（卡 §6.1 contentIdentity 注；
 *        曲线版本变更的可定位判据——目录能力曲线变更→依赖切片失效，
 *        卡 §4.2/evidence §5.3）。
 */
core::ContentIdentity computeCurveContentIdentity(const PerformanceCurve& curve);

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_CATALOGTYPES_HPP
