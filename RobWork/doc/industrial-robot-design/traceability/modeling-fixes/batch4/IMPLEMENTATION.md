# MDL-CR-4 导入闭包、输入诊断与差异收口（实施待独立验收）

C:指定工作树，分支 codex/core-review-fixes，起点 b155d387fb310ed9d3abd41aac445b3228cccc41；启动时只有本轮第四批10个候选文件，未发现其它在途文件。前3批提交不改写；本批实现F07/F08/F12/F13及C02，F14已由第二批前置完成。

保留五对象schema、唯一RobotDesignCodec、pugixml映射、io ZipChannel/AtomicFile、core摘要、project持久身份/原子写路径与runtime唯一WorkCell/DWC owner。删除包UI只接收根对象的丢失路径、数值解析失败变Provided零、源角速率SI直入的错误假设与差异报告漏字段。适配层只承接完整对象闭包、资源原字节和单位/诊断，不新建解析/模型/数学平台。

## 修改与复用边界

- F07：SolidifiedResourceCopy移至已共享值头（原名称/字段不变）；工作集只暂存包字节。包确认后完整接收Tool/TCP、Scene、PoseSet、Drivetrain、固化资源。JSON草稿兼容旧帧、严格hex/身份/对象闭包和64MiB预算，超预算明确拒绝。再导出读取同一草稿资源闭包。
- 完整工作集v2对象槽接受既有project token ExternalResource；仅本处理器按摘要/cv/闭包核查，project分配新身份，与根和部件同一计划写入。资源校验拒绝发生在取号之前；任何失败不提交修订；既有固化引用必须绑定当前固定修订的精确oid/cv。宿主HostCompileResources已经从同次plannedWrites覆盖层沿io读字节，生产接线无需修改；真实项目双编译/磁盘/历史回归补证。成功回执重读正式闭包后清除已消费暂存副本。
- C02/F08：源Revolute VelLimit/AccLimit按上游XMLRWLoader::addLimits乘Deg2Rad，Prismatic保持m/s、m/s²。空/bad/负数保留Invalid，不静默零；新增正式MDL-IMPORT-VALUE-ILLEGAL描述符和来源诊断，补全登记。
- F12：URDF空axis、lower/upper/velocity及mass值用非空字段定位承载Invalid，正常非法原文仍保留；origin/inertia已带字段文本。非法输入回归覆盖空/缺失属性，禁止CoreError外抛。
- F13：速率/加速度四态和来源，稳定ObjectId部件内Tool显示/安装/几何/物性/负载/TCP、Scene、命名姿态、传动数组/耦合/目录逐字段比较；可选存在态和目录字符串不靠有歧义拼接。不变输入空报告，稳定排序，不写项目。
- 宿主组旧命名TCP测试的数值夹具缺少REUSE-T18后必需runtimeModelView锚，真实执行先得NoDevice。测试现按生产SnapshotViewAdapter转发已物化快照，保留原解析FK/IK断言与全部公差；不为测试补生产遗漏，也不创建替身FK。

上游/产品检索：RobWork/RobWork/src/rw/loaders/rwxml/XMLRWLoader.cpp::addLimits的Deg2Rad；现有core::ContentDigester；project/src/SolidifyServiceImpl.cpp的ExternalResource写入语义；ui/plugin/HostCompilePort.cpp::HostCompileResources同源覆盖层。与前三批相同，runtime持模型与编译生命周期，project持身份/正式修订，modeling只做工程输入与编辑/计划适配，依赖方向未扩。

## 实际验证

|验证|PASS|FAIL|SKIP|NOT RUN|
|---|---:|---:|---:|---:|
|modeling unit最终|493|0|0|0|
|modeling contract最终|17|0|0|0|
|Windows GUI最终（VS x64/windows/单exe）|124|0|0|0|
|正式宿主HostCompilePipelineTest最终|22|0|0|0|
|C:独立配置冒烟的本批4条具名用例|4|0|0|0|
|原始评审探针，最终重新编译执行|14|0|0|0|
|DH/Xacro公共头共存语法编译|1|0|0|0|

原始失败不覆盖：unit-first 490PASS/3FAIL（新增诊断码字段断言漏登记、质量原文被多余包装、包TCP夹具未按正式集合序构造）；修正后493PASS。宿主第一次进程退出0xC0000135，未执行，不能算PASS；按既有环境供给后21PASS/1FAIL（旧纯数值夹具无runtime锚），补正确前置后22PASS。GUI首次124PASS，质量Invalid原文调整后重建GUI并再次124PASS。所有日志、gtest XML及per-exe报告原样保存，未执行整库测试或完整RobWorkStudio人工巡游。

真实完整工作集用例包括：固化STL字节载入正式WorkCell/DWC；损坏摘要、硬错误、确认拒绝、真实资源编译失败均不推进head/不清草稿；成功新资源身份、精确cv/字节；后续编辑、undo/redo、旧五token、磁盘重开和只读拒绝。包用例另覆盖多TCP、场景、命名姿态、传动、完整草稿恢复及未应用再导出。

数值/性能：单位换算按解析π/180独立断言，没有新数值核心或公差改变。最终集成unit约0.7s，宿主组约2.3s（其中真实磁盘历史链约2.2s），独立冒烟约0.06s；仅记录本机实测，不套用其它能力包性能接受。固定有限输入，日志里非零用例数已核对。

机器门禁：本批静态R/T/SUB/GRAPH/LIB/SA02及引擎自测PASS（gates-static.log）。本批validate-docs实际退出1：REUSE-FRESHNESS : dynamics/src/InverseDynamics.cpp（docs-final.log），与前三批记录相同的历史REUSE-T52凭证过期阻断；未报告全量PASS。既有REUSE-T52/T53旧凭证不重写、不放宽。各批verify-task-scope显式Base/Head日志及commit-lint另存本目录。本批凭证与函数源区段只绑定实际变化，不把哈希映射当成独立语义验收。

公共影响：原SolidifiedResourceCopy同namespace同字段；工作集新增可选草稿暂存字节，旧JSON无该字段仍可读；v2命令对象槽新增既有资源token消费，旧五对象载荷不变，拒绝未授权资源用途；新增稳定输入诊断码。F14命名空间变化随第二批已送验。modeling/runtime原有内容编码版本不因本批再升版。回滚须整体撤销本批工作集、草稿、命令资源和UI路径及对应测试/凭证，不对已存在项目对象库做删除；保留不可变历史字节。

## 实际文件

- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\include\sdurws\ird\modeling\DiagCodes.hpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\include\sdurws\ird\modeling\Package.hpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\include\sdurws\ird\modeling\RobotDesign.hpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\include\sdurws\ird\modeling\Template.hpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\plugin\ModelingCommandFlows.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\plugin\ModelingDraftCodec.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\plugin\ModelingUiModule.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\CommandHandlers.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\DiagCodes.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\Import.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\ModelDiff.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\WorkCellReverseImport.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\src\WorkingSetCommand.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\test\DiagCodesTest.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\test\ImportTest.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\test\ModelDiffTest.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\modeling\test\ModelingCommandFlowsTest.cpp`
- `C:\Users\zgl18\.codex\worktrees\core-review-fixes\RobWork\RobWork\RobWorkStudio\src\rwslibs\industrialrobot\ui\test\HostCompilePipelineTest.cpp`
