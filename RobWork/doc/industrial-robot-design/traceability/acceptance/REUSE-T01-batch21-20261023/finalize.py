import json,pathlib,hashlib,subprocess,zipfile,datetime
root=pathlib.Path(__file__).parent
repo=root/'repo'
def j(p): return json.loads((root/p).read_text(encoding='utf-8-sig'))
def save(p,v): (root/p).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
def lines(t):return sorted(t.replace('\r\n','\n').splitlines())
scope=j('scope-and-script-checks.json'); bindings=j('binding-checks.json'); search=j('search-replay.json')
original=json.loads((repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch21/search.json').read_text(encoding='utf-8-sig'))
for i,(r,s) in enumerate(zip(search,original),1):
    r['sameHitLinesIgnoringOrderAndCRLF']=lines((root/f'search-{i}.log').read_text(encoding='utf-8'))==lines(s['stdout'])
    r['orderingNote']='rg 并行文件遍历次序不同；命中行集合及退出码相同' if not r['outputMatchesAfterCRLF'] else '输出次序也相同'
save('search-replay.json',search)
notes=[
 ('root-and-encoding','UTF-8 setter 仅捕获宿主 setter 失败；根由显式参数/脚本目录与 git rev-parse 取得，非新建路径解析核心。'),
 ('Test-CommitMessage','单一函数执行前缀、空/泛化/80字标题规则；Merge 豁免，正文真实性未由脚本判断，AllowWip 无新增语义。'),
 ('input-and-range','Message 与真实 git log/show 分支确实分开；空范围仅说明无新增提交，不产生历史全通过结论。'),
 ('aggregate-exit','每提交调用同一 Test-CommitMessage，累计后显式 exit1；只检查提交文案。'),
 ('validate-task.root','TaskFile/设计树存在性与标准路径操作；没有 RobWork 模型对象加载。'),
 ('Test-AnchorExists','编号标题压缩空白后的正则及数字边界；父节前缀允许是现存规则，不声称 Markdown 语义解析。'),
 ('Test-AnchorRequired','四权威设计文档和 units 要求锚点；过程证据整读不要求锚点，均属项目契约规则。'),
 ('index-and-contract','任务索引/字段/依赖存在/引用/分支/陷阱验证真实存在；依赖存在性不能证明依赖完成。branch=main 真实反例两路径拒绝。'),
 ('reuse-gate','转调已有 validate-reuse，未复制复用门禁；pending 行仅允许登记。'),
 ('preflight-and-dryrun','validate-task 与 validate-reuse 在 DryRun 前执行；正常打印 3 条命令，不能当作真实执行。'),
 ('execute-and-count','在仓库根真实 Invoke-Expression 并恢复目录；机器非零/非散文异常抛出；Parse/CommandNotFound 标 manual。此次真实 3 条机器命令，无 manual；其他分类缺陷留待后续审计。'),
 ('environment','实际运行时进程路径与 GUID Temp 根，使用 Git/PowerShell/.NET 原工具，无第二测试宿主。'),
 ('Invoke-Probe','子进程实际退出码和具名输出双条件，先记录再抛错；本轮红绿均有真实计数。'),
 ('fixtures-and-report','8 原用例及规则未变，仅补两套 gtest 稀疏检出；finally 报实际 executed/passed/failed，Temp 清理检查绝对路径前缀。')
]
semantic=[]
for c,(_,n) in zip(bindings,notes): semantic.append({'functionId':c['functionId'],'reviewed':True,'judgment':n,'conclusion':'本批 pending 治理登记语义符合源码；不构成结项授权'})
save('semantic-review.json',semantic)
assert len(bindings)==14 and scope['archiveRawEqual'] and not scope['protectedChanged']
for c in bindings:
    assert all(c[k] for k in ['definitionMatches','pending','noFakeTargets','ownerNone','executionMatches'])
    assert all(s['hashMatches'] and s['symbolLiteral'] and s['fullInputBound'] for s in c['sources'])
    assert all(x['hashMatches'] for k in ['inputs','auditEvidence'] for x in c[k])
assert all(x['exitMatches'] and x['sameHitLinesIgnoringOrderAndCRLF'] for x in search)
assert j('definitions-checks.json')['matches']
lite={}
for name in ['lite-ps7','lite-ps51','lite-before-ps7','lite-before-ps51']:
    report=j(name+'/governance-lite-report.json')
    lite[name]={k:report[k] for k in ['expected','executed','passed','failed','skipped','complete']}
    lite[name]['notRun']=report['expected']-report['executed']
    if 'before' in name:
        assert (report['executed'],report['passed'],report['failed'])==(7,6,1)
        assert 'REUSE-MISSING' in (root/name/'default-ignores-optional-index.log').read_text(encoding='utf-8')
        assert 'RobWork/RobWork/gtest/test-main.cpp' in (root/name/'default-ignores-optional-index.log').read_text(encoding='utf-8')
    else: assert report['complete'] and report['passed']==8 and report['failed']==0
commands=j('commands.json')
expected={'lite-before-ps7':1,'lite-before-ps51':1,'validate-invalid-branch':1,'verify-invalid-branch-dryrun':1,'verify-valid-dryrun':0,'verify-task-real':0,'commit-lint-frozen-range':0}
assert all(c['exitCode']==expected[c['name']] for c in commands)
for name in ['validate-invalid-branch','verify-invalid-branch-dryrun']:assert "invalid branch 'main'" in (root/(name+'.log')).read_text(encoding='utf-8')
assert '机器可执行 3 条全部通过；散文人工核对 0 条' in (root/'verify-task-real.log').read_text(encoding='utf-8')
assert (root/'scope.exit.txt').read_text(encoding='utf-8-sig').strip()=='0'
assert (root/'authored-whitespace.exit.txt').read_text(encoding='utf-8-sig').strip()=='0'
status=subprocess.check_output(['git','-C',str(repo),'status','--porcelain','--untracked-files=no'])
(root/'git-status-end.log').write_bytes(status)
assert not status.strip()
task=json.loads((repo/'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json').read_text(encoding='utf-8-sig'))
bad=j('invalid-branch.json'); bad['branch']=task['branch'];assert bad==task
summary={'reviewType':'independent-local-batch-review','subjectTask':'REUSE-T01','batch':'B21','conclusion':'B21_LOCAL_ACCEPTED','notStageAcceptance':True,'reviewedCommit':'cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7','comparisonBase':'d52fd06aafcb025a7d494d59c6d44bc9c16f3643','createdUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'四治理脚本14条pending登记与lite夹具修补；195路径包含B19/B20原件归档','functionsReviewed':14,'astDefinitions':58,'searchReplays':4,'archivesRawByteEquality':{'B19':86,'B20':52,'allEqual':True},'lite':lite,'realVerify':{'machineExecuted':3,'manual':0,'exitCode':0},'commands':commands,'scopeCheckExitCode':0,'findings':[],'limitations':['不是19治理脚本全审','未复跑全152复用测试','未执行C++/GUI/数值/性能','证据索引DeepEvidence全库审计未执行；lite坏索引负例已执行','未授全T01/T02或迁移/关闭资格；pending仍未结项','Parse/CommandNotFound分类边界与lint全部边界不在本轮完整覆盖内'],'whitespace':{'rawDiffCheckExitCode':2,'windowsDiffCheckExitCode':2,'authoredFilesWindowsDiffCheckExitCode':0,'reason':'CRLF按Git默认空白规则报警；cr-at-eol后仅原样归档及真实日志尾空格/空行，保留原件不修写'},'remoteDelivery':'NOT_PERFORMED_USER_REQUEST','implementationWorkspace':'只读，未改共享现场、提交、推送或合并','stageAuthorization':{'REUSE-T01':'NOT_GRANTED','REUSE-T02':'NOT_GRANTED'}}
save('acceptance.json',summary)
text='''# B21 局部独立验收

冻结 `cff60ddd4a05fdae399932d8b6a3ccb5b1cdada7`，比较基线 `d52fd06aafcb025a7d494d59c6d44bc9c16f3643`。结论：**B21 局部通过**，仅覆盖 commit-lint、validate-task、verify-task、test-governance-lite 四脚本的 14 条 pending 语义登记与 lite 稀疏夹具修补。不是全 T01 阶段验收记录，不授权 T02、模型权限、迁移或关闭。

临时冻结副本独立核对 14 行源码区段、字面锚点、全文 inputs、auditEvidence 摘要和治理 execution/空 targets/owner/NONE/pending 限制；原生 AST 重采样 58 条定义/入口与存档一致。四条上游 rg 命令重跑，退出码及命中行集合一致；两条命令有并行遍历次序差异。已读取两上游 CMake 测试宏和 RobWork test-main：它们是 C++ gtest 基础，不实现项目 PS 契约/提交/章节规则，TEST-HARNESS 保持 candidate。

195 条改动路径通过 verify-task-scope；产品、上游框架及 vcpkg 零改动。唯一脚本修补经明确 CRLF 文本比较，仅增加两套 gtest 稀疏目录和原因注释，无断言或索引规则修改。旧 before 原件与基线脚本原始字节一致；B19 的 86 文件与 B20 的 52 文件（各含 manifest）均以 Git blob 对原件逐字节相同，没有归一化历史。

| 本轮真实执行 | PS7 | PS5.1 |
| --- | --- | --- |
| 修后 lite | 8 expected / 8 executed / 8 passed / 0 failed / 0 skipped / 0 notRun，exit0 | 同左 |
| 原 before | 8 expected / 7 executed / 6 passed / 1 failed / 0 skipped / 1 notRun，exit1 | 同左 |

before 第 7 项真实报 `REUSE-MISSING : RobWork/RobWork/gtest/test-main.cpp`，不是其他前置提前拒绝。修后普通 docs 允许坏可选索引，DeepEvidence 真实具名拒绝坏 JSON。仅将契约 branch 改为 main 的 validate-task 和 verify-task -DryRun 均 exit1 且具名 invalid branch；原契约 DryRun exit0。真实 verify-task 再执行原三命令，机器 3 全部通过、人工 0、exit0。冻结新增提交 commit-lint exit0。

完整命令/退出码见 commands.json、probes.ps1 及逐项日志；两个修后 lite 的执行参数分别为同一冻结脚本 `-RepoRoot <本目录>/repo -OutputDir <本目录>/lite-ps7|lite-ps51`，解释器分别 pwsh 与 powershell。AST、逐行语义、搜索、原始字节核查分见 definitions-checks.json、semantic-review.json、search-replay.json、archive-raw-byte-checks.json。

未发现本批阻断问题。普通 git diff --check exit2，明确 cr-at-eol 后仍有原样归档及日志尾空格/空行；可编辑正文/脚本五文件在 Windows 口径下 exit0。没有为格式改历史。

未运行全 19 治理脚本审计、152 项复用回归、全库证据索引深审、C++/GUI/数值/性能。现有 Parse/CommandNotFound 人工分类边界及 lint 其他边界不因本批通过而被视作全部验证。未改共享现场、提交、推送或合并；按用户要求未同步远程，远程交付未执行，不阻塞本地局部验收。
'''
(root/'acceptance.md').write_text(text,encoding='utf-8')
(root/'finalize.log').write_text('All acceptance assertions verified; B21_LOCAL_ACCEPTED; no T01/T02 stage authorization\n',encoding='utf-8')
files=[p for p in root.rglob('*') if p.is_file() and 'repo' not in p.relative_to(root).parts and p.name not in ['artifact-manifest.json','accepted-b21-evidence.zip']]
save('artifact-manifest.json',[{'path':p.relative_to(root).as_posix(),'bytes':p.stat().st_size,'rawSha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(files)])
with zipfile.ZipFile(root/'accepted-b21-evidence.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in files+[root/'artifact-manifest.json']:z.write(p,p.relative_to(root).as_posix())
print('B21_LOCAL_ACCEPTED; scope=14 pending rows / 4 scripts; no T01/T02 stage authorization')
