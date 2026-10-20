# WP-13-T24 / B3a：F15欧拉角基座输入实现验证

基线8fbc7ca8，分支wp13-t24。1335项通过、0失败、0跳过：集成modeling404/contract17/GUI56、ui326/contract57/GUI90；独立modeling329/contract8/GUI44；BuildRedLine4。正式宿主modeling-tour为既有综合回归，另计，不折算建模功能数。逐套XML及IRD报告已对账。

MDL-22/AT-37：默认ZYX欧拉角输入roll/pitch/yaw，单位rad、R_world_base=Rz(yaw)Ry(pitch)Rx(roll)，位置世界系m；保留高级EAA。仅Custom携带且两表示恰选一。域入口经半角四元数转主值EAA，再复用原校验和来源路径。存储仍为既有EAA，无canonical/项目格式或依赖边变化。

一般角、万向锁、pi、微小角的最终runtime安装矩阵均对照独立RzRyRx元素公式；Codec往返、有限性、歧义、非Custom、恒等拒绝和零修改已验证。真实窗口验证输入暂存、应用、还原、表示切换不写草稿、只读不复活；base-euler.png已查看，坐标/顺序/单位、输入值及按钮可读可达。窗口测试使用1280×900逻辑尺寸，不宣称正式dock全部布局已验齐。

保留真实红证：初始缺欧拉入口、域忽略RPY两项失败；新增显示fallback曾调用框架外联identity()导致独立链接失败，改为runtime权威地面预设占位后双模式重建通过。core首次误用BuildRedLineTest.*过滤器匹配0项，记录为NOT RUN并保留日志；更正BuildRedLine.*后真实执行4项通过，不将空运行计入总数。

Windows/VS2022 x64、QT_QPA_PLATFORM=windows，绝对exe路径、GUI串行；core伴随JSON由真实XML派生。构建中的既有前置声明/弃用返回值警告不等同零警告。冻结时由脚本强制核查verify-task与正式宿主回归成功。

边界：F15欧拉编辑实现验证完成，三预设/任意角的正式三维、环境碰撞和重力同源完整验证仍归B5。47项全功能终验未完成，不将B1/B2/F15的局部通过改写旧审计。独立新上下文验收未执行。仅本地提交，不推送、不合入、不使用子代理。
