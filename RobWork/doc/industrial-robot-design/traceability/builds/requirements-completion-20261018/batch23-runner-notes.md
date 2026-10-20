# 第23批测试运行器记录

- 首轮独立模式在同一PowerShell进程依次重复Enter-VsDevShell，后两组初始化出现命令长度/语法警告；已执行的exe均退出0，原XML/日志保留。为排除环境初始化疑点，requirements和ui分别在全新exec进程按VS x64/windows再次执行，最终统计采用clean-v2及ui-clean目录。
- 第一次尝试用pwsh -File传三个逗号分隔Targets，被当作一个exe名，未启动任何测试；batch23-final-standalone-requirements-clean目录没有XML，不计通过。随后用PowerShell实际数组参数、全新进程执行，证据为requirements-clean-v2。
- 最终集成首轮ui_contract因生产装配清单两个旧锚行失败，49通过/1失败，后续ui_gui未执行；修正活动清单行号后contract及gui在新目录执行并通过。初次失败不覆盖，最终统计仅采用重跑的两个exe。
- 所有实际GUI均使用windows平台、VS x64和绝对exe；全程一次一个，无子代理。
