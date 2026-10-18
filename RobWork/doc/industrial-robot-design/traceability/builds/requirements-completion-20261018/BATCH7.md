# WP-14-T12 第七批：保存项目运行时恢复与宿主生命周期

修复正式宿主的三个实际故障：重开保存机器人项目只有数据、没有runtime；事务持writer锁发布修订时需求订阅同步回查HEAD导致resource deadlock；正常关闭后需求校验仍查询已关闭存储。分别复用空写入十段编译链恢复瞬态运行时、在QObject关联队列中按适配器/项目/关闭状态守卫同步需求修订、关闭时返回空校验上下文。没有修改框架源码、公共接口或存储格式，也不增加保存项目的修订。

## 红绿证据

- batch7-reopen-ordered-red：20条通过、1条失败，复现重开缺runtime。
- batch7-lifecycle-red：26条通过、2条失败，复现需求基线未跟进与关闭误报。
- batch7-lifecycle-green：28条通过、0条失败。
- batch7-spatial-fixture：自动回归误选DeferredDelete排队旧表而失败，未计通过；改为换页后派发DeferredDelete再找当前参数表，生产建模逻辑不变。
- batch7-spatial-final：41条正式宿主断言通过、0条失败、退出0。真实参数表编辑六轴空间链、增加第七轴、应用、导出、关闭重开；设备/几何/场景一致、需求基线同步、末端FK独立对照(0.60,-0.15,-0.15)m误差小于1e-9、HEAD零增长。
- batch7-native-green：正常正式宿主通过原生目录框打开同一项目，轴测/正视/俯视截图检查空间链。碰撞检查继承开启，机器人呈红色；不将这些截图算作碰撞分组或覆盖状态颜色通过。

## 自动回归（构建前最后一轮）

| 目录 | 跑 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: | ---: |
| batch7-integrated-final | 307 | 306 | 1 | 0 |
| batch7-standalone-final | 307 | 306 | 1 | 0 |
| batch7-ui-model-final | 308 | 308 | 0 | 0 |
| batch7-standalone-ui-model-final | 266 | 266 | 0 | 0 |
| batch7-ui-gui-final | 84 | 84 | 0 | 0 |
| batch7-standalone-ui-gui-final | 74 | 74 | 0 | 0 |

两个模式UI目标注册集合不同，84和74分别记录。唯一跳过为旧占位GuiRegistration.V22RegisteredNotExecuted_ACC1，不计功能通过。XML、per-exe ird-test-report.json和console.log均保留。

## 完整构建与构建后复验

集成完整构建最终退出0，见batch7-build/integrated-final.log；ird_gates零命中、validate-task通过。首次裸完整构建在只读框架的Windows min/max宏处失败，使用构建环境CL=/DNOMINMAX解决，未修改框架。integrated.log和integrated-nominmax.log保留中间失败，后者还包含本批异常边界误写host->reportLine的编译错误，最终已改用匿名命名空间自由函数。

集成完整构建后batch7-integrated-postbuild重跑需求三目标及UI模型/GUI：699跑，698通过、1跳过、0失败。独立完整构建退出0（batch7-build/standalone.log）；构建后batch7-standalone-postbuild需求三目标及UI模型/GUI共647跑，646通过、1跳过、0失败。

## 未完成范围

原120项尚未最终全通过；不能用上述组件测试或运行时前置替代正式宿主各项验收。重开后建模编辑工作集仍未从保存设计恢复，面板仍显示输入不完整。本批恢复的是runtime/三维，未宣称恢复建模编辑工作集。后续仍需真实捕获/拾取/三维创建拖动/覆盖评估/区域采样显示/跨域消费者及布局等逐项修复验证。
