runtime偏置反例独立验收：PASS（缺陷复现有效）

冻结 SHA：`dd27799a1afd6ba080c7ad7b4ec9d6db9da86e38`。本结论不表示产品行为通过：产品规范测试仍为 **2 total、1 passed、1 failed、skipped/notRun=0，进程exit1**；产品缺陷保持 **OPEN**。

在独立冻结checkout、新建Temp构建目录和VS2022 x64开发环境中，按原runtime-probe重配重建：configure/build均exit0。探测直接使用产品原runtime/core/testkit CMake、原CanonicalModelFixture与真实compileWorkCell、SerialDevice::baseTend；生成项目确认编译输入是冻结树的WorkCellCompiler.cpp，没有复制产品源清单或伪造FK。

旋转控制组通过。移动规范反例在X/Y/Z三轴均失败：zeroOffset=.25m、q_rw=.125m，规范要求沿轴位移.375m且无旋转；实际位移.125m并残留.25rad旋转。XML每轴记录1个平移、4个旋转断言失败，共15个failure；矩阵实测cos=.96891242171064473、sin=±.24740395925452294，与.25rad吻合。日志、XML及原生JSON逐项状态/汇总一致，与原red探测结果一致。未改规范、黄金或1e-12容差。

根因归runtime owner：compileWorkCell在Prismatic分支前共用jointStaticTransform，把米制zeroOffset送入rotationFromCustomEaa(axis*offset)，静态平移缺失；子连杆只补偿轴对齐旋转。上游PrismaticJoint实现只是沿静态R的Z列平移q并保持传入旋转；其已有JointTest也明确该契约。故此反例不指向上游PrismaticJoint缺陷，不需要新运动学核。后续只可在runtime owner修正既有类型映射，并补相应契约回归；不能在业务层加补偿或另建WorkCell。

6份冻结manifest源码摘要全部匹配；额外绑定core/testkit CMake、报告入口、上游Prismatic源码和权威文档，记录独立二进制/缓存与既有DLL摘要。使用既有D盘导出框架库，不声称完整上游源码可复现构建。原生报告gitCommit为空，冻结身份由独立干净检出、项目输入及外部摘要绑定。

未实施修复、未跑全量runtime/上游测试/GUI或完整边界回归，不给T01/T02整体PASS或迁移批准。父树、产品/上游、规范/黄金与历史报告未修改；未提交、推送、合入或派子代理。

结构化结果见 `acceptance.json`；原始失败保留在 `tests.log`、`gtest.xml`、`ird-test-report.json`。
