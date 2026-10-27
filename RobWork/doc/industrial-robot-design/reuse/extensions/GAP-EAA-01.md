# GAP-EAA-01：保持原容差的旋转表示转换（待所有者确认）

状态：**未批准、未修改上游或产品**。不借自动继续指令批准核心、依赖、ABI或容差变化。
范围：modeling/RpyMath.hpp 的 rpyToEaa、runtime/BaseWorldTransform.cpp 的 EAA→R；RPY正向本身有直接适用API。
证据：[REUSE-T08](../../traceability/reuse/REUSE-T08/README.md)，上游版本仍为catalog锁定的b12e8503cc1c2a1acc040f506842a02e126b9515（本次只读核实源码）。

## 1. 需求与真实反例

既有 `BasePlacementEditTest.cpp::EulerCompilesAndCodecRoundTrips_MDL22_AT37` 已要求一般角、万向锁、pi和微小角的最终矩阵按1e-12验等；不是本包新增标准。
原例 `{1e-6,-2e-6,3e-6}` 经 `RPY(yaw,pitch,roll)→EAA(R)→toRotation3D` 最大元素误差约1.0035e-11，超过该容差。
EAA.cpp::angleAxis 在一般分支以acos(trace)取角，小角接近分支边界时损失精度。
更微小 `{1e-12,-2e-12,3e-12}` 的 `EAA::toRotation3D` 又经 `axis()` 的theta<1e-6归零，丢失有效姿态（误差约3e-12）。
前一项影响RPY→EAA，后一项单独影响未来runtime EAA→R迁移；不得混为一个原因。

`Quaternion::ln` 候选也已查实现（acos(qw)/sin(a)），在qw舍入为1的零/极小角返回非有限值，不能直接替代。
首次探测缺EIGEN_DONT_ALIGN_STATICALLY宏造成Quaternion返回对象偏移错误，已定位为夹具ABI不一致；纠正宏后非有限面从pi转为零/极小角。旧日志保留，**不以上述错位结果指称上游算法缺陷**。

## 2. 可复用能力与适配边界

- RPY.hpp/.cpp：直接复用RPY(yaw,pitch,roll).toRotation3D，产品roll/pitch/yaw换序即可；原RPYTest.cpp已原样编译运行通过。
- EAA.hpp/.cpp：已有矩阵/向量构造、toVector3D和toRotation3D；不因边界不足宣称无旋转能力。
- Quaternion.hpp/.cpp：已有RPY/Rotation构造和标准化；没有满足本契约的完整稳定主值旋转矢量转换入口。ln的零角问题须单独处理，不能新增本地转换核遮盖。
- RobWorkSetup.cmake:830 对本机x64定义EIGEN_DONT_ALIGN_STATICALLY=1；涉及含Eigen四元数成员的导出对象必须与上游制品匹配。当前产品不在本包增加Quaternion或改全局编译宏。

## 3. 最小候选与被否备选

推荐A：向既有EAA.cpp::angleAxis提交最小精度扩展提案，保留原构造/类型/虚接口，以稳定取角消除小角损失；同步评估EAA.hpp::axis与EAA.cpp::toRotation3D对有效小角的行为。**本提案不提供或实施算法补丁**；批准后先依SA-02登记PATCHES与patch，跑上游EAA/RPY/Quaternion及产品独立oracle、微小角/pi、ABI与性能，再决定采用。

B：给上游Quaternion增加稳定主值EAA转换方法，复用既有RPY构造；变更面与API比A大，优先级较低，且必须修清同构建ABI。

C：经明确批准使用上游 `.e()` 暴露的原生Eigen表示作薄转换，验证原有依赖内AngleAxis的适用性及小角/pi规范；没有新依赖，但会绕开当前EAA入口，需所有者裁决其边界，**未获批准不得实施**。

D：在产品保留/新写半角组合、对数映射、小角补丁或双算法fallback；不作为迁移方案。旧实现只维持未迁移事实，不关闭债务。不得放宽1e-12、删除微小角测试或重产golden洗绿。

## 4. 影响与回滚

A触及上游数学模块，所有EAA消费者、axis小角语义和构建/导出制品均受影响，不能只验modeling。需分别检查API/ABI、符号、坐标与主值符号、确定性、异常、耗时及测试资源；无新依赖/许可证变化。
RPY迁移与EAA处在同一个RpyMath文件；当前门禁要求变动文件全部功能有已解析迁移证据，不能将EAA pending伪装成migrated，也不为抢跑缩小门禁。若要解耦文件，必须另行审查原有重复代码的迁移范围，不能以搬家绕过。
回滚为完整能力/patch提交与匹配制品，不留运行期旧核开关。未经批准，本能力保持planned/proposal；不阻断与其无依赖的能力审查和实施。

## 5. 所有者批准方案A（2026-10-23）

本会话所有者明确回复“批准方案 A（上游 EAA 最小修正）”。以上章节保留提案形成时的状态和候选比较；当前方案A已授权实施，B/C/D未批准。批准原文及范围见 [REUSE-T10 approval](../../traceability/reuse/REUSE-T10/approval.json)。先登记0004补丁，再在原EAA.cpp修改精度并补上游EAATest；保持axis()旧行为及API/ABI，近零双向量取角同为该EAA扩展点，支撑runtime轴对齐。独验/性能接受/生产迁移及债务关闭仍未完成。

## 6. 所有者撤销方案A（2026-10-23，最新有效决议）

所有者明确指令“撤销EAA的修改补丁，保持原状，改为适当放大误差阈值”。方案A及其性能接受不再作为实施依据，EAA.cpp/EAATest.cpp恢复上游基线，0004取消。原独验FAIL及实验原件保留。后续只复用原API，测量并局部登记旋转表示转换误差，不能整体放宽FK/编译链1e-9或物理语义要求；不保留修补核心/fallback。
