$ErrorActionPreference='Stop'
$a=$PSScriptRoot
$gates=[ordered]@{}
foreach($n in @('configure','build','task','docs','reuse','scope','commit-lint','diff-check-cr-at-eol')){
 $path=Join-Path $a ($n+'.exit.txt')
 if(-not(Test-Path $path)){throw "Missing completed result: $n"}
 $value=[int](Get-Content $path);$gates[$n]=$value
 if($value-ne0){throw "Gate did not pass: $n=$value"}
}
$bindings=Get-Content (Join-Path $a 'binding-results.json') -Raw|ConvertFrom-Json
$report=Get-Content (Join-Path $a 'run/ird-test-report.json') -Raw|ConvertFrom-Json
[xml]$xml=Get-Content (Join-Path $a 'run/gtest.xml') -Raw
$cases=@($xml.testsuites.testsuite.testcase)
$failed=@($cases|Where-Object {$_.failure})
$summary=$report.summary
if($summary.total-ne43-or$summary.passed-ne42-or$summary.failed-ne1-or$summary.skipped-ne0-or$failed.Count-ne1-or@($failed[0].failure).Count-ne4){throw 'Unexpected independent test outcome'}
if($bindings.bindingFailures.Count-or$bindings.manifestFailures.Count-or-not$bindings.findingsPrefix.match-or$bindings.protectedChanges.Count-or$bindings.catalogChanged){throw 'Binding or protected-scope failure'}
$accept=[ordered]@{
 schemaVersion='ird-independent-audit-acceptance/1';batch='REUSE-T01/B33';verdict='PASS_LOCAL_AUDIT_ONLY';reviewedCommit='e5c58aa0b6b3958b90e3a22ef42c36e4c9a4dc7e';directParent='88cae2102d16a338c5f0a7725d080f1c0d802f95';productBaseline='549a61834bea9755b873dde478ee80c5b9f4b15a';scopeFiles=26;reviewer='independent acceptance subagent; user-authorized acceptance only';reviewedAt=(Get-Date -Format o);gates=$gates
 tests=[ordered]@{status='EXPECTED_FAIL_REPRODUCED';executableExit=[int](Get-Content (Join-Path $a 'run/exit.txt'));total=43;passed=42;failed=1;skipped=0;original=[ordered]@{total=38;passed=38;failed=0;skipped=0};new=[ordered]@{total=5;passed=4;failed=1;skipped=0};failureTest=$failed[0].name;failedAssertions=4}
 evidence=[ordered]@{newRows=7;refreshedRows=1;sectionChecks=8;normalizedBindings=256;rawOriginals=21;externalDependencyRawHashes=4;manifestGitBlobRawSha256=$bindings.manifestGitBlobRawSha256;findingsPreservedPrefixBytes=1289777;allTouchedRowsPending=$true;capabilityNONEIsExemption=$false;catalogChanged=$false;protectedChanges=0}
 limitations=@('Only B33 audit evidence accepted; does not complete all REUSE-T01 or authorize REUSE-T02.','F-968 remains open; no product correction or new parser/backend approved.','Private output contract only; no demonstrated public diagnostic-object contamination.','Full logging/redaction/catalog, cross-locale behavior and remote sync not run.')
 environment=[ordered]@{clone='local --no-hardlinks; core.longpaths=true; detached frozen HEAD';developerEnvironment='Visual Studio 2022 amd64/amd64';qpa='windows';executables='one absolute executable per launch';externalDependencies='existing read-only installed channel, no network/install';sourceWorkspace='read-only; parent/unrelated untracked work preserved'}
 whitespace=[ordered]@{defaultDiffCheckExit=[int](Get-Content (Join-Path $a 'diff-check.exit.txt'));explanation='Default Git treats preserved CRLF raw evidence as whitespace; core.whitespace=cr-at-eol check exits 0. Both logs retained.'}
 remote='NOT RUN';implementation='NOT PERFORMED'
}
$accept|ConvertTo-Json -Depth 15|Set-Content -Encoding utf8 (Join-Path $a 'acceptance.json')
@'
# REUSE-T01 / B33 独立验收

结论：本批审计证据局部 PASS。冻结提交 e5c58aa0b6b3958b90e3a22ef42c36e4c9a4dc7e，直接父 88cae2102d16a338c5f0a7725d080f1c0d802f95，范围 26 文件。此结论不授予全部 T01 完成或 T02 实施许可。

7 条新功能行、1 条注册入口刷新全部 NONE/pending；8 个真实全限定符号/区段、256 项规范化区段与跨文件输入绑定一致。21 份原件按冻结 Git blob raw 大小与 SHA-256 全匹配；manifest 本身 Git blob raw SHA-256 为 7837bd073c6cf717b22b58649cfe2925f3b6ba07be22aaa501754042fcef5091。findings 前 1,289,777 字节 raw 保持；4 个只读安装 Boost 文件 raw 摘要一致，未冒充仓库上游。

隔离克隆在 VS x64 环境真实配置和构建通过，绝对 exe 一次运行：43 跑、42 passed、1 failed、0 skipped。原 38 例全过；新 5 例 4 passed、1 failed。唯一失败含四个 names.empty() 断言，输入分别为大写第二项、重复项、尾逗号、数组后多余字符。false 与非空 error 断言均通过。F-968 保持 open；注册和工厂均在 false 后停止，未证公共对象污染。

实际复查上游头/实现、测试、示例、共享 RobWork/doc 与 CMake；默认 DOM 为 XML，但允许插件，未外推生态不存在候选。已安装 PropertyTree 的类型擦除、Unicode 解转义与换行接受差异由真实测试确认。diagnostics 产品唯一单元依赖仍 core，catalog 未变；NONE 不是豁免。

task、docs、冻结 -Head reuse、26 文件 scope、commit-lint 均退出 0；复用门禁仅为治理校验，不代表算法合规。默认 diff --check 因保留 raw CRLF 报 2，保留原日志；core.whitespace=cr-at-eol 检查退出 0。产品、上游、vcpkg 零本批改动；无联网、安装、fetch/pull/push 或共享树写入。

完整诊断日志/脱敏/目录验收、跨 locale、新后端、生产修正、全 T01 和远程同步均 NOT RUN。证据仅归档安全清单列出的独立输出，排除 clone、.git、build、fixture 与缓存；摘要清单不包含自身。
'@|Set-Content -Encoding utf8 (Join-Path $a 'acceptance.md')
$files=@(Get-ChildItem $a -File|Where-Object {$_.Name-notin@('safe-file-list.json','evidence-manifest.json')}|ForEach-Object Name)+@(Get-ChildItem (Join-Path $a 'run') -File|ForEach-Object {'run/'+$_.Name})
foreach($p in $files){if($p-match'(^|/)(clone|repo|\.git|build|fixture|fixtures|\.cache)(/|$)' -or [IO.Path]::IsPathRooted($p) -or $p-match'(^|/)\.\.(/|$)'){throw "Unsafe export: $p"}}
[ordered]@{included=$files;excluded=@('clone/**','**/.git/**','build/**','fixture/**','fixtures/**','.cache/**','safe-file-list.json','evidence-manifest.json');selfReferential=$false}|ConvertTo-Json -Depth 5|Set-Content -Encoding utf8 (Join-Path $a 'safe-file-list.json')
$entries=@($files|Sort-Object|ForEach-Object{$i=Get-Item (Join-Path $a $_);[pscustomobject]@{path=$_;bytes=$i.Length;rawSha256=(Get-FileHash -Algorithm SHA256 $i.FullName).Hash.ToLowerInvariant()}})
[ordered]@{entries=$entries;excluded=@('clone/**','**/.git/**','build/**','fixture/**','fixtures/**','.cache/**','safe-file-list.json','evidence-manifest.json');selfReferential=$false}|ConvertTo-Json -Depth 5|Set-Content -Encoding utf8 (Join-Path $a 'evidence-manifest.json')
[pscustomobject]@{verdict=$accept.verdict;head=$accept.reviewedCommit;exportedArtifacts=$entries.Count;tests=$accept.tests;gates=$gates}|ConvertTo-Json -Depth 7
