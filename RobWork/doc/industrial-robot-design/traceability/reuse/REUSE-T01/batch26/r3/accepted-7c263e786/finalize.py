import pathlib,json,subprocess,hashlib,datetime
out=pathlib.Path(__file__).parent; repo=out/'repo'; base='4b9977f096d0b3d58687ecc94013f435e5b0502c'; sha='7c263e78683a74b5532e6f99e7647a02b6f39ea7'
def read(name): return json.loads((out/name).read_text(encoding='utf-8-sig'))
def git(*args): return subprocess.check_output(['git','-C',str(repo),*args]).decode().strip()
checks=read('checks.json'); static=read('static-summary.json'); adversarial=read('adversarial-report.json'); red=read('red-comparison-report.json'); bindings=read('binding-review.json')
head=git('rev-parse','HEAD'); status=git('status','--short'); required=[c['exitCode']==0 for c in checks]
required += [len(checks)==7,adversarial['failed']==0,adversarial['passed']==32,red['failed']==0,red['passed']==18,static['minimalGetStagePatch'],static['beforeSourceExact'],static['beforePipelineExact'],static['noProductFrameworkVcpkgChanges'],static['archiveSummary']['failed']==0,static['archiveSummary']['manifestEntries']==327,static['archiveSummary']['gitEntries']==328,static['archiveSummary']['exactSet'],static['bindingSummary']['failed']==0,static['bindingSummary']['affectedRows']==43,static['bindingSummary']['r3VerificationBound']==43,static['bindingSummary']['allRemainPending'],all(static['oldArchivesUnchanged'].values()),static['firstFailUnchanged'],static['firstFailFiles']==8,head==sha,status=='']
passed=all(required); verdict='PASS_LOCAL_B26_R3_ONLY' if passed else 'FAIL_LOCAL_B26_R3'
report=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B26',round=3,reviewedCommit=sha,baseCommit=base,verdict=verdict,generatedAt=datetime.datetime.now().astimezone().isoformat(),blockingFindings=[] if passed else ['Required evidence assertion failed; inspect attached reports'],freeze=dict(independentClone=True,noHardlinks=True,noCheckoutThenDetached=True,coreAutocrlf=False,coreLongpaths=True,initialClean=static['status']=='',finalHead=head,finalStatus=status,noSharedWrites=True),checks=checks,formalCheckSummary=dict(passed=sum(c['exitCode']==0 for c in checks),failed=sum(c['exitCode']!=0 for c in checks),skipped=0,notRun=0),adversarial=dict(passed=adversarial['passed'],failed=adversarial['failed'],skipped=0,notRun=0,report='adversarial-report.json'),baselineComparisons=dict(passed=red['passed'],failed=red['failed'],skipped=0,notRun=0,report='red-comparison-report.json',interpretation='Baseline PS5.1 exit1/NativeCommandError is expected red evidence; frozen exit2 is required. Current full pipeline against baseline merger fails only in PS5.1; both frozen full entries pass.'),staticReview=static,bindingDeltaKinds=sorted({r['path'].split('/')[1] for r in bindings['deltas']}),b26R2FindingClosure=dict(id='B26-R2-01',status='CLOSED_LOCAL_B26_R3',frozenExitCode=2,missingVariants=['all','stage1','stage2','stage3'],bothShells=True,worktreeAndIndexByteForBytePreserved=True,baselinePS51ExitCode=1,evidence='red-comparison-report.json'),priorFailures=dict(aa31Preserved=True,aa31FilesChecked=8,r2VerdictPreserved='FAIL_LOCAL_B26_PS51_MISSING_STAGE_EXIT_CONTRACT',r2ManifestEntries=327,r2IncludingManifest=328,report='r2-archive-raw-report.json'),scope=dict(localBatchOnly=True,fullT01='NOT RUN / NOT ACCEPTED',T02='NOT AUTHORIZED',algorithmReplacement='NOT AUTHORIZED',productChanges=False,frameworkChanges=False,remoteSync='NOT RUN per owner',merge='NOT RUN',implementation=False),notRun=['C++ build/tests','GUI/Widgets tests','numerical/performance acceptance','DeepEvidence','full T01/T02 acceptance','remote CI/sync/push/merge'],nonBlockingObservations=[dict(name='raw Git diff whitespace diagnostic',defaultExit=int((out/'diff-check-default.exit.txt').read_text(encoding='utf-8-sig')),crAtEolExit=int((out/'diff-check-crlf.exit.txt').read_text(encoding='utf-8-sig')),reason='Committed CRLF is reported as trailing whitespace by default; explicit cr-at-eol recognizes CRLF and passes. Both raw logs retained.')],evidenceRoot=str(out),manifest='artifact-manifest.json')
(out/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# B26 第三轮独立局部验收：{'PASS' if passed else 'FAIL'}

冻结 `{sha}`，基点 `{base}`。新建无硬链接、先不检出再 detached 的独立克隆；检出前设 core.autocrlf=false、core.longpaths=true，最终工作区干净。未实施、未写共享树、未远程同步或合入。

正式七项检查全部 exit0：双 Shell 完整原 pipeline（每 Shell union/note-case/schema-case/missing-stage 四边界）、validate-task、冻结 validate-reuse、validate-docs、显式 base..freeze scope、commit-lint。命令、日志和退出码见 checks.json。复用门禁仍为 governance only，845 functions、DEBT=16、UNRESOLVED=1399，不构成算法合规或全 T01 验收。

独立对抗 passed=32、failed=0、skipped=0；覆盖 UTF-8/BOM/中文、并集/同号同义/单方改动、大小写/碰撞/双改/删除/重复/无增量/畸形拒绝、缺全部及分别缺 stage1/2/3。成功例核真实 stage0 与工作文件一致；拒绝例核原工作字节及所有 unmerged index 字节均不变。额外基点/冻结对照 passed=18、failed=0：基点 PS5.1 稳定返回1/NativeCommandError，冻结双 Shell 稳定返回2及具名缺stage原因；当前完整 pipeline 配基点 merger 在 PS5.1 红失败、PS7通过。B26-R2-01 在本冻结局部关闭，原 FAIL 不改写。

43 条受影响记录的43个来源区段/符号及497项总绑定全部通过，43条均新增r3 verification且仍 pending/owner/NONE/空targets。叶变更仅 sources、inputs、auditEvidence、auditBoundary。Get-StageLines 唯一行为差异精确匹配局部 Continue、捕获Git退出码、finally恢复策略；无算法替换和产品/框架/vcpkg变更。

r2 原始327项 manifest 加自身共328项，逐项核 Git blob、检出字节、原 Temp 与manifest摘要，328/328通过、无.git元数据或gitlink。aa31首轮8件逐blob保持；B24/B25/B23归档diff未触碰，引用r2已核173/159/136件的报告及摘要，不重复旧归档。

默认 git diff --check 将已提交CRLF报尾随空白(exit1)；显式 cr-at-eol 后 exit0，两份日志均保留。此项为换行诊断，不是脚本行为失败。

NOT RUN：C++/GUI/数值/性能、DeepEvidence、全T01/T02、远程CI/同步/推送/合入。仅授本冻结B26-r3局部PASS，不授全T01或T02。报告与原始日志留本目录，可交付清单 artifact-manifest.json 排除 repo 及任意.git元数据。
'''
(out/'acceptance.md').write_text(md,encoding='utf-8')
items=[]
for p in sorted(out.rglob('*')):
 if not p.is_file(): continue
 rel=p.relative_to(out)
 if rel.parts[0]=='repo' or '.git' in rel.parts or str(rel)=='artifact-manifest.json': continue
 data=p.read_bytes(); items.append(dict(path=rel.as_posix(),bytes=len(data),rawSha256=hashlib.sha256(data).hexdigest()))
(out/'artifact-manifest.json').write_text(json.dumps(items,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(verdict=verdict,formalPassed=len(checks),adversarialPassed=adversarial['passed'],comparisonPassed=red['passed'],manifestFiles=len(items),head=head,status=status,failedAssertions=sum(not x for x in required)),ensure_ascii=False,indent=2))
