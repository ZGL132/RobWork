import pathlib,json,hashlib,sys,subprocess,datetime
root=pathlib.Path(sys.argv[1]);repo=root/'clone'
def read(n):return json.loads((root/n).read_text(encoding='utf-8-sig'))
checks=read('checks.json');original=read('checks.json');(root/'checks-before-gate-completion.json').write_text(json.dumps(original,ensure_ascii=False,indent=2),encoding='utf-8');extra=read('extra-verification.json');gates={n:int((root/(n+'.exit.txt')).read_text(encoding='utf-8-sig').strip()) for n in ['task','docs','reuse','scope','commit-lint','diff-check-cr-at-eol']}
for x in checks['checks']:
 if x['name']=='fresh gate reuse':x['passed']=gates['reuse']==0;x['detail']='Initial snapshot preceded gate completion; rechecked final exit file.'
checks['passed']=sum(x['passed'] for x in checks['checks']);checks['failed']=sum(not x['passed'] for x in checks['checks']);raw=read('raw-blob-verification.json');tests=read('referenced-tests.json');passed=not checks['failed'] and all(v==0 for k,v in gates.items() if k!='diff-check-cr-at-eol') and extra['finding']['status']=='open' and extra['review']['status']=='candidate' and extra['pathRestrictionExists'] and all(x['executedNormalizedMatchesFrozen'] for x in extra['scriptVerification']) and all(m['sharedManifestRawMatchesBlob'] and all(e['passed'] and e['sharedRawMatchesBlob'] for e in m['entries']) for m in raw)
status=subprocess.check_output(['git','-c','core.longpaths=true','-C',str(repo),'status','--short']);(root/'clone-status-final.log').write_bytes(status);passed=passed and not status
(root/'checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf-8');accept={'taskId':'REUSE-T01','batch':'B31-r2','verdict':'ACCEPT' if passed else 'FAIL','reviewedCommit':checks['head'],'base':checks['base'],'reviewedOriginal':checks['original'],'scopeCount':checks['scopeCount'],'independent':True,'sharedTreeWrites':False,'networkOperations':False,'installation':False,'checks':{'passed':checks['passed'],'failed':checks['failed']},'gates':gates,'closedAcceptanceFinding':'B31-P2-01' if passed else None,'tests':tests,'cppRerun':False,'findingF966':'open','catalogReview':'candidate','rawManifestCounts':[len(x['entries']) for x in raw],'scope':'B31-r2 local only; no full T01/T02 admission','auditHarnessNote':'Initial audit sampled reuse before completion and recorded one not-yet-present gate as false. Original snapshot/log preserved; final checks reread actual completed exit file.'};(root/'acceptance.json').write_text(json.dumps(accept,ensure_ascii=False,indent=2),encoding='utf-8')
report=f'''# REUSE-T01 B31-r2 独立局部验收

结论：**{accept['verdict']}**。仅针对 B31-r2 修复与局部证据，不授全 REUSE-T01/T02 准入。

冻结 `{checks['head']}`，范围基点 `{checks['base']}`，原 B31 `{checks['original']}`。独立本地 `clone --no-hardlinks --no-checkout` 后 detached checkout；长路径启用 `core.longpaths=true`。只写本独立 Temp，未修改共享树、安装依赖或执行网络/远程 Git 操作。

B31-P2-01：实际重跑提交的八目录 `rg` 命令，排除 `industrial-robot-design`，覆盖 `.md/.rst/.hpp/.cpp/.txt`，exit0、stderr空、11条命中与提交逐条一致（仅统一路径分隔符和行尾）。真实阅读 `RobWork/doc/sphinx/tutorials/basic.rst` 161–207行，173行为 Deg2Rad 示例，205行明示 radians。规范化摘要为 `f1042b3b2af4c0ac555cb1cd34e25500d6c44858ae3135e09e5f8992994e437a`，与 search.read 及51行 inputs一致。

51行仅刷新 search、inputs、auditEvidence；所有来源符号/区段、输入及证据规范化摘要均匹配，保持 pending。catalog 只刷新 UTIL-UNITS.resourceSearch，review仍 candidate，原 docs、上游与权限保持不变。冻结校验器只允许 `^RobWork/(RobWork|RobWorkSim)/` 资源路径已核实；真实共享doc保持 search.read/inputs绑定，未伪造catalog docs路径或升级verified。

产品/上游源码/vcpkg、探测 C++、两级审计 CMake 与 oracle 相对原 B31 零变化，原 batch31 全部文件 Git blob 原字节保持。原件23项、归档失败45项、r2原件54项全部 bytes/rawSHA256 与冻结Git blob及共享提交现场匹配；三个manifest自身原字节摘要分别保留在 raw-blob-verification.json，均无自引用。原独验 FAIL 与原 exit2 搜索未改写，原提交与基点均为冻结提交祖先。

新跑 task/docs/reuse(-Head冻结)/scope({checks['scopeCount']}路径)/commit-lint 均 exit0；附加 cr-at-eol diff-check exit2，仅指出归档原件 make-safe-manifest.py:17 末尾空行；该原件已逐字节核对，遵循不改写历史证据要求，保留提示不洗绿。本次验收不把原件格式提示误判为功能证据不完整；复用结果只为 governance pass，functions=1011、closedFiles=0，不是算法合规。执行脚本的规范化内容均与冻结blob匹配。checks共{checks['passed']} passed / {checks['failed']} failed。最初证据汇总早于reuse完成，将未生成exit文件记为false；原汇总与日志保留，最终以完成后的真实exit文件复核收口。

C++本轮 NOT RUN（无相关变更），引用原独立执行 XML：22跑，19 passed / 3 failed / 0 skipped / 0 notRun，失败仍为 scalar/vector/Q 三个有限极值C4反例。独立 Fraction 复核差值/M=2、阈值/M=3/2，必须false；原实现两项皆Inf。F-966仍open，不将失败洗绿。

未运行跨平台、极长序列、全部消费者、上游完整套件、GUI或完整产品集成；不修改数值算法。源工作树已有未跟踪内容保留；独立clone最终status为空。交付清单只含本Temp顶层报告、日志、结果与脚本，排除clone/.git/build/fixture及清单自身。
''';(root/'report.md').write_text(report,encoding='utf-8')
entries=[]
for p in sorted(root.iterdir()):
 if p.is_file() and p.name!='safe-manifest.json':
  b=p.read_bytes();entries.append({'path':p.name,'bytes':len(b),'rawSha256':hashlib.sha256(b).hexdigest()})
manifest={'scope':'independent acceptance top-level reports/logs/results/scripts only','excludedDirectories':['clone','.git','build','fixture'],'onlyExcludedEligibleFile':'safe-manifest.json','entries':entries};(root/'safe-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({'verdict':accept['verdict'],'checks':accept['checks'],'gates':gates,'safeManifestEntries':len(entries)},ensure_ascii=False))
if not passed:sys.exit(1)

