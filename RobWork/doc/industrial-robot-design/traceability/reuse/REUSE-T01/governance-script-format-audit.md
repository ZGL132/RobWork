# B19：治理脚本功能审计的最小表达补强

全覆盖审查指出，固定产品库存以外的19个现行治理PowerShell脚本尚不能在矩阵中诚实表达：原CheckFunctions要求产品目录和CMake目标。不能伪造产品路径或目标来消除此遗漏。

本批只扩展已有`reuse-contract.ps1::CheckFunctions`及原隔离夹具，没有第二门禁平台或脚本目录白名单豁免。`foundation-governance`的待审行可标注真实`execution`入口，入口与同目录dot-source helper全文绑定，targets为空、owner、capabilityId=NONE。产品路径、其他目录、伪目标、伪迁移、缺失/过期上下文及不含入口的命令均拒绝。

这是结构表达能力，不是脚本语义PASS；命令包含路径不证明实际调用链，后者继续逐功能审读与独立验收。尚未登记这19个脚本的功能行。只支持pending/proposal，不能发模型权限或声明migrated/closed，最终模式继续拒绝未关闭功能。后续脚本结项仍需补受独立验收约束的关闭格式，不能以空targets豁免T06。

先用8项新增夹具运行旧helper，真实8 failed，报告与日志保留在`batch19/red/`；其中正例被旧产品/目标格式拒绝，负例也因旧格式先行拒绝而未到预期诊断。最初格式修补后的8项聚焦运行8 passed，保留于`batch19/intermediate-green/`，它对应中间helper，不替代当前版本完整回归。随后补入口与sourced-helper两项，共152项当前完整夹具。最终PS7与PS5.1结果、输入原始摘要、真实退出码见`batch19/verification.json`及两目录JSON/JUnit/每例日志。

前一B20提交已原样归档B17/B18独立验收记录，结论限各自冻结SHA的局部审计；B17的F-960、B18的F-961保持open，不授全T01/T02。未运行无关C++/GUI回归，当前补强必须冻结独立验收后才能作为后续脚本审计依据。
