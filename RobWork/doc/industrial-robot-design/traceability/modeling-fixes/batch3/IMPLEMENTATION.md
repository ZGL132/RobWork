# MDL-CR-3 几何与编辑事务（实施待独立验收）

C:指定工作树/codex/core-review-fixes，起点34712c7a543a7141381a0940d17492b8b8a93d2f。本批落实F05/F06/F10/F11及Owner的C01裁决，不替代独立验收。

保留工程对象/来源、runtime唯一模型owner及既有物性合成。删除COM编辑中错误的正平行轴膨胀、仅换链数组而丢失关联的重排、按当前链长复用身份的分配。适配每次几何引用的局部位姿至runtime Geometry::setTransform（上游RobWork/src/rw/geometry/Geometry.hpp）；工具网格从工具系转所挂TCP系，只做inverse(T_tool_tcp)一次，mount已在帧上。资源内容本身不夹带安装位置。

F06在副本中按稳定ObjectId作新序→旧序置换，Named Poses/Home/Zero、ratio/friction/torque来源一起随动。耦合只换关节行，电机列不动；部分窗口在新序不连续或数组维度失配则原子拒绝，未接管drivetrain算法。F10扫描当前闭包及草稿已占用历史，删/撤销/整链重建不释放身份；私有草稿JSON可选字段保存历史，旧稿缺省兼容，历史不是工程dirty。F11索引只读检查→数值验证→创建传动对象/引用，拒绝不留半对象。

C01关于实际COM的惯量六分量/来源在刚体安装平移时不变；UI中文交互及§5.3规则2同步修正，兼容既有MigrateInertia枚举/token。正常段合成的旋转/平行轴公式与通用COM→其他参考点函数保留，不再由COM编辑调用。质量覆盖与完整张量覆盖仍明确按用户物性输入处理。

公共影响：Description GeometryRef新增默认恒等localTransform；canonical每次引用新增局部变换字段，缓存协议1.5、实现1.3，旧缓存拒绝重编译；项目/modeling对象Codec不变。ModelingWorkingSet新增草稿身份历史，结构拒绝枚举追加AssociatedDataInvalid。无新依赖、无上游修改、无新模型owner、无新数值核心。

实际验证（原始日志/XML/per-exe报告同目录）：

|验证|PASS|FAIL|SKIP|
|---|---:|---:|---:|
|modeling unit最终|489|0|0|
|modeling contract|17|0|0|
|Windows GUI首次|123|1|0|
|GUI唯一失败用例修正定位后复跑|1|0|0|
|runtime受影响Codec/Compiler/Geometry|66|1|0|
|C:独立冒烟StructureEdit/物性|30|0|0|
|14项评审探针累计|10|4|0|

unit首次488/1，遗留面板数值断言仍是C01废止的膨胀值，按已裁决物理合同修正后最终489通过。GUI首次失败是新增用例硬编码joints[1]，真实默认树选择在尾部；改用身份差集识别新对象后复跑通过。其余GUI不重复计数，124项有效覆盖，未冒称单次全绿。VS x64/windows平台/绝对C:exe/每次一个GUI执行。runtime失败仍为先前已有LoaderRestoresThreadLocale_REUSE_T13中文路径转换异常，原日志保留、不计通过、不扩面修复；真实四类mesh局部位姿与1.5身份/旧版拒绝新增用例已通过。

RED证据0/3在修复前执行，覆盖COM不变、重复插入身份与关联置换。探针对应F05/F06/F10/F11全部翻PASS；剩余WorkCell单位、URDF空属性、差异和包UI导入归下一批。header共存语法编译通过。原probe源码/rsp及重放脚本封存，不以永远成功的编译替身作证明。

回滚：本批Description/canonical/codec/编译器/桥、草稿/历史、编辑/测试/文档必须整体回滚，先逆序撤销依赖批；不得只回滚字段使旧缓存或几何位姿错配。runtime模型生命周期、RNE物性及项目修订原子性没有新增owner。

机器门禁：ird_gates静态R/T/SUB/GRAPH/LIB/SA02均通过；首次附带reuse因新增PluginPanelTest凭证待刷新失败，已补本批绑定。复用矩阵仅刷新实际源区段，既有权限仅同步source/build摘要、不增操作和owner。历史REUSE-T52等完整迁移凭证不重写，最终docs-final.log的实际结果单列，不冒称全局PASS；旧全局阻断按batch1/batch2原始记录归责。
