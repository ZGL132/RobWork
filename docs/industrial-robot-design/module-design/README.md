# 模块详细方案层

本目录是需求、架构契约、工作包和智能体任务卡之间的实现解释层。它规定模块如何落地，但不得新增需求、修改架构契约或替代任务卡。

> 文档治理基线：`IRD-D0-20260829`  
> 目录状态：全部 23 篇方案已按 `IRD-D2-20260829` 冻结契约重写为 v0.3（平台 10 篇＋业务 12 篇＋testkit），内容完整、待各自所有者/消费者/独立测试评审签署后转为 `Proposed→Accepted`。文件存在不构成实施授权；引用契约仍为 `Proposed` 时按入口门禁保持 `Planned`。

## 推荐实施顺序

1. 基础契约实现：core-domain、persistence、snapshot-result、runtime-model。
2. 平台能力：policy-collision、execution-platform、diagnostics、secure-io。
3. 阶段 B：testkit、robot-modeling、requirements-definition、kinematics、optimization（仅 OPT-B 静态子集）。
4. 阶段 C：trajectory-planning、dynamics、drivetrain、device-selection。
5. 阶段 D/E：optimization 全量、workflow-integration、system-quality、installation-release、pilot-delivery。

每一阶段只能实施该阶段及其前置模块；阶段 B 不得以阶段 C/D 能力作为退出条件。

## 方案与任务关系

模块方案冻结目录、数据流、适配和证据形态。WP 文件冻结交付范围、依赖和退出条件。任务卡冻结单个智能体的文件边界、测试和提交。需求和架构契约与模块方案冲突时，智能体必须停止并报告。

公共类型、跨模块状态、接口和持久化 Schema 只能引用 `architecture/`；本目录只拥有模块私有类型、内部算法、适配次序、私有错误映射和实现级测试。若同一符号在多份方案中出现不同定义，应视为架构缺口而非允许的局部差异。

## 每份方案必须回答

模块目标/非目标、代码目录、CMake target 和依赖方向；输入输出字段、单位、可空性、状态和错误映射；调用顺序、线程/所有权、持久化或缓存边界；正常、失败、取消、恢复、数据不足场景；算法和坐标约定、序列化样例、测试矩阵、验证命令；旧代码迁移、删除策略、扩展点、任务卡和证据索引。

## 版本规则

模块方案版本跟随需求基线；引用的公共接口只能链接 architecture/ 中的权威定义。任何实现选择若无法由需求和契约唯一推导，必须新增 ADR。

## 进入实施的门禁

1. 所引用需求和架构契约均为当前 `Accepted` 版本；
2. 拥有目录、公共头、CMake target、允许依赖和禁止依赖明确；
3. 输入输出的字段、单位、可空性、状态、错误和所有权无歧义；
4. 正常、失败、取消、恢复、迟到结果和数据不足路径均有设计；
5. 算法公式、坐标系、容差、序列化样例和兼容策略完整；
6. 任务卡、验证命令、证据路径、迁移删除与回滚方案可执行；
7. 模块所有者、架构消费者和独立测试评审通过。
