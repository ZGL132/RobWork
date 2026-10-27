import pathlib, json, subprocess, hashlib, shutil, datetime
OUT=pathlib.Path(__file__).resolve().parent
ROOT=OUT.parent
REPO=ROOT/'clone'
HEAD='52285db6f0d276742531ecb7e7bcf73db283e563'
B='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch30/'
inspection=json.loads((OUT/'inspection.json').read_text(encoding='utf-8'))
delta=json.loads((OUT/'delta-and-coverage.json').read_text(encoding='utf-8'))
def git(*args): return subprocess.check_output(['git','-C',str(REPO),*args])
gates={name:int((OUT/(name+'.exit.txt')).read_text(encoding='utf-8-sig').strip()) for name in ['task','docs','reuse','scope','commit-lint','diff-check','inspection','delta-and-coverage']}
ok=not inspection['errors'] and not delta['errors'] and all(v==0 for v in gates.values())
references=OUT/'referenced-original'
references.mkdir(exist_ok=True)
for name,path in [
 ('independent-gtest.xml',B+'r2/rejected-090075171/gtest.xml'),
 ('independent-ird-test-report.json',B+'r2/rejected-090075171/ird-test-report.json'),
 ('rejected-acceptance.json',B+'r2/rejected-090075171/acceptance.json'),
 ('original37-manifest.json',B+'artifact-manifest.json'),
 ('rejected57-manifest.json',B+'r2/rejected-090075171/archive-manifest.json'),
 ('r2original66-manifest.json',B+'r2/artifact-manifest.json')]:
 (references/name).write_bytes(git('show',HEAD+':'+path))
for name in ['clone.log','checkout.log','checkout-longpaths.log']:
 shutil.copyfile(ROOT/name,OUT/name)
status=git('status','--short').decode()
(OUT/'clone-final-status.log').write_text(status,encoding='utf-8')
if status.strip(): ok=False
acceptance={
 'schemaVersion':'ird-independent-acceptance/1','taskId':'REUSE-T01','batch':'B30-r2',
 'verdict':'PASS' if ok else 'FAIL','reviewedCommit':HEAD,'baseCommit':inspection['base'],
 'originalB30':inspection['original'],'createdUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
 'boundary':'仅B30-r2局部独立验收，不授全T01/T02、生产修复、算法替换、新核心、上游扩展或依赖安装权',
 'closedFinding':{'id':'B30-F01','status':'closed in this frozen revision' if ok else 'not accepted','reason':'两个公开hash行直接绑定Identity.cpp真实fnv1a128，全31行完整依赖摘要已核'},
 'gates':gates,'matrix':{'changedRows':31,'bindings':inspection['bindings']['count'],'bindingFailures':len(inspection['bindings']['failures']),'newRows':inspection['matrix']['added'],'allPending':inspection['matrix']['allPending']},
 'rawManifests':[{k:v for k,v in group.items() if k!='entries'} for group in inspection['rawManifests']],
 'rawBlobFailures':0 if not inspection['errors'] else 'see inspection.json',
 'original37AndManifestUnchanged':inspection['originalManifestUnchanged'],
 'sourceComparison':inspection['sourceComparison'],
 'cppRerun':False,'cppRerunReason':'指定范围文档/审计绑定返工；相对原B30产品、probe、CMake零变，引用归档原独验结果',
 'referencedTests':inspection['tests']['originalIndependent'],'knownFailures':'F-965：两个公开FNV hash反例仍为failed；GAP-DIGEST-01保持未批准',
 'freshPythonHashlibCngProperties':14,'freshFnvOracleFold':inspection['fnvOracleFold'],
 'searchReruns':[{'args':s['args'],'exit':s['exit']} for s in inspection['searchReruns']],
 'environmentNotes':['仅本地clone --no-hardlinks；无fetch/pull/push/安装/共享树写入','首次检出因Windows长路径exit128；以git -c core.longpaths=true重试exit0；冻结reuse首次exit1同因git show长路径，临时clone设置core.longpaths=true后重跑；影响限独立Temp clone'],
 'notRun':['C++重建/重跑（按零变范围引用原独验）','全T01/T02独立验收','跨平台/性能/超长流/消费者兼容性','远程操作/依赖安装'],
 'findings':[] if ok else ['see gate logs and inspection.json'],
 'archivePolicy':'仅evidence内报告、真实日志、结果、脚本及Git blob引用原件；不包含clone/build/.git；archive-manifest.json仅排除自身'
}
(OUT/'acceptance.json').write_text(json.dumps(acceptance,ensure_ascii=False,indent=2),encoding='utf-8')
md=f'''# REUSE-T01 B30-r2 独立验收

结论：**{acceptance['verdict']}，仅 B30-r2 局部**。

冻结 SHA：`{HEAD}`；范围基点：`{inspection['base']}`；原 B30：`{inspection['original']}`。

B30-F01 的两个公开 hash 行现均直接绑定 `core/src/Identity.cpp`，摘要为 `966007ba8d3822f096b9978af59d9dca1fcd9730e3e6c342237240fb6ae3d210`。已读真实 `fnv1a128` 定义与 `Digest.hpp` 两处调用。31 行共 622 个源码段、输入、证据绑定新鲜度全部一致；原有来源及输入保留，其他字段仅预期证据刷新。

原 37 项制品、失败独验归档 57 项、R2 原件 66 项均按冻结 Git blob 原字节核对 bytes/rawSha256，零失配，目录覆盖完整且清单均不自引用。原 37 项及其 manifest 相对原 B30 未变。详见 `inspection.json`、`delta-and-coverage.json`。

五项指定门禁真实运行：task={gates['task']}，docs={gates['docs']}，reuse={gates['reuse']}，scope={gates['scope']}，commit-lint={gates['commit-lint']}；附加 diff-check={gates['diff-check']}。scope 为 933dc6016..{HEAD}，共 {len(inspection['scopePaths'])} 路径。完整命令及日志见相应 `.command.txt`、`.log`、`.exit.txt`。

产品/probe/CMake 相对原 B30 零变，本次 C++ **NOT RUN**，引用原独验真实 13 跑：passed=11、failed=2、skipped=0。两项失败名称 `ContentVersionPublicFnv_CORE_T02` / `ContentIdentityPublicFnv_CORE_T02`，继续归 F-965。独立 Python hashlib 再核原两份 XML 各 14 条 CNG 属性全部一致；独立整数 FNV fold 为 `d84cf67ff3e90eee`。原有失败保持原记录，未洗绿。

R2 三条检索真实重跑退出码 1/0/0，分别表示正确 SHA 搜索无命中、Windows SDK BCrypt 符号命中、真实跨文件 FNV 定义/调用命中。

本地无硬链接克隆；第一次检出长路径失败，单次命令 `core.longpaths=true` 重试成功。冻结 reuse 首次 exit1，实因为内部 git show 同样遭遇 Windows 长路径；同一 blob 用长路径选项可读，故仅设置临时 clone 的 `core.longpaths=true` 后重跑，保留首次失败日志及诊断。未修改共享工作树，未执行远程操作或安装。克隆最终状态干净。

不授全 T01/T02、生产修复、算法替换、新核心、上游扩展或依赖安装；GAP-DIGEST-01 仍未批准。未执行跨平台/性能/超长流/全部消费者兼容性验收。

安全归档只包含 evidence 的报告、原始日志、结果、脚本及引用原件，不包含 clone/build/.git；raw 清单 `archive-manifest.json` 排除自身。
'''
(OUT/'acceptance.md').write_text(md,encoding='utf-8')
print(json.dumps({'verdict':acceptance['verdict'],'head':HEAD,'gates':gates,'archiveRoot':str(OUT)},ensure_ascii=False))
raise SystemExit(0 if ok else 1)
