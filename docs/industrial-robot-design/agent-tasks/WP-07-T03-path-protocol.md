# WP-07-T03 路径验证协议

- **Task ID / 需求 ID / ADR / 阶段：**WP-07-T03；ARC-05、KIN-05、TRJ-04、NFR-COR-05；无新 ADR（参数权威 requirements §15.3）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线路径校验为 RobWork 默认采样，步长/深度不可配置、结论措辞不定）；契约 `architecture/evaluation-semantics.md` §1～2；方案 `module-design/policy-collision.md` v0.3 §5
- **前置任务及必需工件：**WP-07-T02（共享碰撞评估器）、WP-06-T04（任意轴规范模型保证 FK 对照有效）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/` 下 `include/sdurws/ird/policy/PathValidationProfile.hpp`、`src/PathValidator.cpp`、`test/PathProtocolTest.cpp`、`testdata/policy/profiles/`
- **禁止修改的文件和公共接口：**requirements §15.3 冻结参数值、策略安全距离语义、轨迹规划算法、连续安全声明措辞、UI、`CollisionEvaluator` 已冻结语义（T02）
- **修改前接口：**无独立路径验证协议；`kinematicanalysis` 等直接调用 RobWork 路径校验器，分辨率与结论由 RobWork 默认值决定
- **修改后接口：**有版本的 `pathValidationProfile` 携带 §15.3 冻结参数（初始 10 等分、转动步长上限 0.05 rad、移动步长上限 0.01 m、细分余量 0.005 m、最大深度 8）；`PathValidator` 输出采样包络＋`CollisionEvaluation`＋诊断；运动学/轨迹/优化不得本地覆盖启用状态、对象参与、配对规则或安全距离
- **实施步骤：**路径与关节类型 → 10 等分初始采样 → 端点/内部点评估 → 对碰撞或距离不确定段优先递归二分 → 强制步长/余量/深度 → 生成包络与固定措辞结论
- **RED 测试：**`test/PathProtocolTest.cpp`（注册于 `sdurws_ird_policy_test`）：深度 8 耗尽仍未达分辨率必须返回 `Completed + DataInsufficient + Complete` 且结论固定为"在本策略与分辨率下未发现碰撞"，不宣称连续安全证明——先确认测试在无实现时失败
- **最小实现：**初始采样＋二分递归＋深度/步长/余量判定＋结论文本常量；复用 T02 评估器做逐点检测
- **正常/边界/失败测试：**正常：分辨率内无碰路径返回 Completed＋Pass/Complete 并使用固定结论措辞；碰撞段优先二分并记录每个采样点、深度与 pair。边界：纯转动/纯移动/混合路径分别用对应步长、深度恰为 8、安全距离余量边界。失败：距离查询不可用遵循 fallback 保留不确定段证据、后端不可用报 `IRD-POLICY-BACKEND-UNAVAILABLE`
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`rg -n "0\.05|0\.01|0\.005" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src` 确认数值仅来自 profile 携带与 §15.3 引用常量；结论措辞无第二版本
- **证据工件：**`policy/out/test-evidence/wp-07/<run-id>/`：采样点/深度清单、分辨率参数来源记录、结论文本、逐段距离与诊断、命令日志与评审签名
- **提交格式：**`WP-07-T03: 新增路径验证协议`

  - 新增 pathValidationProfile 冻结参数与 PathValidator 递归二分实现
  - 新增 深度耗尽降级测试与目标登记
  - 新增 采样点/深度清单与结论文本证据记录
- **停止与升级条件：**需求 §15.3 未冻结步长、余量或深度语义，或某关节类型无法归属转动/移动步长时暂停并升级至产品负责人
