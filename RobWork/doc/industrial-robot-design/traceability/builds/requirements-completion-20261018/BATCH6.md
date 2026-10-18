# WP-14-T12 第六批：非模态参数筛选/批量粘贴与当前单位解析

工位、区域、工况检查器复用UI公开参数表，支持筛选、单位列、TSV逐行错误定位、暂存、非模态确认和取消。数量基线直接读取领域SI值；位置缺失不补零、仅呈现当前姿态规则参数。确认经过原纯回填函数一次提交完整候选，一次撤销/重做。排队固定原编辑器和完整对象基线，切对象不写错对象、只读禁写、对象变化拒绝过期提交，域拒绝后表格重投权威值。

联调发现公共参数模型运行期切换单位后仍以初始单位解析：m切mm再输入250实际写250m。任务契约精确放行ui/src/FormEditCommon.cpp这一私有实现；setEditText改用当前会话单位，单格/TSV同源，不修改公共接口。

真实失败与修复链：

- batch6-red：三页均缺批量参数表，3例实际失败。
- batch6-first-green：测试误以为值可含单位后缀，按公共契约改为单位列声明、值列仅数值；保留原失败记录。
- batch6-second-green/third-green：工位单位换算真实失败；工况/对象锚连续运行存在剪贴板输入为空。
- batch6-unit-red-retry：独立模型回归实际复现cm/m切换仍按初始mm解析；此前batch6-unit-red为构建期间错误启动，未执行、无通过结论。
- batch6-clipboard-diagnosis：断言实际读回为空，定位Windows OLE所有权消息未派发；测试泵消息并限时3秒重试至真实剪贴板读回一致，失败仍失败、不跳过、不改生产入口。
- batch6-condition-diagnosis单跑通过；batch6-guards-green五例连续通过。最终全量需求GUI72例通过。

最终真实执行（VS2022 x64、QT_QPA_PLATFORM=windows、绝对exe、GUI串行）：

| 模式/证据目录 | 实际运行 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: | ---: |
| batch6-integrated-final（需求模型/契约/GUI） | 307 | 306 | 1 | 0 |
| batch6-standalone-final（需求模型/契约/GUI） | 307 | 306 | 1 | 0 |
| batch6-ui-model-final（集成UI模型） | 308 | 308 | 0 | 0 |
| batch6-standalone-ui-model-final（独立UI模型） | 266 | 266 | 0 | 0 |
| batch6-ui-gui-final（集成公共参数表GUI） | 10 | 10 | 0 | 0 |
| batch6-standalone-ui-gui-final（独立公共参数表GUI） | 10 | 10 | 0 | 0 |

集成/独立UI模型目标注册条件不同，计数分别列明，不混同为同一集合。唯一跳过仍为旧占位GuiRegistration.V22RegisteredNotExecuted_ACC1，不作为功能通过。

两模式目标构建及集成正式宿主/ird_gates退出0，日志见batch6-build；validate-task PASS。原120项正式宿主/三维联合验证尚未完成，继续实施，不宣称最终全部通过。
