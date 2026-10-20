# WP-14-T12 第十二批：真实捕获入口、确认守卫与局部历史

宿主旧已装配清单遗漏捕获/拾取，导致真实三维数据源在位仍永久禁用。本批补齐两条生产路由；无项目门控、现场无设备/无拾取反馈和领域确认门保留。捕获/拾取确认前后核对原编辑器、面板生命周期、会话代次与可写资格、完整工作集和闭包；捕获另核原设备清单/世界矩阵，拾取核原工位/拾取值。SceneObject按已有类型支持Frame及正反平面法向；不把joint/link伪装成robot-design。模块保留宿主lastPicked现取源；宿主真实pickFrame反解缓存与业务选中分离，消费时核项目/修订/WorkCell。成功恰一次通知面板记账和标脏；取消/过期零写入。

- batch12-red、batch12-pick-red：实际失败复现只读写入、确认期间状态改变、场景对象类型错误、宿主缝被覆盖及域写入后未通知面板；失败XML/JSON/console保留。
- batch12-flow-green：11用例中10通过、1失败，原因为测试直接构造面板漏工厂现取编辑器接线；补齐后最终全回归通过。
- batch12-host、batch12-host-route-red：正式宿主失败；补需求阶段工厂前置后仍失败，并新增明确的capture-command-executable失败断言，定位永久撤牌生产缺陷。未将“取消零写入”空转断言冒充入口通过。
- batch12-host-route-green：修路由后真实确认/位置/撤销重做通过；旋转参照失败。查明框架RPY构造按Z/Y/X传参，需求字段按roll/pitch/yaw对应X/Y/Z，测试须显式倒序适配，未改领域参数契约。
- batch12-gimbal-red：真实数值回归在±(π/2−1e−8)rad均失败；原asin因sin舍入到±1丢失余弦信息。改atan2(-R20,hypot(R00,R10))后最终GUI含普通/近奇异/正负奇异共五种角度通过，未用替身代替数学运算。
- batch12-integrated-final：711跑，710通过、1旧占位跳过、0失败。需求模型220（219通过+1跳过）、需求契约16、需求GUI81、UI模型309、UI GUI85；均XML＋per-exe JSON＋console。
- batch12-standalone-final：659跑，658通过、1旧占位跳过、0失败。需求同集成317跑；UI模型267、UI GUI75。双模式完整构建最终退出0；独立首轮新增测试调用外联identity符号而链接失败，改显式同值矩阵后通过，原日志保留。另有测试成员名拼写和初版漏include编译失败记录，未隐藏。
- batch12-host-verified：正式studio真实建模/提交/重开机器人、真实State与FK捕获、取消、确认期间关节改变拒绝、世界位置/旋转独立对照、一次按钮undo/redo及HEAD零增长，78条断言通过、0失败、退出0。已查看mtour-capture-confirm-2.png，摘要含真实设备/位置/RPY、确认和取消按钮；截图为真实对话框本身。
- batch12-host-requirements：正式需求tour回归96条断言通过、0失败、退出0，覆盖区域采样/预览、应用与项目历史等既有交互。

Windows GUI均VS2022 x64/windows平台、绝对路径、一次一个exe；双模式最终回归使用独立TEMP/TMP根。ird_gates零命中，validate-task通过；新增证据跟踪后重建索引并执行文档校验。公共头没有修改。本批仍不等于原120项全部通过：引用目录、三维创建/拖动、真实几何raycast/法向和覆盖评估/跨域消费者仍继续。新装配缝设计已写计划末尾，待所有者审阅；第七批已推送commit-lint格式偏差继续如实保留，禁止改写历史。
