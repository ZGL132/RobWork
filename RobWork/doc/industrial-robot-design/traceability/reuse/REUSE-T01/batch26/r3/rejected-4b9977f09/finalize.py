import pathlib,subprocess,json,hashlib,datetime
out=pathlib.Path(__file__).parent;repo=out/'repo';sha='4b9977f096d0b3d58687ecc94013f435e5b0502c';base='d0fcb96f62a863684848161f7b8ecd93ef0be302'
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args])
prefix='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch26/rejected-aa31/'
retained=[]
for p in git('ls-tree','-r','--name-only',sha,'--',prefix).decode().splitlines():
 rel=p[len(prefix):];original=pathlib.Path(r'C:/Users/zgl18/AppData/Local/Temp/reuse-t01-b26-acceptance-20261023')/rel;data=git('show',sha+':'+p)
 retained.append(dict(path=p,original=str(original),rawMatch=data==original.read_bytes()))
(out/'prior-fail-preservation.json').write_text(json.dumps(retained,ensure_ascii=False,indent=2),encoding='utf-8')
searches=[]
for name,args in [('search-governance',['rg','-n','findings\\.json|merge-findings-union|update-index --index-info|merge-base','RobWork/scripts/industrialrobot']),('search-upstream',['rg','-n','union|merge\\(','RobWork/RobWork/src/rw/core','RobWork/RobWork/src/rw/common','RobWork/RobWorkSim/src/rwsim']),('search-upstream-test',['rg','-n','LinearInterpolatorTest|add_rw_gtest|sdurw-gtest-main','RobWork/RobWork/gtest/CMakeLists.txt'])]:
 p=subprocess.run(args,cwd=repo,capture_output=True);(out/(name+'.stdout.log')).write_bytes(p.stdout);(out/(name+'.stderr.log')).write_bytes(p.stderr);searches.append(dict(name=name,command=args,exitCode=p.returncode))
static=json.loads((out/'static-summary.json').read_text(encoding='utf-8'));checks=json.loads((out/'checks.json').read_text(encoding='utf-8'));ad=json.loads((out/'adversarial-report.json').read_text(encoding='utf-8'))
result=dict(schemaVersion='ird-independent-acceptance/1',taskId='REUSE-T01',batch='B26',round=2,reviewedCommit=sha,baseCommit=base,verdict='FAIL_LOCAL_B26_PS51_MISSING_STAGE_EXIT_CONTRACT',blockingFindings=[dict(id='B26-R2-01',file='RobWork/scripts/industrialrobot/merge-findings-union.ps1',line=59,expected='缺少三方stage应经Fail 2返回调用/环境错误',actual='Windows PowerShell 5.1因Git stderr触发NativeCommandError而退出1；PS7退出2',scope='新增stage读取审计段与脚本退出码契约；既有非回归，基点仅补parser BOM对照同样复现',worktreeAndIndexPreserved=True,evidence='fixtures-v3/powershell-missing-stage/stderr.log')],freeze=dict(independentClone=True,noHardlinks=True,noCheckoutThenDetached=True,coreAutocrlf=False,coreLongpaths=True,initialClean=True,finalHead=git('rev-parse','HEAD').decode().strip(),finalStatus=git('status','--short').decode(),noSharedWrites=True),checks=checks,archives=static['archiveSummaries'],priorFailPreserved=retained,static=dict(outOfScope=static['outOfScope'],gitlinks=static['gitlinks'],minimalMergePatch=static['minimalMergePatch'],bom=static['bom'],bindings=static['bindingSummary']),adversarial=dict(passed=ad['passed'],failed=ad['failed'],skipped=ad['skipped'],report='adversarial-report.json'),sourceSearches=searches,notRun=['C++构建、GUI、数值、性能（治理脚本局部验收不适用）','DeepEvidence','全REUSE-T01/T02或算法迁移验收','远程同步、CI、提交与合入'],limitations=['本结论仅B26冻结对象，不授予全T01或T02放行','验收夹具第一次使用NUL排除文件不被Git接受，第二次同号新增正例缺少真实增量；两次夹具失败日志保留，最终26例独立运行结果25/1/0','首次检出因Windows长路径128，clone本地启用core.longpaths后重试0；日志保留','静态首轮自编摘要校验漏了登记Digest末尾换行规范，原首轮报告保留；修正后110绑定0失败'],evidenceRoot=str(out))
(out/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# B26 第二轮独立局部验收：FAIL

冻结 `{sha}`，基点 `{base}`。独立 clone --no-hardlinks --no-checkout，core.autocrlf=false；不改共享树、不实施、不同步、不合入。

阻断 B26-R2-01：Windows PowerShell 5.1 缺三方 stage 时，merge-findings-union.ps1:59 的 Git stderr 在 Stop 错误策略下直接抛 NativeCommandError，退出 1，而脚本调用/环境错误契约为 2。PS7 返回 2。工作文件与 index 均未修改。基点原脚本仅补解析必需 BOM 后同样复现，属既有非回归；新增 stage-reading 审计仍遗漏此边界。证据：adversarial-report.json、fixtures-v3/powershell-missing-stage/stderr.log、baseline-stage-comparison.json。

通过：B24 Git blob 对 Temp 原件 173/173，B25 含 manifest 自身 159/159，B23 对基点原归档 136/136；无 gitlink/嵌套 Git 元数据。BOM/两处 -cne 的最小差异吻合；10 新行 pending/owner/NONE/空 targets，110 摘要/区段/符号绑定均通过。旧行仅补证据、刷新摘要及扩展受影响 pipeline 回归区段，不改业务授权。

真实执行：两 Shell 完整 pipeline，validate-task、冻结 validate-reuse、validate-docs、显式 base..head 范围、commit-lint 七命令退出码均为 0。复用报告仍为治理检查，845 functions、DEBT=16、UNRESOLVED=1399，不构成算法合规。自选对抗 26 例 passed=25、failed=1、skipped=0。

原 aa31 首轮 FAIL 记录继续保留。NOT RUN：C++/GUI/数值/性能、DeepEvidence、全 T01/T02、远程 CI/同步/提交/合入。完整命令/退出码在 checks.json，证据在当前目录；可交付原始清单见 artifact-manifest.json（排除 repo、fixture Git 元数据）。
'''
(out/'acceptance.md').write_text(md,encoding='utf-8')
manifest=[]
for p in out.rglob('*'):
 if not p.is_file():continue
 rel=p.relative_to(out)
 if rel.parts[0]=='repo' or '.git' in rel.parts or p.name=='artifact-manifest.json':continue
 raw=p.read_bytes();manifest.append(dict(path=rel.as_posix(),bytes=len(raw),rawSha256=hashlib.sha256(raw).hexdigest()))
(out/'artifact-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(verdict=result['verdict'],formalCheckPassed=sum(x['exitCode']==0 for x in checks),adversarial=result['adversarial'],priorFailRawMatches=sum(r['rawMatch'] for r in retained),priorFailFiles=len(retained),manifestFiles=len(manifest),finalStatus=result['freeze']['finalStatus']),ensure_ascii=False,indent=2))
