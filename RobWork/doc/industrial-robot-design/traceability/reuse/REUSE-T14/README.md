# C4 授权方案 A 的候选验证（未实施）

适用输入：基础平台 `f378f01c5199032e08c2cce7ba0251a2f783d505`，候选的精确变化见 `rejected-candidate.patch`。所有者已批准 GAP-CORE-C4-01 方案 A，但提案明确要求缩放不能满足原语义时停止相关实施。生产 Compare.cpp / CompareTest.cpp 已恢复到该提交，F-966 保持 OPEN；不使用放宽阈值或其他新核心掩盖失败。

- 候选：只在 diff/limit 同时溢出时，以 std::scalbn 同除以二；正常路径原样保留。`probe-build.log` 为 MSVC x64 Release/O2，C4Probe.cpp 直接编译真实候选 Compare.cpp。
- `oracle.py` 用 Python Fraction 对 binary64 输入的精确有理值比较，固定种子 20261023；13,168 组，其中 7,889 组两侧溢出。`oracle-result.json`：**6 组溢出边界误绿**，另18组是正常路径原有舍入差异，不在本批准修正范围内。
- 具体失败：value=`0x1.ffffffffffffep+1023`，reference=`-0x1.fffffffffffffp+1023`，relative=`0x1.fffffffffffffp+0`，absolute=0。精确差值仍大于限值，但缩放后分别舍入为相等。不能把测试通过当作该边界已修复。
- `core.xml` / `core-report.json`：候选69 passed/0 skipped/0 failed；`runtime.xml` / `runtime-report.json`：候选305 passed/0 skipped/0 failed。两者是**被拒候选**的回归，不证明最终生产 C4 已修复；原B31三公开重载红例仍适用。
- 决定：候选 FAIL、撤回；不继续自写补偿比较核心。后续若采用标准 fma 残差等更强边界处理，须先给出具体精度/复杂度及正常路径边界提案并由所有者批准；本记录不批准该方向。其余 FNV/日志包可独立推进。

复现仅在独立实验现场应用 patch，以 VS x64 环境执行 `cl /std:c++17 /O2 /EHsc /utf-8 /I<core/include> C4Probe.cpp <core/src/Compare.cpp>`，再运行 oracle.py（探测可执行文件路径见脚本）。不在集成工作树恢复候选。独立验收 NOT RUN，不自签。

后续（2026-10-23）：修订候选 R2（fma 残差＋TwoSum 精确折叠）隔离实验 PASS，见 [revision-candidate/README.md](revision-candidate/README.md) 与 [../../reuse/extensions/GAP-CORE-C4-01.md](../../reuse/extensions/GAP-CORE-C4-01.md) 修订提案章节；待所有者批准，F-966 保持 OPEN。本目录原有失败原件未改动。
