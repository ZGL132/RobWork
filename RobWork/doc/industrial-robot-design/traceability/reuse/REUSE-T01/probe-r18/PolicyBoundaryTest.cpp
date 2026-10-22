/**
 * @file PolicyBoundaryTest.cpp
 * @brief REUSE-T01：规则名称与上游pattern语义的边界审计。
 * 依据：ARC-04/05、SA-19；原会话夹具提供存在性设备，不冒充FK/几何。
 * 本文件只链接原policy目标，模型为用例私有；不修改产品或上游。
 */
#include IRD_POLICY_EXISTING_TEST

// ARC-04：runtime合法名不含通配字符，但允许REGEX前缀；直接上游探测
// 区分正常点号字面转义与保留前缀，不能将makeExclude的参数当纯字面API。
TEST(ReusePolicyBoundary, OrdinaryDottedNamesMatchLiterally) {
    const auto rule = rw::proximity::ProximitySetupRule::makeExclude("Robot.link_1", "Scene.table");
    EXPECT_TRUE(rule.match("Robot.link_1", "Scene.table"));
    EXPECT_FALSE(rule.match("RobotXlink_1", "Scene.table"));
}

// ARC-04/05：真实生产createSession应把已解析的完整名变成精确规则。
// 复用原测试的身份/发布/设备替身，仅改当前用例的名称事实与两对象场景；
// 不运行碰撞数值查询，失败只证明proximitySetup适配输出未精确覆盖原名。
TEST(ReusePolicyBoundary, ProductionSessionRuleMustMatchRegexPrefixedLiteralName) {
    const SceneIds ids;
    auto names = makeNames(ids, "REGEXarm");
    names.byId[ids.link1] = "REGEXarm.link_1";
    names.byId[ids.link2] = "REGEXarm.link_2";
    names.byName.clear();
    for (const auto& entry : names.byId) {
        names.byName.emplace(entry.second, entry.first);
    }
    auto wc = std::make_shared<rw::models::WorkCell>("BoundaryAuditWC");
    wc->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("REGEXarm")));
    for (const auto& name : {names.byId.at(ids.link1), names.byId.at(ids.link2)}) {
        wc->addFrame(rw::core::ownedPtr(new rw::kinematics::FixedFrame(
            name, rw::math::Transform3D<>::identity())));
    }
    auto scene = makeScene(ids);
    scene.workcell = wc;
    scene.objects.resize(2);
    const auto policy = publishPolicy(baseInput(ids), makeContext(ids));
    const auto session = buildSession(policy, scene, names);
    const auto& rules = session->proximitySetup().getProximitySetupRules();
    ASSERT_EQ(1u, rules.size());
    EXPECT_TRUE(rules.front().match(names.byId.at(ids.link1), names.byId.at(ids.link2)));
}
