# WP-14-T12 第十四批：真实场景拾取与资源闭包装配

正式宿主真实建模/保存/重开后，通过公开建模命令创建带真实STL外部资源记录的旋转夹具；沿真实双编译、生产发布事务、OpenGL像素raycast、Ctrl双击、名称反解、需求特征选择/确认写入。覆盖Frame及正/反FramePlaneNormal、取消、一次undo/redo、HEAD零增长、空白拾取清理和场景重编译后旧拾取失效。最后以仅场景对象命令移动夹具，独立核对新世界位置和新WorkCell。

真实链暴露两处生产缺陷并修复：HostProjectClosureView覆盖层丢失对象类型且按首条载荷猜根，改为借用完整ObjectWrite槽，按token选根、按OID返回类型/字节；HostModelCompilePort未绑定资源端口，补现有io资源桥装配。Recorded记录和声明键取同次根/reader，安全读取/预算/复读摘要归io；Solidified取同次闭包匹配内容版本副本。原Missing/Changed/Budget分类保留，失败不替换已发布快照。只改私有装配，无公共头/持久化格式/依赖边变化；契约精确增加两个私有HostCompilePort文件。

- batch14-host-initial：夹具命令失败，未执行后续拾取。batch14-host-command-probe定位reader解引用sceneRefs取得空类型。两轮失败保留。
- batch14-model-red：原覆盖层两个真实十段链回归失败。最终增加按token/字节独立断言，避免只凭非空判断。
- batch14-host-green：类型修复后继续失败，场景无几何引用；测试前置补真实资源记录。batch14-host-resource-green：命令已成功，但测试绕过生产应用编排未调用发布拍，仍观察旧宿主场景。补同款refreshPresentationAfterCommit，不直接注入WorkCell或名称表；原失败保留。
- batch14-model-green：三个回归通过：场景槽先于根、仅场景更新从基线取根、Recorded真实文件变化/缺失拒绝且原快照保持；Solidified实际闭包字节和摘要参与真实编译。测试不宣称mesh解析或渲染保真。
- batch14-resource-mutation：实际撤去CompileRequest.resources注入，三个最终回归全部失败，明确RT-RESOURCE-MISSING；随后恢复修复并全量重建。变异失败XML/JSON/console保留。
- batch14-host-published：正式宿主三种规则确认/取消/一次undo通过。batch14-host-final扩充redo、空白清理、场景单对象更新和旧拾取拒绝，115条断言通过、0失败，退出0。已查看mtour-pick-normal-confirm.png和mtour-5.7-scene-picked.png；确认框显示反向坐标平面法向和确认/取消按钮，正式场景显示编译标记盒。
- batch14-integrated-final：715跑，714通过、1旧占位跳过、0失败。需求模型220（219通过+1跳过）、需求契约16、需求GUI82、UI模型312、UI GUI85，各exe有XML＋per-exe JSON＋console。
- batch14-standalone-final：660跑，659通过、1旧占位跳过、0失败。需求318跑，UI模型267、UI GUI75；上述三个框架编译回归为集成专属，独立模式没有注册，不虚增通过数。
- batch14-build：双模式完整构建最终退出0，ird_gates零命中，validate-task通过。构建、诊断探针和变异构建日志保留；证据跟踪后重建索引并校验文档。

Windows GUI均VS2022 x64、windows平台、绝对路径、一次一个exe；双模式最终测试各自独立TEMP/TMP。源码最后只补中文契约注释，逻辑与最终构建/回归相同。

范围限制：runtime目前消费资源并校验摘要，但mesh呈现通道尚未落地，场景挂接的是标记盒。以上通过证明真实标记盒raycast、Frame身份及坐标平面法向规则写入，不能证明真实mesh外形或任意三角面法向。引用目录/三维创建拖动公共装配设计仍待所有者审阅，真实覆盖评估/跨域消费和原120项最终逐行验收继续，不能据此宣称需求模块全通过。
