# WP-14-T12 首批修复实测（2026-10-18）

本批只覆盖 A-01 的工况 name/note、A-02 六模板类别丢失、A-03 数量预览。完整120项清单仍见前次审计，其他条目未重新判定通过，任务尚未完成。

## 失败能力与修复

- red-gui.log/xml：新增两例真实生产Qt回归均失败，模板类别/数量预览不符，工况输入抛词表外键 name 异常。
- green-gui.log/xml：相同两例修复后通过。生产对话框通过引用显式回传类别、所有输入变化刷新预览；工况文本走文本提交轨。
- 集成三目标：282跑，281通过、1跳过、0失败；独立三目标相同。各目标目录保留 console.log、gtest.xml、ird-test-report.json。跳过为原有 GuiRegistration.V22RegisteredNotExecuted_ACC1，占位不能计通过。
- 本工作树集成正式宿主、需求相关目标与 ird_gates 构建成功；独立三目标构建成功。configure/build日志同目录。

## 正式宿主

本工作树 sdurws_ird_studio，VS2022 x64、QT_QPA_PLATFORM=windows，单实例。隔离短路径项目 C:/ird-wp14t12-test。

- interactive/02-template-inspection-preview：选择检测类别，修改X数量3、Y数量1，预览真实显示“将生成3个工位”。
- interactive/03-template-inspection-generated：确认后正式宿主状态行显示“工艺模板 Inspection 生成3个工位”。其余五类别由真实Qt生产对话框自动回归覆盖；本次未逐一在正式宿主手动复核。
- 工况名称/备注在真实面板自动测试通过；正式宿主输入复核尚未完成。
- 默认240px Dock仍裁切，A-12保留失败；未将浮动绕过记作修复。
- 深路径项目新建遇 publish osError=3（interactive/01），换短路径继续需求验证；未修复长路径问题。

## 后续

按实施计划继续结构化工况、工位引用与规则、完整镜像/阵列、CSV映射和原子导入、筛选粘贴/布局、三维创建拖动、runtime初始恢复和全120项实测。不得据本批281项自动测试通过声明全功能通过。
