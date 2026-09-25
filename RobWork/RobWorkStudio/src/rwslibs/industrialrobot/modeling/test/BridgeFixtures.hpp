/**
 * @file   BridgeFixtures.hpp
 * @brief  WP-13-T12 CanonicalBridge 测试共享夹具（test/ 内部——
 *         CommandFixtures.hpp 同款先例：测试域替身不入公共面）。
 *
 * 内容：
 *   - 值模型构造辅助（关节/连杆/资源清单条目——合法实例，经得起 Codec
 *     解码门 I-MDL 全量复核）；
 *   - 闭包域字节源内存替身 BridgeClosure（ObjectClosureView 实现——按
 *     token/按 id 两张字节表＋取回计数观测）；
 *   - 全量十类字段覆盖模型装配 makeFullDesign（六轴全旋转——§6.4 产品链
 *     口径；工具/场景/传动/摩擦/资源各一）。
 *
 * 设计依据：units/modeling.md §9.2（闭包域字节源注入形态）、§9.1（字段
 * 映射表）；任务契约 WP-13-T12 acceptance 1～5。命名空间
 * sdurws::ird::modeling::testbridge（与 testfixture 命令夹具域分离）。
 *
 * 线程安全：替身均单线程用例内使用（纯函数服务语义——串行调用）。
 */

#ifndef IRD_MODELING_TEST_BRIDGEFIXTURES_HPP
#define IRD_MODELING_TEST_BRIDGEFIXTURES_HPP

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/CanonicalBridge.hpp>
#include <sdurws/ird/modeling/Codec.hpp>
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/runtime/Description.hpp>  // runtime::InstallationPresetToken（预设词表）

namespace sdurws::ird::modeling::testbridge {

/// π 字面量（测试内独立抄写——期望值不引实现常量）。
inline constexpr double kPi = 3.14159265358979323846;

/// 测试用对象身份（随机生成——闭包以规范文本编址，同次运行内一致）。
inline core::ObjectId makeOid() { return core::ObjectId::generate(); }

/// 用户输入来源标记（methodTag 语法 [a-z0-9./_-]——"test-fixture" 合规）。
inline core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 旋转关节（Explicit 权威：axis/origin/bounds 全 Provided——合法实例；
/// zeroOffset 单位 rad，缺省 0）。
inline JointEntry makeRevoluteJoint(const core::ObjectId& oid,
                                    const std::string& name,
                                    double lowerRad = -1.0,
                                    double upperRad = 1.0,
                                    double zeroOffset = 0.0)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Revolute;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 1.0), userProv());
    joint.origin =
        core::SourcedValue<JointPose>::provided(JointPose{}, userProv());
    joint.zeroOffset = zeroOffset;  // 权威零位（rad）
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{lowerRad, upperRad}, userProv());
    return joint;
}

/// 连杆（物性可空——缺失走 DataInsufficient 降级面，不触发断言）。
inline LinkEntry makeLink(const core::ObjectId& oid, const std::string& name)
{
    LinkEntry link;
    link.objectId = oid;
    link.localName = name;
    return link;
}

/// 资源清单条目（Recorded 态——外部引用记录；digest 由用例给值）。
inline ResourceRef makeRecordedResource(const std::string& resourceId,
                                        const core::Digest256& digest)
{
    ResourceRef ref;
    ref.resourceId = resourceId;
    ref.contentDigest = digest;
    ref.state = ResourceState::Recorded;
    ref.externalRecord = ExternalResourceRecord{"D:/external/mesh.stl", digest};
    return ref;
}

/// 资源清单条目（Solidified 态——固化引用 (oid, cv)）。
inline ResourceRef makeSolidifiedResource(const std::string& resourceId,
                                          const core::Digest256& digest,
                                          const core::ObjectId& oid)
{
    ResourceRef ref;
    ref.resourceId = resourceId;
    ref.contentDigest = digest;
    ref.state = ResourceState::Solidified;
    SolidifiedRef solid;
    solid.objectId = oid;
    solid.contentVersion.bytes[0] = 0x5A;  // 非零 cv（固化内容版本——CON-01）
    ref.solidifiedObject = solid;
    return ref;
}

/// 恒等位姿外的显式位姿（平移 (x,y,z)＋绕 z 轴 yaw——期望值手算对照用；
/// yaw 单位 rad）。
inline JointPose explicitPose(double x, double y, double z, double yaw)
{
    const rw::math::Transform3D<double> t(
        rw::math::Vector3D<double>(x, y, z),
        rw::math::Rotation3D<double>(std::cos(yaw), -std::sin(yaw), 0.0,
                                     std::sin(yaw), std::cos(yaw), 0.0,
                                     0.0, 0.0, 1.0));
    return JointPose(t);
}

/// 恒等变换（逐元素构造——冒烟 header-only 纪律：Transform3D 默认构造
/// 引用框架库外联符号 Rotation3D::identity()，冒烟模式不可链接；与
/// runtime Description detail::identityTransform3D 同款纪律）。
inline rw::math::Transform3D<double> identityTransform3D()
{
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));
}

/**
 * @brief 内存闭包替身：按 token/按 id 两张字节表＋取回计数观测。
 *
 * 计数器用于确定性/同源绑定观测：部件对象确经闭包解析（tryObject 计数
 * 随引用数增长）＋重复 read 不走缓存（第二次 read 计数继续增长）。
 * mutable 计数不参与应答（并发只读语义的替身侧形态——单线程用例内使用）。
 */
class BridgeClosure final : public modeling::ObjectClosureView {
public:
    std::map<std::string, modeling::ClosureObject> byToken;  ///< token→对象
    std::map<std::string, modeling::ClosureObject> byId;     ///< oid 规范文本→对象
    mutable int tryObjectCalls = 0;                          ///< 部件解引用计数
    mutable int tryTokenCalls = 0;                           ///< 根路由计数

    /// 按 token 登记（根对象路由位——builder 的根定位入口）。
    void put(std::string token, const std::vector<std::uint8_t>& bytes)
    {
        modeling::ClosureObject obj;
        obj.objectTypeToken = token;
        obj.bytes = bytes;
        byToken[token] = std::move(obj);
    }
    /// 按身份登记（部件对象解引用位）。
    void put(const core::ObjectId& oid, std::string token,
             const std::vector<std::uint8_t>& bytes)
    {
        modeling::ClosureObject obj;
        obj.objectTypeToken = token;
        obj.bytes = bytes;
        byId[oid.toCanonical()] = std::move(obj);
    }

    std::optional<modeling::ClosureObject>
        tryObjectByToken(std::string_view objectTypeToken) const override
    {
        tryTokenCalls++;
        auto it = byToken.find(std::string(objectTypeToken));
        if (it == byToken.end()) { return std::nullopt; }
        return it->second;
    }
    std::optional<modeling::ClosureObject>
        tryObject(const core::ObjectId& objectId) const override
    {
        tryObjectCalls++;
        auto it = byId.find(objectId.toCanonical());
        if (it == byId.end()) { return std::nullopt; }
        return it->second;
    }
};

/// 对象变体编码（canonical 字节——闭包装配入口；编码失败＝夹具装配缺陷）。
inline std::vector<std::uint8_t> encode(const ObjectVariant& object)
{
    RobotDesignCodec codec;
    auto encoded = codec.encode(object, kCurrentFormatVersion);
    if (!encoded.ok()) {
        throw std::logic_error(std::string("fixture: 对象编码失败: ")
                               + encoded.error().detail);
    }
    return encoded.get();
}

/**
 * @brief 装配"全量覆盖十类字段"的模型＋闭包（六轴全旋转——§6.4 产品链
 *        口径；工具/场景/传动/摩擦/资源各一——每类映射行都有可断言载体）。
 *
 * @param closure [out] 部件对象字节登记入此闭包（根对象由调用方按需装配
 *                ——reader 面可刻意不装配以验证单根字节输入）
 * @return 根对象（调用方再行编码/装配）
 */
inline RobotDesign makeFullDesign(BridgeClosure& closure)
{
    RobotDesign design;
    design.displayName = "IRB6700";
    design.basePlacement.preset = runtime::InstallationPresetToken::Ground;
    design.basePlacement.basePosition =
        core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, 0.0), userProv());

    for (int i = 1; i <= 6; ++i) {
        design.joints.push_back(
            makeRevoluteJoint(makeOid(), "J" + std::to_string(i)));
        design.links.push_back(makeLink(makeOid(), "link" + std::to_string(i)));
    }
    design.links.push_back(makeLink(makeOid(), "link6_flange"));  // n+1（含基座连杆）
    design.links.front().localName = "base";

    const core::ObjectId toolOid = makeOid();
    ToolDefinition tool;
    tool.objectId = toolOid;
    tool.localName = "gripper";
    tool.tcpList.push_back(TcpEntry{
        "tip",
        rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(0.0, 0.0, 0.1),
            rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1)),
        ""});
    design.toolRefs = {toolOid};
    design.defaultTcp = TcpRef{toolOid, "tip"};
    closure.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));

    const core::ObjectId sceneOid = makeOid();
    SceneObject table;
    table.objectId = sceneOid;
    table.localName = "table";
    table.geometry = GeometryRef{
        "scene/table", testbridge::identityTransform3D(), GeometryKind::Mesh};
    design.sceneRefs = {sceneOid};
    closure.put(sceneOid, std::string(kSceneObjectObjectType), encode(table));

    const core::ObjectId dtOid = makeOid();
    DrivetrainDesign dt;
    dt.objectId = dtOid;
    for (int i = 0; i < 6; ++i) {
        dt.ratioPerJoint.push_back(
            core::SourcedValue<double>::provided(100.0, userProv()));
        FrictionEntry f;
        f.viscous = core::SourcedValue<double>::provided(0.5, userProv());
        f.coulomb = core::SourcedValue<double>::provided(1.0, userProv());
        f.bias = core::SourcedValue<double>::notProvided();
        dt.frictionPerJoint.push_back(f);
    }
    design.drivetrainRef = dtOid;
    closure.put(dtOid, std::string(kRobotDrivetrainObjectType), encode(dt));

    ResourceRef res;
    res.resourceId = "scene/table";
    res.contentDigest[0] = 0x21;  // 非零摘要（测试值——内容寻址面）
    res.state = ResourceState::Recorded;
    res.externalRecord =
        ExternalResourceRecord{"D:/external/table.stl", res.contentDigest};
    design.resourceManifest.push_back(res);

    return design;
}

}  // namespace sdurws::ird::modeling::testbridge

#endif  // IRD_MODELING_TEST_BRIDGEFIXTURES_HPP
