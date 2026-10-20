# WP-14-T12 第二十四批：每点命名TCP与完整数值消费

整任务仍为WIP。A-15及四项精确补充已按所有者批准实施；原120项尚未全部终验，全程未使用子代理。

## 改动与真实失败能力

- 建模桥保留每工具全部原tcpKey及安装接口复合后的T_flange_tcp；runtime描述、S3/S5、不可变canonical模型均保留和校验目录。按UTF-8无符号字节序归一，完整目录进入模型相等与内容身份；坏键、重复键、非法变换拒绝。
- canonical编码由1.1升1.2，完整目录往返；旧缓存精确拒绝，不改project建模持久化格式。解码count先用包长必要上限阻止巨额分配。真实worker物化后目录、模型及身份保持，仍用物化后的快照验证两个独立手算FK/IK黄金。
- FK位姿/雅可比、IK入口、解析可达界限和既有五处Evaluator预检统一命名键规则：实际键精确命中优先，空键默认，未命中才允许旧工具名别名。真实长TCP界限不再沿用默认偏移。
- UI逐点验证并投影tcpOverride，悬空工具/缺键带工位名受理拒绝。真实两工具四键批量IK与默认TCP均成功，错用默认TCP的对照失败。面板默认引用改为空键，避免同名非默认键碰撞；新面板解析→真实FK先红得到(1.3,0,0.9)m，修复后恢复独立黄金(1.8,0,0)m。
- 保留全部先红：batch24-red-kin/modeling/runtime/ui及panel-red；中间编译、目录排序误断言、旧Evaluator预检、1次迭代预算导致的测试夹具失败均保留。新非零目标测试显式用200次迭代，不改产品预算或降低预期。
- 补充四的两处旧默认键断言先真实在模型/GUI分别失败，所有者批准后仅同步为空键及相邻中文注释，其他身份/配置/数值断言保留。

## 最终验证

- [PASS] 完整集成与完整独立构建实际退出0：batch24-build/integration-final.log、standalone-final.log。首轮独立链接失败来源为pair默认构造与测试identity调用引入框架导出符号，已改用runtime既有显式构造，不引入额外链接边。
- [PASS] 集成22个exe共2884跑：2879通过、5既有跳过、0失败。独立22个exe共2448跑：2444通过、4既有跳过、0失败。逐exe唯一路径和跳过用例见batch24-verification-summary.json；runtime不存在contract/gui目标，不计作通过。core两份JSON从原XML转录，其余由exe生成。
- 独立首次ui因PID复用读到旧临时崩溃日志，3用例失败（files.size为2）；使用全新临时根后同一exe267项通过，未删除历史目录或修改产品。证据batch24-final-standalone与clean并存。集成首次运行器请求未注册runtime_contract目标时未启动后续项，已按实际exe清单续跑，不计作成功目标。
- [PASS] 正式宿主modeling-tour 240条断言通过、0失败；requirements-tour 141条断言通过、0失败，两进程均实际退出0。VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、串行。重开共享树截图已目视核对同OID根、关节、连杆和夹具名称；标记盒截图不能证明真实mesh或任意三角法向。
- [PASS] ird_gates零命中（batch24-build/gates.log）；任务契约校验通过。最终verify-task、文档、scope和commit-lint在本批提交封存后按真实结果另行留痕。
- 首次validate-docs因新增证据尚未暂存失败（docs-pre-evidence.log），保持原记录；证据暂存并重建活动索引后重新检查，不把首次失败改成通过。

## 仍需继续

原120项当前版本逐行终验；六分量约束与五姿态规则的完整下游消费；区域独立TCP；真实mesh/任意三角法向；覆盖样本颜色/蓝圈/图例与真正轨迹、动力学、选型消费者仍需收口。本批组件数量和宿主既有遍历通过不能替代这些功能证据。不自行合入redesign-main。
