$ErrorActionPreference='Stop'
$r=$PSScriptRoot
$a=Get-Content "$r/static-audit.json" -Raw|ConvertFrom-Json
$tests=Get-Content "$r/run/ird-test-report.json" -Raw|ConvertFrom-Json
$checks=@();foreach($name in 'configure','build','task','docs','reuse','commit-lint','diff-check'){
 $exit=[int](Get-Content "$r/$name.exit.txt");$checks+=@{name=$name;exit=$exit;verdict=$(if($exit -eq 0){'PASS'}else{'FAIL'});log="$name.log"}
}
$original=@($tests.tests|Where-Object {$_.testId -notmatch '^(EventLifetimeAudit|SourcedValueLifetimeAudit)\.'})
$validatorRecords=@();foreach($name in 'validate-task.ps1','validate-docs.ps1','validate-reuse.ps1','reuse-contract.ps1','commit-lint.ps1'){
 $p="RobWork/scripts/industrialrobot/$name";$f=Join-Path "$r/repo" $p
 $validatorRecords+=@{path=$p;executedCloneRawSha256=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLowerInvariant();frozenGitBlobId=(& git -C "$r/repo" rev-parse "549a61834bea9755b873dde478ee80c5b9f4b15a:$p");originalRawSha256=(Get-FileHash -LiteralPath (Join-Path 'C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork' $p) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$summary=@{
 taskId='REUSE-T01';batch='B32';reviewedCommit='549a61834bea9755b873dde478ee80c5b9f4b15a';base='a02785a10eada414533d9789eedac3f5af654a71';
 verdict='PASS';scope='仅B32审计记录及证据的局部独立验收；不授予全REUSE-T01/T02或生产修复权限';
 checks=$checks;validatorRecords=$validatorRecords;staticErrors=$a.errors;newFunctions=31;refreshedFunctions=7;sourceRanges=41;allPending=$true;sourceAndInputDigests='PASS';scopeFiles=39;
 originalArtifacts=@{entries=32;manifestSelfHashSeparatelyRecorded=$true;frozenBlobAndOriginalRawBytes='33/33 PASS';cloneCheckout='core.autocrlf converted seven text files and manifest; original/raw blob separately checked, not treated as corruption'};
 diffCheckBoundary=@{sourceCheck='PASS，14个md/json/cpp/CMake源材料；core.whitespace=cr-at-eol';allFilesCheckExit=2;allFilesFinding='两份保留的原始失败build日志含行36尾空白及EOF空行；不改历史原始日志，不算源材料缺陷';rawLog='diff-check-all.log'};
 findings=@{id='F-967';status='open';onlyNewFinding=$true;preservedPrefixBytes=1289098;rawSha256=$a.findingsPrefix.sha256;prefixUnchanged=$a.findingsPrefix.pass};
 rerun=@{developerEnvironment='Visual Studio 2022 x64 Hostx64/x64 MSVC 19.42.34433';qtPlatform='windows';absoluteExecutable="$r/build/Release/ird_event_lifetime_audit.exe";runExit=[int](Get-Content "$r/run/exit.txt");summary=$tests.summary;originalTests=$original.Count;originalPassed=@($original|Where-Object {$_.outcome -eq 'passed'}).Count;failedTest='EventLifetimeAudit.ReferenceHandleDestructionMustUnsubscribe_UT_EVT';failedObservations=@('subscriberCount()==1, expected 0','sink.count==1 after publish, expected 0');classification='FAIL为既有产品RAII缺陷的预期反例，未代修，非审计证据失败'};
 semanticReview=@('生产静态链：UiPlugin.cpp 749/755/845/925/1022、UiPlugin.hpp 614/617、ui/CMakeLists.txt 919/920/987及依赖列表真实；未运行GUI或证明最终DLL符号','ReferenceSubscription无析构退订；existing execution析构退订成功，重复订阅删一项而Reference删全部，回调专用线程不同于Reference发布线程；不能直接替换','SourcedValue两死亡测试固定terminate退出73，只确认合法泛型抛复制/比较进入noexcept终止，不证明所有消费者适用或崩溃','CORE-EVENT仍candidate，core仅math依赖权限未扩张；未复制或创建第三总线','输入覆盖core/execution/ui跨文件与上游Event原头/原两用例/构建入口；原测试原文件直接编译，报告沿用testkit/TestMainReport');
 notRun=@('真实GUI用户会话','最终生产插件DLL符号留存','UI替换execution后的线程/项目切换回归','并发退订与在投回调生命周期','全部SourcedValue消费者泛型约束','生产修正','全REUSE-T01验收','远程同步');
 safety=@{writes='仅Temp审计目录';clone='本地clone --no-hardlinks，core.longpaths=true，detached冻结SHA';sharedTreeEdited=$false;upstreamEdited=$false;vcpkgEdited=$false;network=$false;install=$false;remote=$false;subagents=$false;protectedDiffCount=0;safeEvidenceExcludes=@('repo/ clone','.git/','build/','fixture/');manifestDoesNotHashItself=$true}
}
if($a.errors.Count -or @($checks|Where-Object {$_.exit -ne 0}).Count){$summary.verdict='FAIL'}
$summary|ConvertTo-Json -Depth 20|Set-Content "$r/acceptance.json" -Encoding utf8
@"
# B32 局部独立验收：$($summary.verdict)

冻结提交：549a61834bea9755b873dde478ee80c5b9f4b15a；基点：a02785a10eada414533d9789eedac3f5af654a71。

范围仅B32审计证据。31条新增（core19/execution11/ui1）和7条刷新（Reference5/Sourced2）均pending；41段源码符号、区段、规范化摘要及跨文件inputs/证据摘要核对通过。39文件均在契约范围，无产品/upstream/vcpkg差异。32原件和manifest自身的原始字节分别与Git blob核对33/33通过。克隆受core.autocrlf影响的文本换行单独记录，原件未篡改。F-967是唯一追加，原findings前1,289,098字节SHA256保持a751ddefc1b3e6a2f557016af32862cc5e79c01d5f87e2e07f3a9744ee2c72b6。

VS x64重配置和构建通过；绝对路径exe单独运行：23跑22passed1failed0skipped，原17例通过。唯一失败ReferenceHandleDestructionMustUnsubscribe_UT_EVT同时观察订阅数1而非0、发布后回调数1而非0，bus/sink始终存活。F-967继续开放，不代修。existing execution的RAII探测通过，线程和重复订阅差异不能洗平；两Sourced死亡测试仅观察terminate73。

UiPlugin.cpp:749/755/845/925/1022、UiPlugin.hpp:614/617、ui CMake:919/920/987静态链已逐处审读。生产装配事实成立；没有执行GUI/最终插件DLL或UI改装后的线程、并发退订、项目切换回归。CORE-EVENT仍candidate，不扩大core权限、不新造总线。错误与诊断纯值比较未被冒充重复上游算法。

task/docs/reuse -Head/commit-lint/源码diff-check以及scope39结果见acceptance.json与日志；源码diff-check使用core.whitespace=cr-at-eol适配原任务CRLF。另保留全文件diff-check退出2：两份原始失败build日志的行36尾空白和EOF空行；没有改写原日志，此项不算源材料失败。全库DeepEvidence未执行，docs只授予治理事实结论。当前结论不授予全T01/T02或算法替换。

仅Temp写入；无联网、安装、fetch/pull/push、共享树修改或产品修复。共享源最初有.cache未跟踪项，后续batch33属于并行实施者现场，未触碰。交付清单排除clone/.git/build/fixture，清单不自引用。
"@|Set-Content "$r/acceptance.md" -Encoding utf8
$files=@('acceptance.json','acceptance.md','static-audit.json','source-excerpts.md','search-replay.json','audit.ps1','replay.ps1','finalize.ps1','clone.log','checkout.log','vs-env.log','configure.log','configure.exit.txt','build.log','build.exit.txt','task.log','task.exit.txt','docs.log','docs.exit.txt','reuse.log','reuse.exit.txt','commit-lint.log','commit-lint.exit.txt','diff-check.log','diff-check.exit.txt','diff-check-scope.txt','diff-check-all.log','diff-check-all.exit.txt','run/stdout.log','run/gtest.xml','run/ird-test-report.json','run/exit.txt')+@(1..6|ForEach-Object {"search-$_.log"})
$entries=@();foreach($p in $files){$f=Get-Item -LiteralPath (Join-Path $r $p);$entries+=@{path=$p;bytes=$f.Length;rawSha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}}
@{schemaVersion='ird-independent-evidence/1';entries=$entries;excludes=@('repo/','.git/','build/','fixture/');selfReference=$false}|ConvertTo-Json -Depth 6|Set-Content "$r/evidence-manifest.json" -Encoding utf8
$summary|Select-Object verdict,staticErrors,rerun|ConvertTo-Json -Depth 5
