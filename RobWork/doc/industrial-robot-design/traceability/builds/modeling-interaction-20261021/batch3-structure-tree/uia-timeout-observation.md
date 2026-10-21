# Windows UIA超时观察摘录

本文件摘录本次工具真实返回，不是新增执行报告或产品测试通过证明。正式进程74616，主线程74920。箭头点击后调用UIA FindAll；客户端最终报 `Operation timed out. (0x80131505)`。期间一次CPU采样64.11→65.12秒/1秒，Responding=false。

首次capture-stack.py工具返回的部分栈帧（保留地址与名称；Windows系统无完整PDB，符号可能为最近导出名）：

| 地址 | 符号 |
| --- | --- |
| 0x7fff2f7cb957 | UiaNodeFromProvider |
| 0x7fff2f651982 | UiaHostProviderFromHwnd |
| 0x7fff2f65932f | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f65a59d | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f691f71 | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f69018c | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f7acc58 | UiaNodeFromProvider |
| 0x7fff2f697dac | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f6f9d78 | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff2f6f8a1c | UiaRaiseAutomationPropertyChangedEvent |
| 0x7fff57dd8d04 | CoWaitForMultipleHandles |
| 0x7ffd95d7ed73 | QEventDispatcherWin32::processEvents |
| 0x7ffdad6bcf19 | QWindowsGuiEventDispatcher::processEvents |
| 0x7ffd95bd3194 | QEventLoop::exec |
| 0x7ffd95bcaafa | QCoreApplication::exec |

随后复取栈原文在batch3-uia-timeout-stack.json：已恢复NtUserMsgWaitForMultipleObjectsEx→Qt正常事件循环。结束该次自有进程后，新进程6656独立重放相同关节箭头操作，点击后不遍历UIA，直接屏幕截图确认关节折叠、连杆展开，Responding=true，随后正常关闭。见host-arrow-replay.png/json。

结论仅为鼠标折叠单独复核通过；UIA枚举兼容性限制保留，未修改Windows/Qt提供者，未证明可访问性遍历问题已修复。文件夹对话框的首次自动输入同样未作为通过证据，最终打开方式为已有最近项目菜单。
