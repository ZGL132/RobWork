# 覆盖对照与不遗漏检查

本表只保留历史检查身份和功能名，不复制历史结果，不合并或改写旧证据。

## 旧审核矩阵逐项对应

|旧矩阵|旧编号|功能|本轮操作卡|
|---|---|---|---|
|需求|R01-01|新建关键任务点|[REQ-R01-01](01-REQUIREMENTS.md#req-r01-01)|
|需求|R01-02|复制工位|[REQ-R01-02](01-REQUIREMENTS.md#req-r01-02)|
|需求|R01-03|删除工位及选中回退|[REQ-R01-03](01-REQUIREMENTS.md#req-r01-03)|
|需求|R01-04|被引用工位删除保护|[REQ-R01-04](01-REQUIREMENTS.md#req-r01-04)|
|需求|R01-05|名称和工艺标签编辑|[REQ-R01-05](01-REQUIREMENTS.md#req-r01-05)|
|需求|R01-06|参考坐标系选择|[REQ-R01-06](01-REQUIREMENTS.md#req-r01-06)|
|需求|R01-07|TCP 引用选择|[REQ-R01-07](01-REQUIREMENTS.md#req-r01-07)|
|需求|R01-08|任务点 XYZ 位置|[REQ-R01-08](01-REQUIREMENTS.md#req-r01-08)|
|需求|R01-09|六个受约束/自由分量|[REQ-R01-09](01-REQUIREMENTS.md#req-r01-09)|
|需求|R01-10|位置与姿态容差|[REQ-R01-10](01-REQUIREMENTS.md#req-r01-10)|
|需求|R01-11|Must/Should 等级与启用|[REQ-R01-11](01-REQUIREMENTS.md#req-r01-11)|
|需求|R01-12|唯一标识、重名和字段状态|[REQ-R01-12](01-REQUIREMENTS.md#req-r01-12)|
|需求|R02-01|接近段启用、方向、距离|[REQ-R02-01](01-REQUIREMENTS.md#req-r02-01)|
|需求|R02-02|作业段定义|[REQ-R02-02](01-REQUIREMENTS.md#req-r02-02)|
|需求|R02-03|撤离段启用、方向、距离|[REQ-R02-03](01-REQUIREMENTS.md#req-r02-03)|
|需求|R02-04|顺序键、重复与环路|[REQ-R02-04](01-REQUIREMENTS.md#req-r02-04)|
|需求|R03-01|Box 区域新增与中心尺寸|[REQ-R03-01](01-REQUIREMENTS.md#req-r03-01)|
|需求|R03-02|按数量网格采样|[REQ-R03-02](01-REQUIREMENTS.md#req-r03-02)|
|需求|R03-03|按间距网格采样|[REQ-R03-03](01-REQUIREMENTS.md#req-r03-03)|
|需求|R03-04|随机位置采样与种子|[REQ-R03-04](01-REQUIREMENTS.md#req-r03-04)|
|需求|R03-05|方向姿态采样|[REQ-R03-05](01-REQUIREMENTS.md#req-r03-05)|
|需求|R03-06|工具滚转姿态采样|[REQ-R03-06](01-REQUIREMENTS.md#req-r03-06)|
|需求|R03-07|最低覆盖率|[REQ-R03-07](01-REQUIREMENTS.md#req-r03-07)|
|需求|R03-08|退化区域、零计划与计数溢出|[REQ-R03-08](01-REQUIREMENTS.md#req-r03-08)|
|需求|R03-09|中性采样网格预览|[REQ-R03-09](01-REQUIREMENTS.md#req-r03-09)|
|需求|R03-10|Good/Weak/Failed 样本着色与图例|[REQ-R03-10](01-REQUIREMENTS.md#req-r03-10)|
|需求|R03-11|选中样本蓝圈/三维定位|[REQ-R03-11](01-REQUIREMENTS.md#req-r03-11)|
|需求|R04-01|工况新增、复制与删除|[REQ-R04-01](01-REQUIREMENTS.md#req-r04-01)|
|需求|R04-02|工况改名|[REQ-R04-02](01-REQUIREMENTS.md#req-r04-02)|
|需求|R04-03|引用环境障碍/场景对象|[REQ-R04-03](01-REQUIREMENTS.md#req-r04-03)|
|需求|R04-04|负载质量 kg|[REQ-R04-04](01-REQUIREMENTS.md#req-r04-04)|
|需求|R04-05|负载质心 m 与惯量 kg·m²|[REQ-R04-05](01-REQUIREMENTS.md#req-r04-05)|
|需求|R04-06|Grasp/Release/Dwell 事件|[REQ-R04-06](01-REQUIREMENTS.md#req-r04-06)|
|需求|R04-07|最小关节裕量与无碰撞要求|[REQ-R04-07](01-REQUIREMENTS.md#req-r04-07)|
|需求|R04-08|工况适用范围和工位清单|[REQ-R04-08](01-REQUIREMENTS.md#req-r04-08)|
|需求|R04-09|目标节拍 s|[REQ-R04-09](01-REQUIREMENTS.md#req-r04-09)|
|需求|R04-10|必验集合唯一派生规则|[REQ-R04-10](01-REQUIREMENTS.md#req-r04-10)|
|需求|R05-01|CSV 真实文件读取|[REQ-R05-01](01-REQUIREMENTS.md#req-r05-01)|
|需求|R05-02|字段字典、别名自动识别|[REQ-R05-02](01-REQUIREMENTS.md#req-r05-02)|
|需求|R05-03|手动字段映射|[REQ-R05-03](01-REQUIREMENTS.md#req-r05-03)|
|需求|R05-04|单位声明与 mm/deg→SI 预览|[REQ-R05-04](01-REQUIREMENTS.md#req-r05-04)|
|需求|R05-05|错误行定位到行/列/原文|[REQ-R05-05](01-REQUIREMENTS.md#req-r05-05)|
|需求|R05-06|正确行保留并跳过坏行|[REQ-R05-06](01-REQUIREMENTS.md#req-r05-06)|
|需求|R05-07|缺列、重复标识/名称、非法单位|[REQ-R05-07](01-REQUIREMENTS.md#req-r05-07)|
|需求|R05-08|公式只按文本数据解析|[REQ-R05-08](01-REQUIREMENTS.md#req-r05-08)|
|需求|R05-09|CSV 可逆转义与方言标识|[REQ-R05-09](01-REQUIREMENTS.md#req-r05-09)|
|需求|R05-10|取消或失败不写草稿|[REQ-R05-10](01-REQUIREMENTS.md#req-r05-10)|
|需求|R05-11|导入溯源摘要与原始行号|[REQ-R05-11](01-REQUIREMENTS.md#req-r05-11)|
|需求|R06-01|任意草稿保存与恢复|[REQ-R06-01](01-REQUIREMENTS.md#req-r06-01)|
|需求|R06-02|应用生成新修订|[REQ-R06-02](01-REQUIREMENTS.md#req-r06-02)|
|需求|R06-03|启用非法 Must 阻断正式运行|[REQ-R06-03](01-REQUIREMENTS.md#req-r06-03)|
|需求|R06-04|有效 Must 不满足与 Should 警告|[REQ-R06-04](01-REQUIREMENTS.md#req-r06-04)|
|需求|R06-05|部分预览只检查有效条目|[REQ-R06-05](01-REQUIREMENTS.md#req-r06-05)|
|需求|R06-06|过期修订冲突及草稿保留|[REQ-R06-06](01-REQUIREMENTS.md#req-r06-06)|
|需求|R06-07|未应用编辑不使正式结果失效|[REQ-R06-07](01-REQUIREMENTS.md#req-r06-07)|
|需求|R06-08|R0~R9 分层就绪校验|[REQ-R06-08](01-REQUIREMENTS.md#req-r06-08)|
|需求|R06-09|校验页筛选、对象跳转和三要素|[REQ-R06-09](01-REQUIREMENTS.md#req-r06-09)|
|需求|R07-01|拆垛/拣选模板|[REQ-R07-01](01-REQUIREMENTS.md#req-r07-01)|
|需求|R07-02|机床上下料模板|[REQ-R07-02](01-REQUIREMENTS.md#req-r07-02)|
|需求|R07-03|码垛模板|[REQ-R07-03](01-REQUIREMENTS.md#req-r07-03)|
|需求|R07-04|检测模板|[REQ-R07-04](01-REQUIREMENTS.md#req-r07-04)|
|需求|R07-05|换工具模板|[REQ-R07-05](01-REQUIREMENTS.md#req-r07-05)|
|需求|R07-06|交接模板|[REQ-R07-06](01-REQUIREMENTS.md#req-r07-06)|
|需求|R07-07|模板参数、数量实时预览|[REQ-R07-07](01-REQUIREMENTS.md#req-r07-07)|
|需求|R07-08|来源徽标和批次溯源|[REQ-R07-08](01-REQUIREMENTS.md#req-r07-08)|
|需求|R07-09|按参数重生成及手改冲突|[REQ-R07-09](01-REQUIREMENTS.md#req-r07-09)|
|需求|R07-10|解除关联保留条目|[REQ-R07-10](01-REQUIREMENTS.md#req-r07-10)|
|需求|R08-01|三维场景创建目标|[REQ-R08-01](01-REQUIREMENTS.md#req-r08-01)|
|需求|R08-02|三维拖动与检查目标|[REQ-R08-02](01-REQUIREMENTS.md#req-r08-02)|
|需求|R08-03|拾取/捕获写回前确认|[REQ-R08-03](01-REQUIREMENTS.md#req-r08-03)|
|需求|R09-01|固定姿态 Fixed|[REQ-R09-01](01-REQUIREMENTS.md#req-r09-01)|
|需求|R09-02|对齐参考系 AlignFrame|[REQ-R09-02](01-REQUIREMENTS.md#req-r09-02)|
|需求|R09-03|对齐几何法向 AlignGeometryNormal|[REQ-R09-03](01-REQUIREMENTS.md#req-r09-03)|
|需求|R09-04|指向目标 PointAtTarget|[REQ-R09-04](01-REQUIREMENTS.md#req-r09-04)|
|需求|R09-05|工具滚转自由 ToolRollFree|[REQ-R09-05](01-REQUIREMENTS.md#req-r09-05)|
|需求|R09-06|规则解析来源、确定性和失败定位|[REQ-R09-06](01-REQUIREMENTS.md#req-r09-06)|
|需求|R10-01|从Frame拾取参考对象|[REQ-R10-01](01-REQUIREMENTS.md#req-r10-01)|
|需求|R10-02|从几何特征拾取法向|[REQ-R10-02](01-REQUIREMENTS.md#req-r10-02)|
|需求|R10-03|从当前场景捕获TCP|[REQ-R10-03](01-REQUIREMENTS.md#req-r10-03)|
|需求|R10-04|过期会话诊断与来源标签|[REQ-R10-04](01-REQUIREMENTS.md#req-r10-04)|
|需求|R11-01|过参考系镜像面镜像|[REQ-R11-01](01-REQUIREMENTS.md#req-r11-01)|
|需求|R11-02|不可镜像规则与派生无环|[REQ-R11-02](01-REQUIREMENTS.md#req-r11-02)|
|需求|R11-03|线性阵列完整参数|[REQ-R11-03](01-REQUIREMENTS.md#req-r11-03)|
|需求|R11-04|矩形阵列行列层、各轴间距|[REQ-R11-04](01-REQUIREMENTS.md#req-r11-04)|
|需求|R11-05|圆形阵列半径、角度和轴|[REQ-R11-05](01-REQUIREMENTS.md#req-r11-05)|
|需求|R11-06|三维折线阵列控制点|[REQ-R11-06](01-REQUIREMENTS.md#req-r11-06)|
|需求|R11-07|局部撤销/重做与批次整体回滚|[REQ-R11-07](01-REQUIREMENTS.md#req-r11-07)|
|需求|R11-08|项目撤销与需求草稿撤销分离|[REQ-R11-08](01-REQUIREMENTS.md#req-r11-08)|
|需求|R12-01|需求JSON导入|[REQ-R12-01](01-REQUIREMENTS.md#req-r12-01)|
|需求|R12-02|JSON同字段字典、安全与版本错误|[REQ-R12-02](01-REQUIREMENTS.md#req-r12-02)|
|需求|R12-03|导出JSON草稿副本|[REQ-R12-03](01-REQUIREMENTS.md#req-r12-03)|
|需求|R12-04|副本不修改项目、原子失败保旧文件|[REQ-R12-04](01-REQUIREMENTS.md#req-r12-04)|
|需求|U01|空态引导与缺输入呈现|[REQ-U01](01-REQUIREMENTS.md#req-u01)|
|需求|U02|工程用语与隐藏内部实现词|[REQ-U02](01-REQUIREMENTS.md#req-u02)|
|需求|U03|失败提示对象、原因、建议及比较值单位|[REQ-U03](01-REQUIREMENTS.md#req-u03)|
|需求|U04|高级采样/种子与开发诊断折叠|[REQ-U04](01-REQUIREMENTS.md#req-u04)|
|需求|U05|批量粘贴及筛选需求参数|[REQ-U05](01-REQUIREMENTS.md#req-u05)|
|需求|U06|单位显示与显示单位切换|[REQ-U06](01-REQUIREMENTS.md#req-u06)|
|需求|U07|四页树表联动及面包屑|[REQ-U07](01-REQUIREMENTS.md#req-u07)|
|需求|U08|列表状态词、三维颜色与图例一致|[REQ-U08](01-REQUIREMENTS.md#req-u08)|
|需求|U09|表单应用确认、取消及会话显示不弹保存|[REQ-U09](01-REQUIREMENTS.md#req-u09)|
|需求|U10|工程策略摘要、入口与显示开关分离|[REQ-U10](01-REQUIREMENTS.md#req-u10)|
|需求|U11|停靠、隐藏、恢复和跨会话布局|[REQ-U11](01-REQUIREMENTS.md#req-u11)|
|需求|U12|窄Dock与常用分辨率可读可操作|[REQ-U12](01-REQUIREMENTS.md#req-u12)|
|需求|U13|七种公共状态及取消/过期原因|[REQ-U13](01-REQUIREMENTS.md#req-u13)|
|需求|U14|顶/前/右及轴测标准视图|[REQ-U14](01-REQUIREMENTS.md#req-u14)|
|需求|U15|缩放、自动缩放和Home|[REQ-U15](01-REQUIREMENTS.md#req-u15)|
|需求|U16|相机旋转与透视/正交|[REQ-U16](01-REQUIREMENTS.md#req-u16)|
|需求|U17|线框/透明及渲染分组显隐|[REQ-U17](01-REQUIREMENTS.md#req-u17)|
|需求|U18|碰撞高亮与组掩码|[REQ-U18](01-REQUIREMENTS.md#req-u18)|
|需求|U19|PNG视图截图与真实ray-cast|[REQ-U19](01-REQUIREMENTS.md#req-u19)|
|需求|U20|需求阶段门控与下游失效提示|[REQ-U20](01-REQUIREMENTS.md#req-u20)|
|需求|U21|需求命令搜索、快捷键与键盘导航|[REQ-U21](01-REQUIREMENTS.md#req-u21)|
|需求|U22|帮助、版本和插件清单|[REQ-U22](01-REQUIREMENTS.md#req-u22)|
|需求|U23|只读项目禁编辑但可浏览/导出|[REQ-U23](01-REQUIREMENTS.md#req-u23)|
|需求|U24|项目关闭/切换清理三维预览|[REQ-U24](01-REQUIREMENTS.md#req-u24)|
|需求|J-01|对象身份、切片与修订隔离|[REQ-J-01](01-REQUIREMENTS.md#req-j-01)|
|需求|J-02|下游覆盖分母与必验工况全覆盖|[REQ-J-02](01-REQUIREMENTS.md#req-j-02)|
|需求|J-03|段/负载/事件进入轨迹动力学|[REQ-J-03](01-REQUIREMENTS.md#req-j-03)|
|需求|J-04|工具回填后需求引用不改、下游重算|[REQ-J-04](01-REQUIREMENTS.md#req-j-04)|
|建模|M01|六轴模板及安装预设|[MDL-M01](02-MODELING.md#mdl-m01)|
|建模|M02|七轴模板与显式权威|[MDL-M02](02-MODELING.md#mdl-m02)|
|建模|M03|结构增删/插入/重排/六轴重置|[MDL-M03](02-MODELING.md#mdl-m03)|
|建模|M04|关节类型/轴/原点/零位|[MDL-M04](02-MODELING.md#mdl-m04)|
|建模|M05|限位/速度/加速度|[MDL-M05](02-MODELING.md#mdl-m05)|
|建模|M06|双权威与只读派生|[MDL-M06](02-MODELING.md#mdl-m06)|
|建模|M07|显式转DH五状态|[MDL-M07](02-MODELING.md#mdl-m07)|
|建模|M08|URDF字段/默认/忽略/不支持报告|[MDL-M08](02-MODELING.md#mdl-m08)|
|建模|M09|多分支显式选链|[MDL-M09](02-MODELING.md#mdl-m09)|
|建模|M10|continuous工程范围|[MDL-M10](02-MODELING.md#mdl-m10)|
|建模|M11|混合链与4/5轴阻止|[MDL-M11](02-MODELING.md#mdl-m11)|
|建模|M12|mimic/planar/floating/闭环|[MDL-M12](02-MODELING.md#mdl-m12)|
|建模|M13|Xacro参数与受控展开|[MDL-M13](02-MODELING.md#mdl-m13)|
|建模|M14|外部WorkCell反向导入|[MDL-M14](02-MODELING.md#mdl-m14)|
|建模|M15|视觉/碰撞几何加载与解绑|[MDL-M15](02-MODELING.md#mdl-m15)|
|建模|M16|占位连杆几何生成|[MDL-M16](02-MODELING.md#mdl-m16)|
|建模|M17|自碰撞/排除对/安全距离|[MDL-M17](02-MODELING.md#mdl-m17)|
|建模|M18|法兰与默认TCP|[MDL-M18](02-MODELING.md#mdl-m18)|
|建模|M19|工具创建/删除/几何与物性|[MDL-M19](02-MODELING.md#mdl-m19)|
|建模|M20|多个命名TCP|[MDL-M20](02-MODELING.md#mdl-m20)|
|建模|M21|实心/空心圆柱/矩形物性估算|[MDL-M21](02-MODELING.md#mdl-m21)|
|建模|M22|物性手工覆盖及质心确认|[MDL-M22](02-MODELING.md#mdl-m22)|
|建模|M23|质量/SPD/三角不等式断言|[MDL-M23](02-MODELING.md#mdl-m23)|
|建模|M24|行程策略与显式确认|[MDL-M24](02-MODELING.md#mdl-m24)|
|建模|M25|WC/DWC双编译原子应用|[MDL-M25](02-MODELING.md#mdl-m25)|
|建模|M26|名称映射与改名|[MDL-M26](02-MODELING.md#mdl-m26)|
|建模|M27|场景固定帧/障碍物管理|[MDL-M27](02-MODELING.md#mdl-m27)|
|建模|M28|额定/峰值力矩及摩擦|[MDL-M28](02-MODELING.md#mdl-m28)|
|建模|M29|命名位姿/Home/Zero|[MDL-M29](02-MODELING.md#mdl-m29)|
|建模|M30|规范包与XML导出/导入|[MDL-M30](02-MODELING.md#mdl-m30)|
|建模|M31|耦合矩阵位置/力矩/限位映射|[MDL-M31](02-MODELING.md#mdl-m31)|
|建模|M32|基座三预设与任意角|[MDL-M32](02-MODELING.md#mdl-m32)|
|建模|M33|结构/参数/物性差异|[MDL-M33](02-MODELING.md#mdl-m33)|
|建模|H01|进入建模/目标和就绪状态|[MDL-H01](02-MODELING.md#mdl-h01)|
|建模|H02|树↔属性↔三维选择|[MDL-H02](02-MODELING.md#mdl-h02)|
|建模|H03|批量粘贴/筛选/单位|[MDL-H03](02-MODELING.md#mdl-h03)|
|建模|H04|错误定位与中文工程文案|[MDL-H04](02-MODELING.md#mdl-h04)|
|建模|H05|暂存/确认/取消/脏标记|[MDL-H05](02-MODELING.md#mdl-h05)|
|建模|H06|本地撤销/重做|[MDL-H06](02-MODELING.md#mdl-h06)|
|建模|H07|项目撤销/重做|[MDL-H07](02-MODELING.md#mdl-h07)|
|建模|H08|保存草稿/关闭重开|[MDL-H08](02-MODELING.md#mdl-h08)|
|建模|H09|基线冲突/编辑重演|[MDL-H09](02-MODELING.md#mdl-h09)|
|建模|H10|只读会话|[MDL-H10](02-MODELING.md#mdl-h10)|
|建模|H11|命令可达/搜索/快捷键|[MDL-H11](02-MODELING.md#mdl-h11)|
|建模|V01|真实三维显示正确性|[MDL-V01](02-MODELING.md#mdl-v01)|
|建模|V02|三维显示模式与导航|[MDL-V02](02-MODELING.md#mdl-v02)|
|建模|V03|三维覆盖层与拾取|[MDL-V03](02-MODELING.md#mdl-v03)|
|运动学|K01|当前 FK 与双参考系 TCP|[KIN-K01](03-KINEMATICS.md#kin-k01)|
|运动学|K02|Jacobian 完整显示|[KIN-K02](03-KINEMATICS.md#kin-k02)|
|运动学|K03|奇异值、条件数、可操作度|[KIN-K03](03-KINEMATICS.md#kin-k03)|
|运动学|K04|逐关节裕量与最差关节|[KIN-K04](03-KINEMATICS.md#kin-k04)|
|运动学|K05|设备和 TCP 选择|[KIN-K05](03-KINEMATICS.md#kin-k05)|
|运动学|K06|XYZ＋RPY 单目标输入|[KIN-K06](03-KINEMATICS.md#kin-k06)|
|运动学|K07|目标单位切换|[KIN-K07](03-KINEMATICS.md#kin-k07)|
|运动学|K08|非法/空/非有限输入|[KIN-K08](03-KINEMATICS.md#kin-k08)|
|运动学|K09|三类多初值策略|[KIN-K09](03-KINEMATICS.md#kin-k09)|
|运动学|K10|残差与限位硬过滤|[KIN-K10](03-KINEMATICS.md#kin-k10)|
|运动学|K11|碰撞硬过滤|[KIN-K11](03-KINEMATICS.md#kin-k11)|
|运动学|K12|逐轴去重|[KIN-K12](03-KINEMATICS.md#kin-k12)|
|运动学|K13|四键稳定排序|[KIN-K13](03-KINEMATICS.md#kin-k13)|
|运动学|K14|搜索未果与不可达分离|[KIN-K14](03-KINEMATICS.md#kin-k14)|
|运动学|K15|候选检查器与双击|[KIN-K15](03-KINEMATICS.md#kin-k15)|
|运动学|K16|需求点只读消费|[KIN-K16](03-KINEMATICS.md#kin-k16)|
|运动学|K17|批量三态报告|[KIN-K17](03-KINEMATICS.md#kin-k17)|
|运动学|K18|多必验工况覆盖|[KIN-K18](03-KINEMATICS.md#kin-k18)|
|运动学|K19|后台进度/取消|[KIN-K19](03-KINEMATICS.md#kin-k19)|
|运动学|K20|跨项目与迟到回执|[KIN-K20](03-KINEMATICS.md#kin-k20)|
|运动学|K21|只读与无项目门控|[KIN-K21](03-KINEMATICS.md#kin-k21)|
|运动学|K22|区域参考系与独立TCP|[KIN-K22](03-KINEMATICS.md#kin-k22)|
|运动学|K23|冻结采样与身份|[KIN-K23](03-KINEMATICS.md#kin-k23)|
|运动学|K24|位置与姿态覆盖分母|[KIN-K24](03-KINEMATICS.md#kin-k24)|
|运动学|K25|混合数据不足/零样本|[KIN-K25](03-KINEMATICS.md#kin-k25)|
|运动学|K26|区域操作入口与结果|[KIN-K26](03-KINEMATICS.md#kin-k26)|
|运动学|K27|缺检测器/几何错误|[KIN-K27](03-KINEMATICS.md#kin-k27)|
|运动学|K28|共享策略一致性|[KIN-K28](03-KINEMATICS.md#kin-k28)|
|运动学|K29|Home/Zero与命名姿态|[KIN-K29](03-KINEMATICS.md#kin-k29)|
|运动学|K30|Jog|[KIN-K30](03-KINEMATICS.md#kin-k30)|
|运动学|K31|Playback|[KIN-K31](03-KINEMATICS.md#kin-k31)|
|运动学|K32|失败点三维|[KIN-K32](03-KINEMATICS.md#kin-k32)|
|运动学|K33|薄弱区三维|[KIN-K33](03-KINEMATICS.md#kin-k33)|
|运动学|K34|碰撞对象高亮|[KIN-K34](03-KINEMATICS.md#kin-k34)|
|运动学|K35|最差裕量三维|[KIN-K35](03-KINEMATICS.md#kin-k35)|
|运动学|K36|筛选与最差项排序入口|[KIN-K36](03-KINEMATICS.md#kin-k36)|
|运动学|K37|JSON/CSV导出|[KIN-K37](03-KINEMATICS.md#kin-k37)|
|运动学|K38|导出失败原子性|[KIN-K38](03-KINEMATICS.md#kin-k38)|
|运动学|K39|批量复算|[KIN-K39](03-KINEMATICS.md#kin-k39)|
|运动学|K40|随机均匀采样|[KIN-K40](03-KINEMATICS.md#kin-k40)|
|运动学|K41|关节网格采样|[KIN-K41](03-KINEMATICS.md#kin-k41)|
|运动学|K42|采样后台与进度取消|[KIN-K42](03-KINEMATICS.md#kin-k42)|
|运动学|K43|七种指标着色|[KIN-K43](03-KINEMATICS.md#kin-k43)|
|运动学|K44|XY/XZ/YZ投影|[KIN-K44](03-KINEMATICS.md#kin-k44)|
|运动学|K45|散点/包络/标签图例|[KIN-K45](03-KINEMATICS.md#kin-k45)|
|运动学|K46|PNG导出|[KIN-K46](03-KINEMATICS.md#kin-k46)|
|运动学|K47|选样本与三维回写|[KIN-K47](03-KINEMATICS.md#kin-k47)|
|运动学|K48|采样碰撞与策略跳转|[KIN-K48](03-KINEMATICS.md#kin-k48)|
|运动学|K49|外包络方向/默认180|[KIN-K49](03-KINEMATICS.md#kin-k49)|
|运动学|K50|外包络后台取消与标识|[KIN-K50](03-KINEMATICS.md#kin-k50)|
|运动学|K51|位姿计划预览|[KIN-K51](03-KINEMATICS.md#kin-k51)|
|运动学|K52|位姿后台报告|[KIN-K52](03-KINEMATICS.md#kin-k52)|
|运动学|K53|显示单位入口|[KIN-K53](03-KINEMATICS.md#kin-k53)|
|运动学|K54|inch/grad/turn子项|[KIN-K54](03-KINEMATICS.md#kin-k54)|
|运动学|K55|完整配置编辑|[KIN-K55](03-KINEMATICS.md#kin-k55)|
|运动学|K56|保存取消重开|[KIN-K56](03-KINEMATICS.md#kin-k56)|
|运动学|K57|运行身份和过期提示|[KIN-K57](03-KINEMATICS.md#kin-k57)|
|运动学|K58|策略权威|[KIN-K58](03-KINEMATICS.md#kin-k58)|
|运动学|K59|设默认TCP|[KIN-K59](03-KINEMATICS.md#kin-k59)|
|运动学|K60|设默认设备|[KIN-K60](03-KINEMATICS.md#kin-k60)|
|运动学|K61|空态/缺项/失败/过期|[KIN-K61](03-KINEMATICS.md#kin-k61)|
|运动学|K62|诊断定位及比较字段|[KIN-K62](03-KINEMATICS.md#kin-k62)|
|运动学|K63|键盘/复制粘贴/选择|[KIN-K63](03-KINEMATICS.md#kin-k63)|
|运动学|K64|真网格与TCP坐标架|[KIN-K64](03-KINEMATICS.md#kin-k64)|
|运动学|K65|三维导航与显示|[KIN-K65](03-KINEMATICS.md#kin-k65)|
|运动学|K66|DPI/缩放/布局恢复|[KIN-K66](03-KINEMATICS.md#kin-k66)|
|运动学|K67|响应与耗时隔离|[KIN-K67](03-KINEMATICS.md#kin-k67)|
|运动学|K68|黄金对照与确定性|[KIN-K68](03-KINEMATICS.md#kin-k68)|
|运动学|K69|修改与失效边界|[KIN-K69](03-KINEMATICS.md#kin-k69)|
|运动学|K70|归档重开与结果追溯|[KIN-K70](03-KINEMATICS.md#kin-k70)|

## 需求条目到操作卡

此表便于回查，操作卡内提及需求不代表全部子语义已经实测通过。公共能力中的分期项仅列已存在源码面和需核实入口，不将所有分期需求默认判已实现。

|需求条目|人工检查项|
|---|---|
|REQ-01|[REQ-R01-01](01-REQUIREMENTS.md#req-r01-01)、[REQ-R01-02](01-REQUIREMENTS.md#req-r01-02)、[REQ-R01-03](01-REQUIREMENTS.md#req-r01-03)、[REQ-R01-04](01-REQUIREMENTS.md#req-r01-04)、[REQ-R01-05](01-REQUIREMENTS.md#req-r01-05)、[REQ-R01-06](01-REQUIREMENTS.md#req-r01-06)、[REQ-R01-07](01-REQUIREMENTS.md#req-r01-07)、[REQ-R01-08](01-REQUIREMENTS.md#req-r01-08)、[REQ-R01-09](01-REQUIREMENTS.md#req-r01-09)、[REQ-R01-10](01-REQUIREMENTS.md#req-r01-10)、[REQ-R01-11](01-REQUIREMENTS.md#req-r01-11)、[REQ-R01-12](01-REQUIREMENTS.md#req-r01-12)|
|REQ-02|[REQ-J-03](01-REQUIREMENTS.md#req-j-03)、[REQ-R02-01](01-REQUIREMENTS.md#req-r02-01)、[REQ-R02-02](01-REQUIREMENTS.md#req-r02-02)、[REQ-R02-03](01-REQUIREMENTS.md#req-r02-03)、[REQ-R02-04](01-REQUIREMENTS.md#req-r02-04)|
|REQ-03|[REQ-R03-01](01-REQUIREMENTS.md#req-r03-01)、[REQ-R03-02](01-REQUIREMENTS.md#req-r03-02)、[REQ-R03-03](01-REQUIREMENTS.md#req-r03-03)、[REQ-R03-04](01-REQUIREMENTS.md#req-r03-04)、[REQ-R03-05](01-REQUIREMENTS.md#req-r03-05)、[REQ-R03-06](01-REQUIREMENTS.md#req-r03-06)、[REQ-R03-07](01-REQUIREMENTS.md#req-r03-07)、[REQ-R03-08](01-REQUIREMENTS.md#req-r03-08)、[REQ-R03-09](01-REQUIREMENTS.md#req-r03-09)、[REQ-R03-10](01-REQUIREMENTS.md#req-r03-10)、[REQ-R03-11](01-REQUIREMENTS.md#req-r03-11)|
|REQ-04|[REQ-R04-01](01-REQUIREMENTS.md#req-r04-01)、[REQ-R04-02](01-REQUIREMENTS.md#req-r04-02)、[REQ-R04-03](01-REQUIREMENTS.md#req-r04-03)、[REQ-R04-04](01-REQUIREMENTS.md#req-r04-04)、[REQ-R04-05](01-REQUIREMENTS.md#req-r04-05)、[REQ-R04-06](01-REQUIREMENTS.md#req-r04-06)、[REQ-R04-07](01-REQUIREMENTS.md#req-r04-07)、[REQ-R04-08](01-REQUIREMENTS.md#req-r04-08)、[REQ-R04-09](01-REQUIREMENTS.md#req-r04-09)、[REQ-R04-10](01-REQUIREMENTS.md#req-r04-10)|
|REQ-05|[REQ-R05-01](01-REQUIREMENTS.md#req-r05-01)、[REQ-R05-02](01-REQUIREMENTS.md#req-r05-02)、[REQ-R05-03](01-REQUIREMENTS.md#req-r05-03)、[REQ-R05-04](01-REQUIREMENTS.md#req-r05-04)、[REQ-R05-05](01-REQUIREMENTS.md#req-r05-05)、[REQ-R05-06](01-REQUIREMENTS.md#req-r05-06)、[REQ-R05-07](01-REQUIREMENTS.md#req-r05-07)、[REQ-R05-08](01-REQUIREMENTS.md#req-r05-08)、[REQ-R05-09](01-REQUIREMENTS.md#req-r05-09)、[REQ-R05-10](01-REQUIREMENTS.md#req-r05-10)、[REQ-R05-11](01-REQUIREMENTS.md#req-r05-11)|
|REQ-06|[REQ-R01-11](01-REQUIREMENTS.md#req-r01-11)、[REQ-R06-01](01-REQUIREMENTS.md#req-r06-01)、[REQ-R06-02](01-REQUIREMENTS.md#req-r06-02)、[REQ-R06-03](01-REQUIREMENTS.md#req-r06-03)、[REQ-R06-04](01-REQUIREMENTS.md#req-r06-04)、[REQ-R06-05](01-REQUIREMENTS.md#req-r06-05)、[REQ-R06-06](01-REQUIREMENTS.md#req-r06-06)、[REQ-R06-07](01-REQUIREMENTS.md#req-r06-07)、[REQ-R06-08](01-REQUIREMENTS.md#req-r06-08)、[REQ-R06-09](01-REQUIREMENTS.md#req-r06-09)|
|REQ-07|[REQ-R07-01](01-REQUIREMENTS.md#req-r07-01)、[REQ-R07-02](01-REQUIREMENTS.md#req-r07-02)、[REQ-R07-03](01-REQUIREMENTS.md#req-r07-03)、[REQ-R07-04](01-REQUIREMENTS.md#req-r07-04)、[REQ-R07-05](01-REQUIREMENTS.md#req-r07-05)、[REQ-R07-06](01-REQUIREMENTS.md#req-r07-06)、[REQ-R07-07](01-REQUIREMENTS.md#req-r07-07)、[REQ-R07-08](01-REQUIREMENTS.md#req-r07-08)、[REQ-R07-09](01-REQUIREMENTS.md#req-r07-09)、[REQ-R07-10](01-REQUIREMENTS.md#req-r07-10)|
|REQ-08|[REQ-R08-01](01-REQUIREMENTS.md#req-r08-01)、[REQ-R08-02](01-REQUIREMENTS.md#req-r08-02)、[REQ-R08-03](01-REQUIREMENTS.md#req-r08-03)|
|REQ-09|[REQ-R09-01](01-REQUIREMENTS.md#req-r09-01)、[REQ-R09-02](01-REQUIREMENTS.md#req-r09-02)、[REQ-R09-03](01-REQUIREMENTS.md#req-r09-03)、[REQ-R09-04](01-REQUIREMENTS.md#req-r09-04)、[REQ-R09-05](01-REQUIREMENTS.md#req-r09-05)、[REQ-R09-06](01-REQUIREMENTS.md#req-r09-06)|
|REQ-10|[REQ-R10-01](01-REQUIREMENTS.md#req-r10-01)、[REQ-R10-02](01-REQUIREMENTS.md#req-r10-02)、[REQ-R10-03](01-REQUIREMENTS.md#req-r10-03)、[REQ-R10-04](01-REQUIREMENTS.md#req-r10-04)|
|REQ-11|[REQ-R11-01](01-REQUIREMENTS.md#req-r11-01)、[REQ-R11-02](01-REQUIREMENTS.md#req-r11-02)、[REQ-R11-03](01-REQUIREMENTS.md#req-r11-03)、[REQ-R11-04](01-REQUIREMENTS.md#req-r11-04)、[REQ-R11-05](01-REQUIREMENTS.md#req-r11-05)、[REQ-R11-06](01-REQUIREMENTS.md#req-r11-06)、[REQ-R11-07](01-REQUIREMENTS.md#req-r11-07)、[REQ-R11-08](01-REQUIREMENTS.md#req-r11-08)|
|REQ-12|[REQ-R12-01](01-REQUIREMENTS.md#req-r12-01)、[REQ-R12-02](01-REQUIREMENTS.md#req-r12-02)、[REQ-R12-03](01-REQUIREMENTS.md#req-r12-03)、[REQ-R12-04](01-REQUIREMENTS.md#req-r12-04)|
|MDL-01|[MDL-M01](02-MODELING.md#mdl-m01)、[MDL-M02](02-MODELING.md#mdl-m02)、[MDL-M03](02-MODELING.md#mdl-m03)、[MDL-M04](02-MODELING.md#mdl-m04)、[MDL-M05](02-MODELING.md#mdl-m05)|
|MDL-02|[MDL-M02](02-MODELING.md#mdl-m02)、[MDL-M06](02-MODELING.md#mdl-m06)|
|MDL-03|[MDL-M08](02-MODELING.md#mdl-m08)|
|MDL-04|[MDL-M15](02-MODELING.md#mdl-m15)、[MDL-M16](02-MODELING.md#mdl-m16)、[MDL-M17](02-MODELING.md#mdl-m17)、[MDL-M18](02-MODELING.md#mdl-m18)、[MDL-V01](02-MODELING.md#mdl-v01)|
|MDL-05|[MDL-M21](02-MODELING.md#mdl-m21)、[MDL-M22](02-MODELING.md#mdl-m22)|
|MDL-06|[MDL-M05](02-MODELING.md#mdl-m05)、[MDL-M10](02-MODELING.md#mdl-m10)、[MDL-M23](02-MODELING.md#mdl-m23)、[MDL-M24](02-MODELING.md#mdl-m24)、[MDL-M25](02-MODELING.md#mdl-m25)、[SYS-03](05-PROJECT-SHARED.md#sys-03)|
|MDL-07|[MDL-H01](02-MODELING.md#mdl-h01)、[MDL-H02](02-MODELING.md#mdl-h02)、[MDL-V03](02-MODELING.md#mdl-v03)|
|MDL-08|[MDL-M33](02-MODELING.md#mdl-m33)|
|MDL-09|[MDL-M04](02-MODELING.md#mdl-m04)、[MDL-M06](02-MODELING.md#mdl-m06)|
|MDL-10|[MDL-M07](02-MODELING.md#mdl-m07)|
|MDL-11|[MDL-M08](02-MODELING.md#mdl-m08)|
|MDL-12|[MDL-M09](02-MODELING.md#mdl-m09)、[MDL-M10](02-MODELING.md#mdl-m10)、[MDL-M11](02-MODELING.md#mdl-m11)、[MDL-M12](02-MODELING.md#mdl-m12)|
|MDL-13|[MDL-M18](02-MODELING.md#mdl-m18)、[MDL-M19](02-MODELING.md#mdl-m19)、[MDL-M20](02-MODELING.md#mdl-m20)|
|MDL-14|[MDL-M26](02-MODELING.md#mdl-m26)|
|MDL-15|[MDL-M27](02-MODELING.md#mdl-m27)|
|MDL-16|[MDL-M28](02-MODELING.md#mdl-m28)|
|MDL-17|[MDL-M29](02-MODELING.md#mdl-m29)|
|MDL-18|[MDL-M14](02-MODELING.md#mdl-m14)|
|MDL-19|[MDL-M13](02-MODELING.md#mdl-m13)|
|MDL-20|[MDL-M30](02-MODELING.md#mdl-m30)|
|MDL-21|[MDL-M31](02-MODELING.md#mdl-m31)|
|MDL-22|[MDL-M32](02-MODELING.md#mdl-m32)|
|MDL-12-S1|[MDL-M11](02-MODELING.md#mdl-m11)|
|KIN-01|[KIN-K01](03-KINEMATICS.md#kin-k01)、[KIN-K02](03-KINEMATICS.md#kin-k02)、[KIN-K03](03-KINEMATICS.md#kin-k03)、[KIN-K04](03-KINEMATICS.md#kin-k04)、[KIN-K05](03-KINEMATICS.md#kin-k05)|
|KIN-02|[KIN-K06](03-KINEMATICS.md#kin-k06)、[KIN-K07](03-KINEMATICS.md#kin-k07)、[KIN-K08](03-KINEMATICS.md#kin-k08)、[KIN-K09](03-KINEMATICS.md#kin-k09)、[KIN-K10](03-KINEMATICS.md#kin-k10)、[KIN-K11](03-KINEMATICS.md#kin-k11)、[KIN-K12](03-KINEMATICS.md#kin-k12)、[KIN-K13](03-KINEMATICS.md#kin-k13)、[KIN-K14](03-KINEMATICS.md#kin-k14)、[KIN-K15](03-KINEMATICS.md#kin-k15)|
|KIN-03|[KIN-K16](03-KINEMATICS.md#kin-k16)、[KIN-K17](03-KINEMATICS.md#kin-k17)、[KIN-K18](03-KINEMATICS.md#kin-k18)、[KIN-K19](03-KINEMATICS.md#kin-k19)、[KIN-K20](03-KINEMATICS.md#kin-k20)、[KIN-K21](03-KINEMATICS.md#kin-k21)|
|KIN-04|[KIN-K22](03-KINEMATICS.md#kin-k22)、[KIN-K23](03-KINEMATICS.md#kin-k23)、[KIN-K24](03-KINEMATICS.md#kin-k24)、[KIN-K25](03-KINEMATICS.md#kin-k25)、[KIN-K26](03-KINEMATICS.md#kin-k26)、[REQ-J-02](01-REQUIREMENTS.md#req-j-02)、[REQ-R03-10](01-REQUIREMENTS.md#req-r03-10)|
|KIN-05|[KIN-K27](03-KINEMATICS.md#kin-k27)、[KIN-K28](03-KINEMATICS.md#kin-k28)、[SYS-02](05-PROJECT-SHARED.md#sys-02)|
|KIN-06|[KIN-K29](03-KINEMATICS.md#kin-k29)、[KIN-K30](03-KINEMATICS.md#kin-k30)、[KIN-K31](03-KINEMATICS.md#kin-k31)、[TRJ-07](04-OTHER-MODULES.md#trj-07)|
|KIN-07|[KIN-K32](03-KINEMATICS.md#kin-k32)、[KIN-K33](03-KINEMATICS.md#kin-k33)、[KIN-K34](03-KINEMATICS.md#kin-k34)、[KIN-K35](03-KINEMATICS.md#kin-k35)|
|KIN-08|[KIN-K36](03-KINEMATICS.md#kin-k36)、[KIN-K37](03-KINEMATICS.md#kin-k37)、[KIN-K38](03-KINEMATICS.md#kin-k38)、[KIN-K39](03-KINEMATICS.md#kin-k39)|
|KIN-09|[KIN-K40](03-KINEMATICS.md#kin-k40)、[KIN-K41](03-KINEMATICS.md#kin-k41)、[KIN-K42](03-KINEMATICS.md#kin-k42)、[KIN-K43](03-KINEMATICS.md#kin-k43)、[KIN-K44](03-KINEMATICS.md#kin-k44)、[KIN-K45](03-KINEMATICS.md#kin-k45)、[KIN-K46](03-KINEMATICS.md#kin-k46)、[KIN-K47](03-KINEMATICS.md#kin-k47)、[KIN-K48](03-KINEMATICS.md#kin-k48)|
|KIN-10|[KIN-K49](03-KINEMATICS.md#kin-k49)、[KIN-K50](03-KINEMATICS.md#kin-k50)|
|KIN-11|[KIN-K51](03-KINEMATICS.md#kin-k51)、[KIN-K52](03-KINEMATICS.md#kin-k52)|
|KIN-12|[KIN-K53](03-KINEMATICS.md#kin-k53)、[KIN-K54](03-KINEMATICS.md#kin-k54)|
|KIN-13|[KIN-K55](03-KINEMATICS.md#kin-k55)、[KIN-K56](03-KINEMATICS.md#kin-k56)、[KIN-K57](03-KINEMATICS.md#kin-k57)、[KIN-K58](03-KINEMATICS.md#kin-k58)、[REQ-R03-04](01-REQUIREMENTS.md#req-r03-04)|
|KIN-14|[KIN-K59](03-KINEMATICS.md#kin-k59)、[KIN-K60](03-KINEMATICS.md#kin-k60)|
|KIN-12-S1|[KIN-K54](03-KINEMATICS.md#kin-k54)|

## 仍开放的问题必须进入本轮记录

|已有问题|重点操作卡|如何记结论|
|---|---|---|
|MA-01 网格完整性|MDL-M15、MDL-V01、KIN-K64、REQ-R10-02|格式/scale/帧内变换/法向分别记录，不以STL基础加载推翻未支持格式|
|MA-02 碰撞完整性|MDL-M17、SYS-02、KIN-K11/K27/K28/K34|正安全距离拒绝保持未封闭；真实对象对/规则/高亮另取证|
|MA-06 F07来源持久|MDL-H08/H09、SYS-01|关闭重开逐对provenance和导入候选，不以会话内撤销通过替代|
|MA-07 联合终验未齐|REQ-J-01～04、KIN-K18/K22/K70、TRJ/DYN/SEL/OPT/RPT|同项目冻结修订贯通真实入口；未装配记BLOCKED，不拼单元测试冒充全链|
|MA-08 真实视觉未齐/文字多重轮廓|MDL-V01～03、REQ-R08-02、KIN-K32～35/K64/K65|逐安装/TCP/遮挡/导航/临时编辑取图；已见错误记FAIL|
|MA-04 长表单人工矩阵未齐|MDL-H01/H05、KIN-K66、APP-17|逐页全展开/窄窗/DPI，不只验滚动容器存在|
|析构压力未单独验|REQ-U24、KIN-K20、APP-03、SYS-05|正常切换、取消专项与全应用析构压力分开记录|
|探索采样后台取消缺口|KIN-K42|当前为内联运行，不写成后台已实现|
|独立分析/单位/排序等入口待核实|KIN-K30/K31/K36/K49～54、OPT-08/12、APP-14|逐入口查找并建问题；不因内核存在预填PASS|

开放问题权威记录：[T29开放问题](../wp13-t29-closure/OPEN-ISSUES.md)。本轮没有修复产品代码或关闭这些问题。
