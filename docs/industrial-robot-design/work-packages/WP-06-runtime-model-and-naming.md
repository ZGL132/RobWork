# WP-06 运行时模型与名称实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立唯一规范 SE(3) 计算模型和 `RuntimeNameMap`，从稳定 objectId 确定性编译 WorkCell/DynamicWorkCell，并消除分散的名称前缀处理。

**Architecture:** 领域模型不保存 RobWork 运行时全名；模型编译器一次生成不可变名称映射和两个运行时工件。所有适配器只通过 `IRuntimeNameResolver` 解析，禁止自行拼接、剥离或猜测前缀。

**Tech Stack:** C++、RobWork、RobWorkSim、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_runtime`、`sdurws_ird_runtime_test`。

**创建：**

- `industrialrobot/runtime/include/.../CanonicalKinematicModel.hpp`
- `industrialrobot/runtime/include/.../RuntimeNameMap.hpp`
- `industrialrobot/runtime/include/.../IRuntimeNameResolver.hpp`
- `industrialrobot/runtime/include/.../CanonicalModelCompiler.hpp`
- `industrialrobot/runtime/include/.../CompiledRobotArtifacts.hpp`
- `industrialrobot/runtime/include/.../RobWorkModelAdapter.hpp`
- `industrialrobot/runtime/src/`
- `industrialrobot/runtime/test/`

**覆盖需求：** ARC-03、04，CON-06，MDL-06、09、10、14，NFR-COR-05，NFR-MNT-07，AT-01、15、16、18。

## 冻结接口

```cpp
struct RuntimeNameBinding {
    ObjectId objectId;
    std::string runtimeDeviceName;
    std::string runtimeScopedName;
    ObjectKind objectKind;
};

class IRuntimeNameResolver {
public:
    virtual RuntimeNameBinding resolve(ObjectId) const = 0;
    virtual ObjectId reverse(std::string_view runtimeScopedName) const = 0;
};

struct CompiledRobotArtifacts {
    CanonicalKinematicModel canonical;
    RuntimeNameMap names;
    rw::models::WorkCell::Ptr workCell;
    rwsim::dynamics::DynamicWorkCell::Ptr dynamicWorkCell;
};
```

编译必须全成或全败；任一工件失败不得返回可提交产物。

## 任务

### Task 1：规范计算模型

- [ ] 先写 StandardDH 与 ExplicitJoint 编译后 FK/世界轴线等价测试。
- [ ] 定义父子关系、Joint Type、Origin、Axis、Zero/Home、限制和附属 Frame 的规范 SE(3) 链。
- [ ] 确认所有 FK/IK/轨迹/动力接口只接受规范模型，不接受 DH 表或 Widget 数据。

### Task 2：名称映射

- [ ] 使用 Arm、ArmA、RobotB 和重复 Joint1/TCP 黄金数据先写失败测试。
- [ ] 实现 objectId 与 `<runtimeDeviceName>.<localName>` 严格一一映射和反解。
- [ ] WORLD 与外部环境对象不添加机器人前缀；设备内 Joint/Frame/Object/Model 全部使用作用域名。
- [ ] 未知、歧义、双前缀、去前缀重名和旧前缀均返回稳定错误，不取首个匹配。

### Task 3：确定性双编译

- [ ] 从同一 RobotDesign 编译 WorkCell、DynamicWorkCell 和 RuntimeNameMap。
- [ ] 在 WorkCell、DWC、CollisionSetup、ProximitySetup、几何、限位、动力 Link 中逐项校验 objectId 绑定。
- [ ] 对 WorkCell 成功/DWC 失败和相反情况注入故障，确认没有部分产物。

### Task 4：任意轴运行时适配

- [ ] 对非 Z、非单位、continuous 和 prismatic 轴建立适配测试。
- [ ] 若 RobWork 关节要求局部 Z，补偿子连杆、视觉/碰撞几何、质心和惯量坐标。
- [ ] 在 Zero、Home、有限边界和固定 100 状态满足需求第 15.3 节容差，领域原始 Origin/Axis 不被改写。

### Task 5：重命名与静态扫描

- [ ] 重命名只改变 runtimeDeviceName/map；objectId 和物理输入切片不变。
- [ ] 历史快照保留旧名称，新执行使用新映射，不产生旧/双前缀。
- [ ] 边界扫描只允许 resolver 实现目录出现前缀拼接/剥离；迁移完成后删除旧逐字段 `stripDeviceScope` 链路。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime_test$'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 退出条件

- A-GATE-06 与 AT-18 全链路通过。
- RuntimeNameMap 双向一一对应，所有设备内引用无漏、旧、双前缀。
- 规范模型与运行时模型的 FK、轴、几何、质心和惯量满足冻结容差。
