/**
 * @file   DynamicWorkCellCompiler.cpp
 * @brief  S7 DynamicWorkCell 编译器实现——CanonicalModel ＋ S6 产物 →
 *         rwsim DynamicWorkCell 的能力门控编译（Body/RigidDevice/质量惯量/
 *         摩擦/重力）＋§8.5 关联校验＋DynamicWorkCellConstView 实现。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S7 行、§5.3（失败回滚——RAII 瞬态对象）、
 *     §5.6（能力缺失/输入非法/编译失败正交）、§8.1（P-RT-3 已登记 L1：
 *     rwsim dynamics——本文件的设计依据面）、§8.2（显式设值＋只读包装）、
 *     §8.4（异常→稳定诊断转译——不吞异常）、§8.5（同源/Body 覆盖/设备
 *     一致/销毁顺序）、§9.1（dynamicWorkCellState）、§9.6（能力声明）、
 *     §8.8（摩擦 API 缺项的默认"无补丁方案"——选型登记 §15.4 v0.9）
 *   - 需求 MDL-06（原子性）、DYN-06（物性缺失降级非阻断）、CON-04（跳过
 *     状态是能力事实）、NFR-COR-01/02（确定性）、NFR-REL-05（用户诊断
 *     不含调用栈）
 *   - 任务契约 tasks/foundation/RT-T08.json（acceptance 1～3）
 *
 * ★ 编译模式说明：本文件调用大量基线库非模板符号（DynamicWorkCell/
 * RigidBody/FixedBody/RigidDevice 构造、MaterialDataMap 注册——实现于
 * sdurwsim 的 .cpp），**只在集成模式编译**（CMakeLists 按 TARGET
 * sdurw_kinematics 条件增列；冒烟模式无框架库可链，本文件不进构建——
 * 与 RT-T07 的 S6 分工同款，§15.4 v0.8/v0.9 登记）。
 *
 * 线程安全：compileDynamicWorkCell 无共享可变状态（每次调用全新对象树）；
 * DynamicWorkCellConstView 发布后只读。
 * 单位纪律：质量 kg、长度 m、惯量 kg·m²、粘性摩擦 N·m·s/rad、库仑摩擦
 * N·m、偏置 N·m、重力 m/s²（SI 真值——§4.4；CanonicalModel 已保证，本
 * 文件不做换算，只做结构映射）。
 * 坐标系读法：T_ab＝"b 系相对 a 系"（core §4.6）；BodyInfo::masscenter
 * 为体坐标系下表示（基线 Body.hpp 契约）＝canonical 连杆系/工具系下质心
 * ——承载帧与规范帧重合（承载变换恒等），故逐字映射、零换算。
 */

#include "DynamicWorkCellCompiler.hpp"

#include <sdurws/ird/runtime/Adapter.hpp>   // DynamicWorkCellConstView（视图契约面）
#include <sdurws/ird/runtime/Errors.hpp>    // token/registryCode/RuntimeError
#include <sdurws/ird/runtime/NameMap.hpp>   // buildRuntimeNameMap（名称唯一源）

#include <rwsim/dynamics/Body.hpp>             // BodyInfo/Body::getInfo/getName
#include <rwsim/dynamics/FixedBody.hpp>        // 基座连杆/有物性工具（落地/刚连）
#include <rwsim/dynamics/MaterialDataMap.hpp>  // 摩擦承载（§8.8 无补丁方案）
#include <rwsim/dynamics/RigidBody.hpp>        // 移动连杆（质量惯量承载）
#include <rwsim/dynamics/RigidDevice.hpp>      // 设备动力学子（基座体＋连杆体）

#include <rw/core/Exception.hpp>            // rw::core::Exception（§8.4 转译行 1）
#include <rw/kinematics/FixedFrame.hpp>     // 基座连杆 Body 承载帧
#include <rw/kinematics/MovableFrame.hpp>   // 移动连杆 Body 承载帧（RigidBody 强制）
#include <rw/models/Joint.hpp>              // 设备关节集交叉校验（§8.5 设备一致）
#include <rw/models/JointDevice.hpp>        // RigidDevice 的运动学模型输入
#include <rw/models/Object.hpp>             // Body 的对象载体
#include <rw/models/RigidObject.hpp>        // 每体对象（基线 Body 载体惯例）

#include <exception>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::runtime {
namespace {

// =====================================================================
// 局部工具（匿名命名空间——翻译单元内可见，不外泄符号）。
// =====================================================================

/**
 * @brief 判定一个规范连杆的物性三元组是否齐备（S7 门控与缺失清单的同一
 *        判定面——与 S5 hasDynamicWorkCell 派生口径逐字一致：mass/
 *        centerOfMass/inertia 均 Provided，§9.6；RT-T04 登记的"DWC Body
 *        集合＝链连杆"即本函数的消费面）。
 *
 * @param link [in] 规范连杆（只读）
 * @return true＝三元组全 Provided（可构造 Body）；false＝任一 NotProvided
 *         （能力缺失，非非法——§5.6）
 */
bool linkPhysicsProvided(const CanonicalLink& link)
{
    return link.mass.tryValue().has_value()
           && link.centerOfMass.tryValue().has_value()
           && link.inertia.tryValue().has_value();
}

/**
 * @brief 判定一个规范工具的物性三元组是否齐备（工具 Body 的构造条件——
 *        §8.5"Body 数＝有物性连杆数＋有物性工具数"的工具侧口径；工具物性
 *        缺项不阻止 DWC 构造〔RT-T04 登记的 gate 口径〕，仅不计入 Body）。
 */
bool toolPhysicsProvided(const CanonicalTool& tool)
{
    return tool.mass.tryValue().has_value()
           && tool.centerOfMass.tryValue().has_value()
           && tool.inertia.tryValue().has_value();
}

/**
 * @brief 以规范物性填充基线 BodyInfo（§8.2"每 Body 质量惯量"显式设值的
 *        唯一装配点——不依赖基线构造默认〔BodyInfo 默认 mass＝0、质心
 *        (0,0,0)、惯量未定〕）。
 *
 * @param mass    [in] 质量，单位 kg（前置：Provided——调用方先过齐备判定）
 * @param com     [in] 质心，体（连杆/工具）系下表示，单位 m（前置同上）
 * @param inertia [in] 惯量张量，质心系下表示，单位 kg·m²（前置同上；基线
 *                BodyInfo.inertia 语义＝"body frame around masscenter"——
 *                与 canonical M-2 基准逐字同义，零换算）
 * @return 就绪的 BodyInfo（material/objectType/integratorType 留基线默认
 *         空串——canonical 无材质/积分器概念，不发明数据）
 */
rwsim::dynamics::BodyInfo makeBodyInfo(const core::SourcedValue<double>& mass,
                                       const core::SourcedValue<rw::math::Vector3D<double>>& com,
                                       const core::SourcedValue<rw::math::InertiaMatrix<double>>&
                                           inertia)
{
    rwsim::dynamics::BodyInfo info;
    info.mass       = *mass.tryValue();      // 单位 kg
    info.masscenter = *com.tryValue();       // 单位 m（体系下表示）
    info.inertia    = *inertia.tryValue();   // 单位 kg·m²（质心系）
    return info;
}

}  // namespace

// =====================================================================
// compileDynamicWorkCell——S7 唯一入口（签名契约见私有头）。
// =====================================================================

DynamicWorkCellCompileOutcome compileDynamicWorkCell(
    const CanonicalModel& model, const WorkCellCompileOutcome& workCellOutcome)
{
    // ---------------------------------------------------------------
    // 异常边界（§8.4，与 S6 同构）：编译体全程在 try 内。顺序约束：
    //   1) RuntimeError 最先——本单元稳定失败语义透传（绝不能被误转译成
    //      dwc-compile-failed 覆盖原稳定码，否则错误归属失真）；
    //   2) std::bad_alloc→ResourceBudget（§5.5 内存不足行）；
    //   3) rw::core::Exception→DwcCompileFailed（translateRobWorkError 按
    //      stage="S7" 冻结分派——RT-AD-1 的 S7 通道，RT-T07 已实现）；
    //   4) 其余 std::exception→DwcCompileFailed（编译硬失败兜底）。
    // 转译不吞异常：catch 后一律 throw RuntimeError 终止编译；S7 局部
    // 瞬态对象（承载帧/Body/Device/DWC）经栈回退自动析构（MDL-06）；已
    // 挂入 WC 的承载帧属 WC 的瞬态增量——S7 失败时调用方（RT-T11 编译
    // 事务）按 MDL-06 弃置整个 WC，不对外发布（快照唯一发布点 S10 不达）。
    // ---------------------------------------------------------------
    try {
        // ===== 第 1 步：名称源唯一化＋门控判定材料准备。
        // 映射与 S6 同源（纯函数、同 model 同映射）；Body 承载帧名/关节名/
        // Tcp 名全部取自映射（R-4：编译器不拼名）。
        const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
        const RobotChain& chain = model.chain();

        // 重走连杆物性四态得缺失清单（链序——诊断与 §9.1 缺失对象清单的
        // 确定性序），并记录首个缺失连杆（警告 subject 定位，§9.6）。该
        // 判定与 S5 的 hasDynamicWorkCell 派生同一口径（linkPhysicsProvided），
        // 此处重走是为拿到"逐对象"的缺失定位（S5 只产出位，无清单）。
        std::vector<core::ObjectId> missingLinks;
        const CanonicalLink* firstMissing = nullptr;
        for (const CanonicalLink& link : chain.links) {
            if (!linkPhysicsProvided(link)) {
                if (firstMissing == nullptr) {
                    firstMissing = &link;  // 链序首个缺失——警告 subject
                }
                missingLinks.push_back(link.objectId);
            }
        }

        // 防御性互核：能力位（S5 派生，PA-1 单一权威）与本次重走结论必须
        // 一致（gate==true ⟺ 缺失清单为空，§9.6 派生规则）——不一致说明
        // S5 产物被越权构造或派生口径分叉，属内部违约，fail-fast 不静默
        // 放行（NFR-COR-03；两方向都核）。
        const bool gate = model.capabilities().hasDynamicWorkCell;
        if (gate != missingLinks.empty()) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; 能力位与连杆物性重检不一致——S5 派生内部违约");
        }

        // ===== 第 2 步：门控为假→能力缺失降级（§5.2 S7"跳过构造……不算
        // 失败"；§5.6"缺失不妨碍发布"）。零副作用返回：不触碰 WC、不构造
        // 任何基线对象；警告级诊断恰一条（§9.6"每个缺失能力一条警告"），
        // subject 定位首个缺失对象。正常返回不抛——能力缺失绝不是失败。
        if (!gate) {
            DynamicWorkCellCompileOutcome outcome;
            outcome.status = DwcCompileStatus::SkippedNoPhysics;
            outcome.deviceName = workCellOutcome.deviceName;
            outcome.workCell = workCellOutcome.workCell;  // 同源透传（§8.5 顺序不变）
            outcome.skippedObjects = missingLinks;        // §9.1 缺失对象清单

            // RT-CAPABILITY-MISSING＝§10.11 v0.3 冻结的警告级事件码（无枚举
            // 对应——与 RT-ROBWORK-ERROR 同类，字面串按冻结清单使用，码值
            // 权威归 diagnostics StableCodeRegistry，PA-1）。firstMissing
            // 非空由 !gate ⟺ missingLinks 非空保证（上方互核已锁定）。
            outcome.warnings.push_back(core::DiagnosticRecord::make(
                std::string{"RT-CAPABILITY-MISSING"},
                std::optional<core::ObjectId>(firstMissing->objectId),
                std::optional<std::string>(firstMissing->localName),
                std::nullopt,
                std::string{"S7 DynamicWorkCell 编译（能力门控降级，§5.2 S7/§5.6）"},
                std::string{"链连杆质量/质心/惯量未提供（NotProvided）——"
                            "hasDynamicWorkCell=false，DWC 跳过构造（能力事实，非失败）"},
                std::string{"建模侧补全连杆物性后重新编译；下游按能力声明处置"
                            "（dynamics→DYN-06 DataInsufficient 域判定）"}));
            return outcome;
        }

        // ===== 第 3 步：门控为真——DWC 构造。前置防御：S6 产物 WC 句柄
        // 必须非空（空输入＝调用方契约违约，§8.4 空句柄行 fail-fast）。
        if (workCellOutcome.workCell.isNull()) {
            throw RuntimeError(RuntimeErrorCode::RobWorkError,
                               "runtime/robwork-null-handle: S7 收到空 WorkCell 句柄"
                               "（S6 产物缺失）");
        }
        const rw::core::Ptr<rw::models::WorkCell> workCell = workCellOutcome.workCell;

        // ---- 3a. WC 设备解析（RigidDevice 的运动学模型输入）：设备名来自
        // 映射（Device 作用域身份条目——S6 已按名写入 WC），此处按名取回并
        // 收敛为 JointDevice（SerialDevice 是其子类——基线层次实测）。取不
        // 到＝S6 产物与映射失配，内部违约。
        const RuntimeName deviceName =
            nameMap.resolveObjectId(chain.robotObjectId).get();
        const rw::core::Ptr<rw::models::JointDevice> jointDevice =
            workCell->findDevice(deviceName.fullName).cast<rw::models::JointDevice>();
        if (jointDevice.isNull()) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; WC 中未找到设备 JointDevice（name="
                                   + deviceName.fullName + "）——S6 产物与映射失配");
        }

        // ---- 3b. 基座连杆 Body（link[0]，物性必齐备——门控保证）。
        // 承载帧：FixedFrame（基座落地固定——移动语义不适用），名＝映射
        // Body 条目（§7.1"IRB6700.<base_link>.body"），挂 BaseFrame 下；
        // 变换恒等（承载帧与规范连杆系重合——质心坐标零换算）。
        const CanonicalLink& baseLink = chain.links.front();
        const std::string baseBodyName =
            scopedFullName(nameMap, baseLink.objectId, NameScope::Body);
        const std::string baseFrameName =
            scopedFullName(nameMap, baseLink.objectId, NameScope::BaseFrame);
        const rw::core::Ptr<rw::kinematics::Frame> baseFrame =
            workCell->findFrame(baseFrameName);  // 非拥有句柄（StateStructure 持有）
        if (baseFrame.isNull()) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; WC 中未找到基座帧（name=" + baseFrameName
                                   + "）——S6 产物与映射失配");
        }
        rw::core::Ptr<rw::kinematics::FixedFrame> baseCarrier =
            rw::core::ownedPtr(new rw::kinematics::FixedFrame(
                baseBodyName, detail::identityTransform3D()));
        workCell->addFrame(baseCarrier, baseFrame);

        // 基座体：FixedBody（落地固定；BodyInfo 逐字承载 canonical 物性——
        // 数据不因"固定"语义丢失，下游按需读取）。RigidObject 挂 WC（基线
        // 加载器惯例——WC 持对象清单，Body 经对象引帧）；ownedPtr＝引用
        // 计数接管（裸 Ptr(T*) 构造不持引用计数，混用即悬垂——基线约定）。
        rw::core::Ptr<rw::models::RigidObject> baseObject =
            rw::core::ownedPtr(new rw::models::RigidObject(
                rw::core::Ptr<rw::kinematics::Frame>(baseCarrier)));
        workCell->add(baseObject);
        const rwsim::dynamics::Body::Ptr baseBody = rw::core::ownedPtr(
            new rwsim::dynamics::FixedBody(
                makeBodyInfo(baseLink.mass, baseLink.centerOfMass, baseLink.inertia),
                baseObject));

        // ---- 3c. 移动连杆 Body（i＝1..N，链序）：承载帧必须是 MovableFrame
        // （RigidBody 构造强制——R-2 实测结论 2），名＝映射 Body 条目，挂
        // 对应连杆帧下。RigidDevice 的 RigidLink 经承载帧向上回溯首个关节
        // 定位 jointIdx（基线 RigidDevice.cpp 实测）——承载帧挂在连杆帧
        // （关节子帧）之下正是该回溯成立的结构前提。
        std::vector<std::pair<rwsim::dynamics::BodyInfo, rw::models::Object::Ptr>>
            deviceLinkBodies;  // RigidDevice 构造输入（链序＝确定序）
        for (std::size_t i = 1; i < chain.links.size(); ++i) {
            const CanonicalLink& link = chain.links.at(i);
            const std::string carrierName =
                scopedFullName(nameMap, link.objectId, NameScope::Body);
            // 末连杆帧作用域＝Flange、其余＝LinkFrame（与 S6 写入侧同一
            // 判定——§7.1 范围表；末下标＝links.size()−1，off-by-one 直查）。
            const bool isLastLink = (i + 1 == chain.links.size());
            const std::string linkFrameName =
                scopedFullName(nameMap, link.objectId,
                               isLastLink ? NameScope::Flange : NameScope::LinkFrame);
            const rw::core::Ptr<rw::kinematics::Frame> linkFrame =
                workCell->findFrame(linkFrameName);
            if (linkFrame.isNull()) {
                throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                                   "stage=S7; WC 中未找到连杆帧（name=" + linkFrameName
                                       + "）——S6 产物与映射失配");
            }
            // MovableFrame 仅有单参构造（基线 MovableFrame.hpp 实测）——
            // 初始变换恒为恒等阵（基线构造文档语义），恰为本编译器的承载
            // 语义（承载帧与规范连杆系重合）；恒等由基线构造保证，非默认
            // 残留（承载变换不属于被消费字段，恒等即规范语义）。
            rw::core::Ptr<rw::kinematics::MovableFrame> carrier =
                rw::core::ownedPtr(new rw::kinematics::MovableFrame(carrierName));
            workCell->addFrame(carrier, linkFrame);

            rw::core::Ptr<rw::models::RigidObject> object =
                rw::core::ownedPtr(new rw::models::RigidObject(
                    rw::core::Ptr<rw::kinematics::Frame>(carrier)));
            workCell->add(object);
            deviceLinkBodies.emplace_back(
                makeBodyInfo(link.mass, link.centerOfMass, link.inertia), object);
        }

        // ---- 3d. 有物性工具 Body：FixedBody 锚在其既有 Tcp 帧（S6 产物，
        // 名＝映射 Tcp 条目）。不新造承载帧名——§7.1 Body 范围仅覆盖 link，
        // 工具体若新造 MovableFrame 名须扩映射生成规则（契约面变更），而
        // FixedBody 直接复用 Tcp 条目名（R-4：零名称拼装）；工具体不进
        // RigidDevice 连杆序列（RigidLink 会把体挂到回溯出的末关节——
        // canonical 未声明该耦合语义），作为独立体承载物性数据（§8.5 Body
        // 覆盖口径：有物性工具各计一体）。无物性工具不计体、不构造（能力
        // 事实——工具物性缺项不阻止 DWC 构造，RT-T04 登记）。
        std::vector<rwsim::dynamics::Body::Ptr> allBodies;
        allBodies.push_back(baseBody);
        for (const CanonicalTool& tool : model.tools()) {
            if (!toolPhysicsProvided(tool)) {
                continue;  // 工具物性缺项——仅无体，DWC 照常构造
            }
            const std::string tcpName =
                scopedFullName(nameMap, tool.objectId, NameScope::Tcp);
            const rw::core::Ptr<rw::kinematics::Frame> tcpFrame =
                workCell->findFrame(tcpName);
            if (tcpFrame.isNull()) {
                throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                                   "stage=S7; WC 中未找到工具 Tcp 帧（name=" + tcpName
                                       + "）——S6 产物与映射失配");
            }
            rw::core::Ptr<rw::models::RigidObject> toolObject =
                rw::core::ownedPtr(new rw::models::RigidObject(tcpFrame));
            workCell->add(toolObject);
            allBodies.push_back(rw::core::ownedPtr(
                new rwsim::dynamics::FixedBody(
                    makeBodyInfo(tool.mass, tool.centerOfMass, tool.inertia),
                    toolObject)));
        }

        // ---- 3e. RigidDevice＋DynamicWorkCell 整体构造：基座体＋移动连杆
        // 体序列＋WC 设备（基线构造器对 Body/Device 一次注册进 StateStructure
        // ——R-2 实测结论 1）。约束/控制器列表为空（canonical 无此概念，
        // 不发明数据）。
        rw::core::Ptr<rwsim::dynamics::RigidDevice> rigidDevice =
            rw::core::ownedPtr(new rwsim::dynamics::RigidDevice(
                baseBody, deviceLinkBodies, jointDevice));
        for (const rwsim::dynamics::Body::Ptr& body : rigidDevice->getLinks()) {
            allBodies.push_back(body);  // RigidLink（基线包装体）入全体清单
        }

        rw::core::Ptr<rwsim::dynamics::DynamicWorkCell> dwc =
            rw::core::ownedPtr(new rwsim::dynamics::DynamicWorkCell(
                workCell, allBodies, allBodies,
                rwsim::dynamics::DynamicWorkCell::ConstraintList{},
                rwsim::dynamics::DynamicWorkCell::DeviceList{rigidDevice},
                rwsim::dynamics::DynamicWorkCell::ControllerList{}));

        // ---- 3f. 显式设值：DWC 重力＝权威 world.gravityWorld（§8.2"DWC
        // 重力"行——基线构造默认 (0,0,−9.82) 与 canonical 默认 (0,0,−9.81)
        // 数值可区分：读回 9.81 即设值路径执行的直接证据；DYN-01 世界系
        // 重力恒定）。MotorForceLimits：canonical 无对应字段，不发明数据
        // （留基线零值——非被消费字段，不在 §8.2 显式设值清单）。
        dwc->setGravity(model.world().gravityWorld);

        // ---- 3g. 摩擦承载（§8.8"无补丁方案"默认）：rwsim 无逐关节摩擦
        // 一等职（R-2 实测结论 3——摩擦仅为 material-pair 载体，挂在 DWC
        // 的 MaterialDataMap 上，故只能在 DWC 构造后经可变访问器注册）。
        // 对三元组全 Provided 的关节：注册确定性材质（id＝该关节映射全名
        // ——名称唯一源仍是映射，R-4 合规）并在 (mat, mat) 对上登记 Custom
        // 摩擦数据，参数名冻结为 fv/fc/bias（MDL-16 三元；粘性 N·m·s/rad、
        // 库仑 N·m、偏置 N·m——dynamics 阶段按本约定检索）。不改 rwsim
        // 源码（SA-02 零补丁）；hasFrictionModel 能力位仍由 S5 按内容派生，
        // 本段只是数据承载，不产生能力判定。
        for (const CanonicalJoint& joint : chain.joints) {
            const auto fv = joint.friction.viscous.tryValue();
            const auto fc = joint.friction.coulomb.tryValue();
            const auto bias = joint.friction.bias.tryValue();
            if (!fv.has_value() || !fc.has_value() || !bias.has_value()) {
                continue;  // 非全 Provided→不承载（能力位已表达缺项，不伪造数据）
            }
            const std::string matId =
                nameMap.resolveObjectId(joint.objectId).get().fullName;
            rwsim::dynamics::FrictionData data;
            // FrictionType 枚举在 rwsim::dynamics 命名空间（非类内嵌套——
            // MaterialDataMap.hpp 实测）；Custom 槽位的 parameters 为自由
            // 名值对，是本承载方案的 API 基础。
            data.type = static_cast<int>(rwsim::dynamics::Custom);
            data.typeName = "ird-joint-friction";  // 承载约定名（§15.4 v0.9 登记）
            data.parameters.emplace_back("fv", rw::math::Q(1, *fv));
            data.parameters.emplace_back("fc", rw::math::Q(1, *fc));
            data.parameters.emplace_back("bias", rw::math::Q(1, *bias));
            // 材质注册＋材质对摩擦数据（材质描述为固定文本——非消费字段）。
            dwc->getMaterialData().add(matId, "ird joint friction carrier");
            dwc->getMaterialData().addFrictionData(matId, matId, data);
        }

        // ===== 第 4 步：§8.5 关联校验（全部通过才产出；违者即编译器内部
        // 缺陷——fail-fast 不放行，与 S9 自检同类）。

        // ---- 4a. 同源校验：DWC 必须由本 WC 构造（§8.5 行 1 防御性断言
        // "DWC.getWorkCell() 指向本 WC"——构造输入即本 WC，此处核对基线
        // 持有面未被偷换）。
        if (dwc->getWorkCell().get() != workCell.get()) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; §8.5 同源校验失败：DWC.getWorkCell() 非 S6 产物");
        }

        // ---- 4b. Body 覆盖：Body 数＝有物性连杆数＋有物性工具数（§8.5
        // 行 2）；且每个有物性连杆经映射 Body 名恰命中一体（缺项）、总数
        // 相等（超项）。缺项与超项均为 DwcCompileFailed。
        std::size_t expectedBodies = chain.links.size();  // 门控保证：全连杆有物性
        for (const CanonicalTool& tool : model.tools()) {
            if (toolPhysicsProvided(tool)) {
                ++expectedBodies;
            }
        }
        if (dwc->getBodies().size() != expectedBodies) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; §8.5 Body 覆盖校验失败：实际 "
                                   + std::to_string(dwc->getBodies().size())
                                   + " 体，期望 " + std::to_string(expectedBodies)
                                   + " 体（有物性连杆＋有物性工具）");
        }
        for (const CanonicalLink& link : chain.links) {
            const std::string bodyName =
                scopedFullName(nameMap, link.objectId, NameScope::Body);
            if (dwc->findBody(bodyName).isNull()) {
                throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                                   "stage=S7; §8.5 Body 覆盖校验失败：连杆体缺失（"
                                       + bodyName + "）");
            }
        }

        // ---- 4c. 设备一致：RigidDevice 各连杆体承载帧向上回溯的首个关节
        // 名集合＝WC 设备关节名集合（逐关节名交叉、经映射——§8.5 行 3）。
        // 该校验能捕获"承载帧挂错子树→RigidLink 关节定位漂移"的内部缺陷
        // （RigidLink 构造期按同款遍历定位 jointIdx，两面对不上即定位失真）。
        std::set<std::string> jointsViaBodies;
        for (const rwsim::dynamics::Body::Ptr& body : rigidDevice->getLinks()) {
            // getBodyFrame() 返回裸 Frame*（基线 RW_USE_PTR 未定义形态——
            // Body.hpp 实测）；所有权在 StateStructure，仅作只读回溯。
            rw::kinematics::Frame* frame = body->getBodyFrame();
            const rw::models::Joint* joint = nullptr;
            while (frame != nullptr && joint == nullptr) {
                joint = dynamic_cast<const rw::models::Joint*>(frame);
                frame = frame->getParent();  // 无状态父帧（树内回溯——与基线
                                             // RigidLink 构造期同款遍历）
            }
            if (joint == nullptr) {
                throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                                   "stage=S7; §8.5 设备一致校验失败：连杆体无关节父"
                                   "（body=" + body->getName() + "）");
            }
            jointsViaBodies.insert(joint->getName());
        }
        std::set<std::string> jointsViaDevice;
        for (const rw::models::Joint* j : jointDevice->getJoints()) {
            jointsViaDevice.insert(j->getName());
        }
        if (jointsViaBodies != jointsViaDevice) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=S7; §8.5 设备一致校验失败：RigidDevice 关节集"
                               "与 WC 设备关节集不一致");
        }

        // ===== 第 5 步：产物装配（§8.5 销毁顺序：dynamicWorkCell 在
        // workCell 之前声明——析构逆序使 WC 成员先出作用域而 WC 对象因
        // DWC 自持引用存活至 DWC 释放；RT-AD-3 以引用计数观测钉住）。
        DynamicWorkCellCompileOutcome outcome;
        outcome.status = DwcCompileStatus::Compiled;
        outcome.deviceName = deviceName.fullName;
        outcome.dynamicWorkCell = dwc;
        outcome.workCell = workCell;
        return outcome;
    }
    catch (const RuntimeError&) {
        throw;  // 本单元稳定失败语义（各段码面）——透传，不转译（见异常边界注释）
    }
    catch (const std::bad_alloc& ex) {
        // 内存不足→ResourceBudget（§5.5——对象级清理由 RAII 完成：栈回退
        // 时 DWC/Body/承载帧局部对象已析构）。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S7", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::ResourceBudget,
                           std::string("stage=S7; ") + record.cause);
    }
    catch (const rw::core::Exception& ex) {
        // RobWork 基线异常→dwc-compile-failed（§8.4 表行 1 的 S7 通道——
        // translateRobWorkError 按 stage="S7" 冻结分派；RT-CPX-1 的稳定码
        // 通道）。detail 携带 stage 与消息摘要；诊断记录本身由 RT-T11 链
        // 全量登记。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S7", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                           std::string("stage=S7; ") + record.cause);
    }
    catch (const std::exception& ex) {
        // 其余标准异常＝编译硬失败兜底（不吞不续——MDL-06）。转译行：非 rw
        // 类型→RT-ROBWORK-ERROR 事件码面。
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S7", model.chain().robotObjectId);
        throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                           std::string("stage=S7; ") + record.cause);
    }
}

// =====================================================================
// DynamicWorkCellConstView——只读视图实现（契约注释见公共头 Adapter.hpp）。
// =====================================================================

DynamicWorkCellConstView::DynamicWorkCellConstView(
    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> dynamicWorkCell)
    : m_dynamicWorkCell(std::move(dynamicWorkCell))
{
    // 空句柄＝调用方契约违约（DWC 缺失场景由 capability 表达而非空视图，
    // §9.6——hasDynamicWorkCell=false 时快照侧根本不持有本视图）；按 §8.4
    // "空 Ptr→robwork-error＋操作名" fail-fast（与 WorkCellConstView 同款）。
    if (m_dynamicWorkCell.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "runtime/robwork-null-handle: DynamicWorkCellConstView 构造"
                           "收到空 DynamicWorkCell 句柄");
    }
}

const rwsim::dynamics::DynamicWorkCell&
    DynamicWorkCellConstView::dynamicWorkCell() const noexcept
{
    // 构造已保证非空——解引用安全（前置在唯一构造入口集中执行）。
    return *m_dynamicWorkCell;
}

rw::core::Ptr<const rwsim::dynamics::Body>
    DynamicWorkCellConstView::findBody(std::string_view name) const noexcept
{
    // 基线 findBody 入参为 const std::string&（string_view 无隐式转换——
    // 显式构造；未命中返回空 Ptr，与基线语义一致；返回面收敛为 const——
    // §8.2 只读包装：Body 的 setForce 等可变方法在类型层不可达）。
    return m_dynamicWorkCell->findBody(std::string(name))
        .template cast<const rwsim::dynamics::Body>();
}

std::size_t DynamicWorkCellConstView::bodyCount() const noexcept
{
    // 基线 getBodies 返回全体清单（_allbodies——含 FixedBody/RigidBody）。
    return m_dynamicWorkCell->getBodies().size();
}

}  // namespace sdurws::ird::runtime
