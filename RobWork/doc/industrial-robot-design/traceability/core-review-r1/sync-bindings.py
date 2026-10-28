import json,subprocess,re,difflib,hashlib
from pathlib import Path
root=Path.cwd();d='RobWork/doc/industrial-robot-design/';p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/'
BASE='cdd0eae9bdba3d5c7acd049d5ad405f4446f0ceb'
def baseline(path):
 return subprocess.check_output(['git','show',BASE+':'+path]).decode('utf-8-sig')
e=d+'traceability/core-review-r1/';tf=d+'tasks/foundation/CORE-T12.json';mf=d+'reuse/changes/CORE-T12.json'
def read(path):return (root/path).read_text(encoding='utf-8-sig')
def write(path,obj):
 f=root/path;old=f.read_bytes() if f.exists() else b'';text=json.dumps(obj,ensure_ascii=False,indent=2)+'\n'
 if b'\r\n' in old:text=text.replace('\n','\r\n')
 f.write_bytes(text.encode())
def digest(text):return hashlib.sha256((text.removeprefix('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
changed=[p+'src/Identity.cpp',p+'src/IdentityRandom.hpp',p+'src/Events.cpp',p+'test/IdentityTest.cpp',p+'test/EventsTest.cpp',p+'test/BuildRedLineTest.cpp',p+'CMakeLists.txt',p+'src/Core.cpp']
template=json.loads(read(d+'tasks/REUSE-T37.json'))
task={'schemaVersion':'ird-task/1','taskId':'CORE-T12','unit':'core','masterWp':'WP-B','title':'core第一轮评审整改：身份播种、事件错误轨与参考句柄RAII','status':'ready','branch':'codex/core-review-fixes','dependsOn':[],'requirements':['ARC-04','CON-01','TASK-03','NFR-COR-02','NFR-MNT-01'],'designRefs':['units/core.md#4','units/core.md#5','reuse/policy.md#1'], 'allowedFiles':changed+[p+'include/sdurws/ird/core/README.md',d+'units/core.md',tf,mf,d+'reuse/functions/core.json',d+'reuse/functions/diagnostics.json',d+'reuse/functions/execution.json',d+'reuse/functions/ui.json',e+'**'],'forbiddenFiles':['RobWork/RobWork/src/**','RobWork/RobWorkSim/src/**','vcpkg/**'],'outputs':['core实现、回归与原始证据；逐文件及逐功能凭证；本地提交，待所有者独立验收'], 'verify':['cmake --build build --config Release --target sdurws_ird_core_test','cmake --build build/core-smoke --config Release --target sdurws_ird_core_test','pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile '+tf], 'interUnit':False,'acceptance':['固定128位熵消费回归先红后绿，真实生成入口完整身份去重；六类型/文本/FNV不变','四访问器在实际variant错配时抛CoreError；RAII析构退订、显式退订后句柄晚销毁、同sink参考语义及FIFO持句柄真实断言','集成/冒烟与直接project/execution事件消费者回归，日志/XML/原生报告齐全；ird_gates与复用文档门禁按实测报告','删除无符号Core.cpp、同步当前说明；不改变SHA/C4/R3/单位/泛型异常接口，不冒称独验通过'], 'note':'所有者2026-10-27明确要求从cdd0eae9bdba3d5c7acd049d5ad405f4446f0ceb新建分支和工作树，按core第一轮评审结果修改完善；此指令覆盖REUSE-T41列出的core参考句柄残留实施。原主工作树保持。独立验收由所有者另开会话，本实施不自签。新增最小契约仅为既有逐文件凭证机器检查所需，不另设治理状态。', 'governanceAcceptance':template['governanceAcceptance'],'reuseGateAcceptance':template['reuseGateAcceptance'],'reuse':{'policy':'H1-H6','manifest':mf}}
write(tf,task)
# 依据HEAD旧区段映射当前行号，保留已有功能身份/上游授权，不重写历史证据文件。
existing={x for x in changed if (root/x).exists()};all_changed=existing|{d+'units/core.md'}
new_hash={x:digest(read(x)) for x in all_changed}
core_path=d+'reuse/functions/core.json';matrix=json.loads(baseline(core_path))
def remap(source):
 path=source['path']
 if path not in existing or path.endswith('IdentityRandom.hpp'):return
 before=baseline(path).replace('\r\n','\n').splitlines()
 after=read(path).splitlines();ops=difflib.SequenceMatcher(a=before,b=after,autojunk=False).get_opcodes()
 def bound(x,end=False):
  for tag,a,b,c,z in ops:
   if tag=='insert' and a==x:return c if end else z
   if a<=x<b:
    if tag=='equal':return c+x-a
    return z if end and x>a else c
  return len(after)
 start=bound(source['startLine']-1);end=bound(source['endLine'],True)
 if end<=start:end=start+1
 if source['symbol'] not in '\n'.join(after[start:end]):raise RuntimeError('symbol lost '+str(source))
 source.update(startLine=start+1,endLine=end,sha256=digest('\n'.join(after[start:end])))
for f in matrix['functions']:
 for s in f['sources']:remap(s)
 if any(s['path']==p+'src/Events.cpp' for s in f['sources']):
  f['decision']='business-specific';f['status']='migrated';f['capabilityId']='CORE-EVENT'
  f['differences']='CORE-T12：保留四事件/端口值协议与单线程测试参考；标准variant守卫兑现既有CoreError；参考句柄析构退订及FIFO持句柄已补，生产仍execution。rw::core::Event无RAII/线程/drain且core无core事件依赖权限，不能整体替换；独验待所有者，不据migrated冒称独验。'
  f['nonOverlapEvidence']={'path':e+'README.md','sha256':digest(read(e+'README.md'))}
# Events已逐函数登记：只将头注释/namespace/间隔行归邻接的真实函数，不追加整文件行。
event_sources=sorted([s for f in matrix['functions'] for s in f['sources'] if s['path']==p+'src/Events.cpp'],key=lambda s:s['startLine'])
event_lines=read(p+'src/Events.cpp').splitlines()
event_sources[0]['startLine']=1
for i,s in enumerate(event_sources):
 s['endLine']=event_sources[i+1]['startLine']-1 if i+1<len(event_sources) else len(event_lines)
 s['sha256']=digest('\n'.join(event_lines[s['startLine']-1:s['endLine']]))
# 新增未登记文件按真实函数/测试分区，不以文件凭证冒充语义审计。
def add_region(path,start,end,symbol,label,target):
 text=read(path).splitlines();row={'functionId':'core.CORE_T12.'+label,'ownerUnit':'core','requirements':(['NFR-MNT-01','NFR-MNT-02'] if path.endswith('/test/BuildRedLineTest.cpp') else ['TASK-03'] if 'Events' in path else ['ARC-04','CON-01','TASK-03']),'sources':[{'path':path,'symbol':symbol,'startLine':start,'endLine':end,'sha256':digest('\n'.join(text[start-1:end]))}],'targets':[target],'usageRole':'owner','permissionRef':'ARCH:3.6.1','capabilityId':'NONE','decision':'business-specific','status':'migrated','search':{'command':'见traceability/core-review-r1/README.md的本地上游检索','conclusion':'保留产品值/错误/测试契约，通用子操作使用标准库；不引入平行算法或上游扩展'},'differences':'CORE-T12：'+label+'；职责、保留/删除/复用和测试证据见本批记录，独立验收未完成','inputs':[{'path':path,'sha256':digest(read(path))},{'path':p+'CMakeLists.txt','sha256':digest(read(p+'CMakeLists.txt'))}],'auditEvidence':[{'path':e+'README.md','sha256':digest(read(e+'README.md'))}],'nonOverlapEvidence':{'path':e+'README.md','sha256':digest(read(e+'README.md'))}}
 matrix['functions'].append(row)
for path in sorted(existing):
 text=read(path).splitlines();covered=set()
 for f in matrix['functions']:
  for s in f['sources']:
   if s['path']==path:covered.update(range(s['startLine'],s['endLine']+1))
 # 独立测试文件按TEST块分段，已有Identity功能行以新增区段补覆盖。
 boundaries=[1]+[i+1 for i,line in enumerate(text) if re.match(r'^TEST\(',line)]+[len(text)+1]
 # 未曾登记的测试支持也按真实helper分段，保留完整注释/include覆盖。
 if path.endswith('/test/BuildRedLineTest.cpp'):
  boundaries+= [next(i+1 for i,line in enumerate(text) if ' * @brief 递归收集' in line)-1,next(i+1 for i,line in enumerate(text) if ' * @brief 读文本文件' in line)-1]
 if path.endswith('/test/EventsTest.cpp'):
  boundaries+= [next(i+1 for i,line in enumerate(text) if line.startswith('/// 事件收集'))]
 boundaries=sorted(set(boundaries))
 for a,b in zip(boundaries,boundaries[1:]):
  if a>=b:continue
  missing=[n for n in range(a,b) if text[n-1].strip() and n not in covered]
  if not missing:continue
  start=missing[0];end=missing[-1]
  region='\n'.join(text[start-1:end])
  candidates=['makeIdentityEngine','FixedIdentityEntropy','coreSourceDir','collectCppFiles','readFileOrFail','sampleTask','RecordingSink']
  sym=next((line.strip() for line in text[start-1:end] if line.startswith('TEST(')),None)
  if not sym:sym=next((name for name in candidates if name in region),None)
  if not sym:raise RuntimeError('unclassified uncovered region '+path+':'+str(start))
  label=Path(path).name.replace('.','_')+'.L'+str(start)
  add_region(path,start,end,sym,label,'sdurws_ird_core_test' if '/test/' in path else 'sdurws_ird_core')
write(core_path,matrix)
# 仅刷新实际变动绑定；保留源片段与输入各自Digest语义。
counts={}
for path in (root/(d+'reuse/functions')).glob('*.json'):
 j=json.loads(path.read_text(encoding='utf-8-sig'));n=0
 def visit(node):
  global n
  if isinstance(node,dict):
   if node.get('path') in new_hash and 'sha256' in node and 'startLine' not in node:
    value=new_hash[node['path']]
    if node['sha256']!=value:node['sha256']=value;n+=1
   for v in node.values():visit(v)
  elif isinstance(node,list):
   for v in node:visit(v)
 visit(j)
 if n:write(path.relative_to(root),j);counts[path.name]=n
files=[]
for path in changed:
 deleted=not(root/path).exists()
 files.append({'path':path,'sha256':'deleted' if deleted else digest(read(path)),'mode':'remove' if deleted else 'business','capabilities':[],'boundary':'CORE-T12：标准库多字熵播种/variant错误轨/参考句柄RAII与回归；删除空占位，保留规范协议、生产execution总线、C4/SHA/单位语义；职责与授权见本批记录','verify':e+'README.md'})
write(mf,{'schemaVersion':'ird-reuse-change/1','taskId':'CORE-T12','taskFile':tf,'files':files})
print('matrix binding repins',counts,'rows',len(matrix['functions']))
