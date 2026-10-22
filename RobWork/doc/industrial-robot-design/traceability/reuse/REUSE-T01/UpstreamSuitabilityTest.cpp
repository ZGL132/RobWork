/**
 * @file UpstreamSuitabilityTest.cpp
 * @brief REUSE-T01 公共 API 适用性探测；不是生产复用正向证据。
 *
 * 按 audit-contract §2 验证候选能力，不复制产品求解器。
 * 模型仅为测试私有夹具，State/求解器在单用例内独占，不授予业务构造权限。
 */
#include <gtest/gtest.h>
#include <rw/invkin/ParallelIKSolver.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/LinearAlgebra.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Polynomial.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/geometry/GeometryUtil.hpp>
#include <rw/models/ParallelDevice.hpp>
#include <rw/models/ParallelLeg.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rwsim/dynamics/FixedBody.hpp>
#include <rwsim/dynamics/RigidDevice.hpp>
#include <rwsim/util/RecursiveNewtonEuler.hpp>
#include <rwsimlibs/bullet/BtSimulator.hpp>
#include <rw/trajectory/LinearInterpolator.hpp>
#include <rw/trajectory/LloydHaywardBlend.hpp>
#include <memory>

// 验证建模 SPD 判定的候选能力：上游特征值求解能否保留排序及极小尺度。
TEST(ReuseAudit, SymmetricEigenvaluesAcrossScales) {
    for (double scale : {1.0, 1e-150, 1e-300}) {
        Eigen::MatrixXd m = Eigen::MatrixXd::Zero(3, 3);
        m.diagonal() << scale, 2 * scale, 3 * scale;
        const auto decomposition = rw::math::LinearAlgebra::eigenDecompositionSymmetric(m);
        ASSERT_EQ(3, decomposition.second.size());
        for (int i = 0; i < 3; ++i)
            EXPECT_NEAR(i + 1.0, decomposition.second[i] / scale, 1e-12);
    }
}

// GAP-IK：同一帧链的单 leg 视图可否调用掩码 IK；不新建另一份模型链。
TEST(ReuseAudit, SingleLegMaskedPositionOnExistingFrames) {
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;
    StateStructure structure;
    auto* base = new FixedFrame("base", Transform3D<>());
    auto* joint = new RevoluteJoint("joint", Transform3D<>());
    auto* tcp = new FixedFrame("tcp", Transform3D<>(Vector3D<>(1, 0, 0)));
    structure.addFrame(base, structure.getRoot());
    structure.addFrame(joint, base);
    structure.addFrame(tcp, joint);
    joint->setActive(true);
    State state = structure.getDefaultState();
    ParallelLeg leg(std::vector<Frame*>{base, joint, tcp});
    ParallelDevice device(ParallelDevice::Legs{&leg}, "view", state);
    device.setBounds({Q(1, -3.14), Q(1, 3.14)});
    rw::invkin::ParallelIKSolver solver(&device);
    solver.setMaxIterations(100);
    solver.setMaxError(1e-10);
    VectorND<6, bool> enabled;
    for (int i = 0; i < 6; ++i) enabled[i] = i < 3;
    // 目标姿态故意与关节角不一致，证明关掉姿态约束确实参与求解。
    rw::invkin::ParallelIKSolver::Target target(tcp,
        Transform3D<>(Vector3D<>(std::cos(0.4), std::sin(0.4), 0)), enabled);
    const auto solutions = solver.solve(std::vector<rw::invkin::ParallelIKSolver::Target>{target}, state);
    ASSERT_EQ(1u, solutions.size());
    EXPECT_NEAR(0.4, solutions.front()[0], 1e-8);
    EXPECT_DOUBLE_EQ(0.0, joint->getData(state)[0]); // 调用者 State 未被修改。
}

// GAP-TRJ：静止端点可构成五次 smoothstep；这不证明任意端导数或七次时间律等价。
TEST(ReuseAudit, LloydBlendRestToRestQuintic) {
    using namespace rw::trajectory;
    LinearInterpolator<double> start(0.0, 0.0, 1.0);
    LinearInterpolator<double> end(1.0, 1.0, 1.0);
    LloydHaywardBlend<double> blend(&start, &end, 0.5, 7.5);
    EXPECT_DOUBLE_EQ(0.0, blend.x(0.0));
    EXPECT_DOUBLE_EQ(1.0, blend.x(1.0));
    EXPECT_NEAR(0.5, blend.x(0.5), 1e-14);
    EXPECT_NEAR(0.0, blend.dx(0.0), 1e-14);
    EXPECT_NEAR(0.0, blend.dx(1.0), 1e-14);
    EXPECT_NEAR(0.0, blend.ddx(0.0), 1e-14);
    EXPECT_NEAR(0.0, blend.ddx(1.0), 1e-14);
}

// GAP-RNE：单移动关节的广义力应为 m(a-g)，不能把力矩输出当作移动力。
TEST(ReuseAudit, PrismaticWrenchProjectionAgainstNewtonLaw) {
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;
    using namespace rwsim::dynamics;
    StateStructure structure;
    auto* base = new FixedFrame("base", Transform3D<>());
    auto* joint = new PrismaticJoint("slider", Transform3D<>());
    auto* tcp = new FixedFrame("tcp", Transform3D<>());
    auto* carrier = new MovableFrame("body");
    structure.addFrame(base, structure.getRoot());
    structure.addFrame(joint, base);
    structure.addFrame(tcp, joint);
    structure.addFrame(carrier, joint);
    joint->setActive(true);
    const State state = structure.getDefaultState();
    const auto serial = rw::core::ownedPtr(new SerialDevice(base, tcp, "slider", state));
    BodyInfo info;
    info.mass = 2.0; // kg；独立解析真值 F=2*(3+9.81) N。
    info.masscenter = Vector3D<>::zero();
    info.inertia = InertiaMatrix<>(1, 1, 1);
    const auto baseObject = rw::core::ownedPtr(new RigidObject(base));
    const auto baseBody = rw::core::ownedPtr(new FixedBody(info, baseObject));
    const auto linkObject = rw::core::ownedPtr(new RigidObject(carrier));
    const auto rigid = rw::core::ownedPtr(new RigidDevice(baseBody, {{info, linkObject}}, serial));
    rwsim::util::RecursiveNewtonEuler solver(rigid);
    ASSERT_TRUE(solver.validate());
    solver.setGravity(Vector3D<>(0, 0, -9.81));
    const auto wrenches = solver.solve(state, Q(1, 0.0), Q(1, 3.0));
    ASSERT_EQ(2u, wrenches.size()); // DOF+1，最后一项为工具载荷。
    EXPECT_NEAR(25.62, wrenches[0].force()[2], 1e-12);
    const auto motorTorques = solver.solveMotorTorques(state, Q(1, 0.0), Q(1, 3.0));
    // 明确记录接口差异：现有 torque API 对移动关节仍投影 torque，不能直接复用它。
    EXPECT_NEAR(0.0, motorTorques[0], 1e-12);
}

// GAP-TRJ：通用 Polynomial 已能求七次及 jerk，不把“无命名七次轨迹类”当作缺库。
TEST(ReuseAudit, ExistingPolynomialSepticWithThirdDerivative) {
    const rw::math::Polynomial<> p(std::vector<double>{0, 0, 0, 0, 35, -84, 70, -20});
    for (double end : {0.0, 1.0}) {
        const auto derivatives = p.evaluateDerivatives(end, 3);
        EXPECT_NEAR(end, derivatives[0], 1e-12);
        for (int order = 1; order <= 3; ++order)
            EXPECT_NEAR(0.0, derivatives[order], 1e-12);
    }
    const auto midpoint = p.evaluateDerivatives(0.5, 3);
    EXPECT_NEAR(0.5, midpoint[0], 1e-12);
    EXPECT_NEAR(-52.5, midpoint[3], 1e-12);
}

// 替换三处手写 SVD 前核对排序、奇异形态与矩阵重构；不依赖旧 Jacobi 实现作真值。
TEST(ReuseAudit, PublicSvdSingularAndWellConditioned) {
    for (double last : {0.0, 0.5}) {
        Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(3, 3);
        matrix.diagonal() << 3.0, 2.0, last;
        Eigen::MatrixXd u, v;
        Eigen::VectorXd sigma;
        rw::math::LinearAlgebra::svd(matrix, u, sigma, v);
        ASSERT_EQ(3, sigma.size());
        EXPECT_NEAR(3.0, sigma[0], 1e-12);
        EXPECT_NEAR(2.0, sigma[1], 1e-12);
        EXPECT_NEAR(last, sigma[2], 1e-12);
        EXPECT_LE((u * sigma.asDiagonal() * v.transpose() - matrix).norm(), 1e-12);
    }
}

// 建模可优先用更高层 GeometryUtil；排序及张量重构在公开 API 边界验证。
TEST(ReuseAudit, PrincipalInertiaFromGeometryUtil) {
    const auto result = rw::geometry::GeometryUtil::calculatePrincipalInertia(
        rw::math::InertiaMatrix<>(1.0, 2.0, 3.0));
    EXPECT_NEAR(1.0, result.second[0], 1e-12);
    EXPECT_NEAR(2.0, result.second[1], 1e-12);
    EXPECT_NEAR(3.0, result.second[2], 1e-12);
}

// GAP-FD 限制探测：当前 Bullet 的公共 addDevice 明确拒绝，不能把已链接误认为力控制已接通。
TEST(ReuseAudit, BulletDynamicDeviceInsertionIsNotImplemented) {
    rwsimlibs::bullet::BtSimulator simulator;
    rw::kinematics::State state;
    EXPECT_THROW(simulator.addDevice(nullptr, state), rw::core::Exception);
    // 此用例证明未实现入口，未运行机器人 force→step；不构成 DYN-05 通过。
}

// GAP-IK：Euler分量约束与参考系EAA分量约束不是同一个误差空间。
TEST(ReuseAudit, EulerMaskCannotBeSubstitutedByEaaRows) {
    using namespace rw::math;
    // RobWork RPY 参数顺序是 yaw/pitch/roll，产品字段顺序是 roll/pitch/yaw。
    // 仅 roll 改变；固定 yaw/pitch 时，空间 EAA 的 y/z 仍出现非零分量。
    const auto actual = RPY<>(0.7, 0.4, 0.6).toRotation3D();
    const auto desired = RPY<>(0.7, 0.4, 1.2).toRotation3D();
    const RPY<> actualAngles(actual), desiredAngles(desired);
    EXPECT_NEAR(0.0, desiredAngles[0] - actualAngles[0], 1e-12); // yaw 固定。
    EXPECT_NEAR(0.0, desiredAngles[1] - actualAngles[1], 1e-12); // pitch 固定。
    const EAA<> spatialError = actual * EAA<>(inverse(actual) * desired);
    EXPECT_GT(std::hypot(spatialError[1], spatialError[2]), 0.2);
}

// 上游选择的反例：JacobianIKSolver 在半周目标处的公开收敛结果须用独立姿态误差复核。
TEST(ReuseAudit, JacobianSolverHalfTurnNeedsIndependentValidation) {
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;
    StateStructure structure;
    auto* base = new FixedFrame("base", Transform3D<>());
    auto* joint = new RevoluteJoint("joint", Transform3D<>());
    auto* tcp = new FixedFrame("tcp", Transform3D<>());
    structure.addFrame(base, structure.getRoot());
    structure.addFrame(joint, base);
    structure.addFrame(tcp, joint);
    joint->setActive(true);
    State state = structure.getDefaultState();
    const auto device = rw::core::ownedPtr(new SerialDevice(base, tcp, "view", state));
    rw::invkin::JacobianIKSolver solver(device, state);
    // 夹具只有绕 Z 的关节；绕 X 半周不可达，不能把 RPY 首参误当 roll。
    const Transform3D<> target(RPY<>(0, 0, rw::math::Pi).toRotation3D());
    const bool reportedConverged = solver.solveLocal(target, 1e-6, state, 2);
    EXPECT_TRUE(reportedConverged); // 实测上游差异，不允许生产沿用其成功判定。
    const EAA<> actualResidual(inverse(device->baseTend(state).R()) * target.R());
    EXPECT_NEAR(rw::math::Pi, actualResidual.angle(), 1e-12);
}
