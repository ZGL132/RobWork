# 源码区规则

根 AGENTS.md 是完整编码与治理约束；本区只补充定位信息，不重复加载设计区全部入口。

- 测试位于各单元 test/、contract_test/、gui_test/、perf_test/；目标通常为 sdurws_ird_<unit>_test / _contract_test / _gui_test / _perf_test。
- 按根 §4 选择受影响构建与测试。集成模式是交付口径；构建/依赖/接口变化另跑独立冒烟。GUI 交互要验证真实控件与接线。
- 计算逻辑、数学算法与数据变换用真实验证；替身只放六类端口边界，契约层覆盖真实接线。不引入 gmock。
- 公共接口、跨单元、核心算法、身份/持久化及复用迁移走独立验收；普通修正自查。本轮按根 §6.2 在主工作树主线串行提交；禁止自行合并其他分支，主线提交不等于独验通过。
- Windows GUI：VS x64 开发环境，QT_QPA_PLATFORM=windows，绝对路径，一次一个 exe；禁止 offscreen。故障时按需读 RobWork/.agents/skills/ird-qt-gui-windows/SKILL.md。
- 复用证据按当前文件和功能定向取用，禁止为了开工通读完整库存；机器全量检查仍执行 validate-reuse.ps1。
- 上游类型/操作权限见设计区 ARCHITECTURE §3.6 / SA-19，逐功能/生产正向证据见 reuse/audit-contract.md。直接 API 优先，薄适配仅连接真实兼容边界；不得自建模型链/通用 facade、扩大宿主例外或在 worker 传借持句柄。新增门禁独立验收前不进入适配实现或算法替换。
