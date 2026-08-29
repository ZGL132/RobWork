# 测试与证据契约

## 1. 测试分层

每个任务至少提供失败测试、正常路径测试和边界测试；公共接口必须有契约测试；算法必须有解析算例或独立参考；适配层必须有 RobWork/RobWorkSim 测试；GUI 测试必须单独启动可执行文件。

## 2. 数值和性能

算法级容差与端到端容差分开。Jacobian、FK、IK、动力学和积分规则引用需求第 15.3 节。性能测试引用 `benchmark-manifest.json`，固定硬件、数据集、线程、种子、预热、测量和统计方法。

## 3. Given/When/Then 格式

```text
Given 固定项目修订、输入切片和前置工件
When 执行一个公开命令或评估入口
Then 产生明确状态、结果/诊断、持久化变化和可复核证据
```

失败断言必须说明错误类别、诊断码、旧状态是否保持、是否创建修订以及可恢复动作。

## 4. 证据命名

测试报告至少包含任务 ID、需求 ID、提交 SHA、环境、命令、输入哈希、实际结果、期望结果、日志路径和评审者。人工试点使用签署记录和报告工件，不以空 CTest 目标替代。

## 5. Windows 规则

GUI 测试在 Visual Studio x64 开发环境运行，设置 `QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 可执行文件；模型测试可使用 `QCoreApplication`。门禁脚本兼容 Windows PowerShell 5.1 与 PowerShell 7。
