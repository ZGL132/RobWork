"""DIAG-T12 局部凭证同步；沿 EX-T11 的差异映射方式，不关闭旧 pending 或变更权限。"""
import json, hashlib, difflib, subprocess, re
from pathlib import Path
BASE='af375242c7b632d572c8190ee0d954aa149d80dc'
D='RobWork/doc/industrial-robot-design/'
P='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
TRACE=D+'traceability/diagnostics-review-r1/'
TASK=D+'tasks/foundation/DIAG-T12.json'
MANIFEST=D+'reuse/changes/DIAG-T12.json'
def read(p): return Path(p).read_text(encoding='utf-8-sig')
def digest(s): return hashlib.sha256((s.removeprefix('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n').encode()).hexdigest()
def old(p): return subprocess.check_output(['git','show',BASE+':'+p],stderr=subprocess.DEVNULL).decode('utf-8-sig')
def write(p,data):
 Path(p).parent.mkdir(parents=True,exist_ok=True)
 Path(p).write_text(json.dumps(data,ensure_ascii=False,indent=1 if p.endswith('/testkit.json') else 2)+'\n',encoding='utf-8')
changed=subprocess.check_output(['git','diff','--name-only',BASE]).decode().splitlines()
changed+=subprocess.check_output(['git','ls-files','--others','--exclude-standard']).decode().splitlines()
products=sorted(set(p for p in changed if p.startswith(P)))
hashes={p:digest(read(p)) for p in products}
hashes[D+'units/diagnostics.md']=digest(read(D+'units/diagnostics.md'))
mappings={}
for p in products:
 try: before=old(p).splitlines()
 except subprocess.CalledProcessError: continue
 after=read(p).splitlines()
 mappings[p]=(after,difflib.SequenceMatcher(a=before,b=after,autojunk=False).get_opcodes())
def remap(src):
 p=src['path']
 if p not in mappings:return
 lines,ops=mappings[p]
 def bound(point,end=False):
  for tag,a,b,c,d in ops:
   if tag=='insert' and a==point:return c if end else d
   if a<=point<b:return c+point-a if tag=='equal' else (d if end and point>a else c)
  return len(lines)
 a=bound(src['startLine']-1);b=max(a+1,bound(src['endLine'],True))
 region='\n'.join(lines[a:b])
 if src['symbol'] not in region:
  prefix=src['symbol'].split('(',1)[0].split('{',1)[0].strip()
  matching=[line.strip() for line in lines[a:b] if prefix in line]
  if not matching:raise RuntimeError('lost symbol '+str(src))
  src['symbol']=matching[0]
 src.update(startLine=a+1,endLine=b,sha256=digest(region))
matrices={};updated=[]
for file in Path(D+'reuse/functions').glob('*.json'):
 p=file.as_posix();m=json.loads(old(p));original=json.dumps(m,ensure_ascii=False)
 m['functions']=[f for f in m['functions'] if not any(s['path'].endswith('/diagnostics/src/Logging.cpp') and 'isInternalDiagChannel(' in s['symbol'] for s in f['sources'])]
 for f in m['functions']:
  for src in f['sources']:remap(src)
 def repin(node):
  if isinstance(node,dict):
   if node.get('path') in hashes and 'sha256' in node and 'startLine' not in node:node['sha256']=hashes[node['path']]
   for v in node.values():repin(v)
  elif isinstance(node,list):
   for v in node:repin(v)
 repin(m)
 matrices[p]=m
 if json.dumps(m,ensure_ascii=False)!=original:updated.append(p)
def add(unit,relative,symbol,label,target,start=None,end=None):
 p=P+unit+'/'+relative;lines=read(p).splitlines();m=matrices[D+'reuse/functions/'+unit+'.json']
 if start is None:
  start=next(i for i,line in enumerate(lines) if symbol in line)
  indent=len(lines[start])-len(lines[start].lstrip())
  opened=next(i for i in range(start,len(lines)) if '{' in lines[i])
  if lines[opened].count('{')==lines[opened].count('}') and opened==start:end=opened+1
  else:end=next(i+1 for i in range(opened+1,len(lines)) if lines[i] in (' '*indent+'}',' '*indent+'};'))
 row={'functionId':unit+'.DIAG_T12.'+label,'ownerUnit':unit,'requirements':['ERR-01','NFR-SEC-07','NFR-MNT-03'],
 'sources':[{'path':p,'symbol':symbol,'startLine':start+1,'endLine':end,'sha256':digest('\n'.join(lines[start:end]))}],
 'targets':[target],'usageRole':'owner','permissionRef':'ARCH:3.6.3','capabilityId':'NONE','decision':'business-specific','status':'migrated',
 'search':{'command':'rg -n "class Log|class LogWriter|class LogStreamWriter|class Exception|UTF.?8" RobWork/RobWork/src/rw/core RobWork/RobWorkSim/src RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core',
 'conclusion':'上游通用日志无稳定码、两级脱敏、确认绑定及worker收尾语义；沿现有设施和标准库/Windows CRT薄适配。UTF-8仅合并既有私有切点，无新核心。'},
 'differences':'DIAG-T12：'+label+'；具体安全/事务/期限边界与独立预期见实施记录，不扩大owner/接口/性能标准。',
 'inputs':[{'path':p,'sha256':hashes[p]},{'path':P+unit+'/CMakeLists.txt','sha256':hashes[P+unit+'/CMakeLists.txt']}],
 'auditEvidence':[{'path':TRACE+'IMPLEMENTATION.md','sha256':digest(read(TRACE+'IMPLEMENTATION.md'))}],
 'nonOverlapEvidence':{'path':TRACE+'IMPLEMENTATION.md','sha256':digest(read(TRACE+'IMPLEMENTATION.md'))},
 'auditBoundary':'本包实施与自查；不关闭旧pending/proposal，不自签独立验收。'}
 m['functions'].append(row)
 path=D+'reuse/functions/'+unit+'.json'
 if path not in updated:updated.append(path)
for rel,symbol,label in [
 ('src/EntryDetail.hpp','inline std::size_t utf8PrefixSize(', 'utf8PrefixSize'),
 ('src/Confirmable.cpp','struct TransitionNotifications {','TransitionNotifications'),
 ('src/Logging.cpp','struct RedactionScope {','RedactionScope'),
 ('src/Logging.cpp','struct FlushCompletion {','FlushCompletion'),
 ('src/Logging.cpp','void enqueueDropSummary()','enqueueDropSummary'),
 ('src/Logging.cpp','bool shutdownExpired()','shutdownExpired'),
 ('src/Logging.cpp','void abandonDeferred()','abandonDeferred')]:add('diagnostics',rel,symbol,label,'sdurws_ird_diagnostics')
for unit,rel,target in [('diagnostics','test/DiagnosticsRegressionTest.cpp','sdurws_ird_diagnostics_test'),('ui','contract_test/DiagnosticsAssemblyContractTest.cpp','sdurws_ird_ui_contract_test')]:
 lines=read(P+unit+'/'+rel).splitlines();tests=[i for i,l in enumerate(lines) if l.startswith('TEST(')]
 add(unit,rel,'#include',Path(rel).stem+'.support',target,0,tests[0])
 for a,b in zip(tests,tests[1:]+[len(lines)]):add(unit,rel,lines[a],Path(rel).stem+'.'+lines[a].split(',')[1].strip(' )'),target,a,b)
# 新确认用例独立绑定，原末用例的相邻命名空间不吞入新测试。
p=P+'diagnostics/test/ConfirmableTest.cpp';lines=read(p).splitlines();first=next(i for i,l in enumerate(lines) if 'TEST(ConfirmableTest, Review_' in l)
for f in matrices[D+'reuse/functions/diagnostics.json']['functions']:
 for src in f['sources']:
  if src['path']==p and src['startLine']<=first and src['endLine']>first:src['endLine']=first;src['sha256']=digest('\n'.join(lines[src['startLine']-1:first]))
for a in [i for i,l in enumerate(lines) if l.startswith('TEST(ConfirmableTest, Review_')]:
 b=next((i for i in range(a+1,len(lines)) if lines[i].startswith('TEST(')),len(lines))
 add('diagnostics','test/ConfirmableTest.cpp',lines[a],lines[a].split(',')[1].strip(' )'),'sdurws_ird_diagnostics_test',a,b)
# 新增日志 helper 不能被前一旧函数的尾部区段冒充覆盖。
for symbol in ['void runLoopInner()','void finalize()']:
 path=P+'diagnostics/src/Logging.cpp';lines=read(path).splitlines();a=next(i for i,l in enumerate(lines) if symbol in l);indent=len(lines[a])-len(lines[a].lstrip());b=next(i+1 for i in range(a+1,len(lines)) if lines[i]==' '*indent+'}')
 for f in matrices[D+'reuse/functions/diagnostics.json']['functions']:
  for src in f['sources']:
   if src['path']==path and src['symbol']==symbol:src['endLine']=b;src['sha256']=digest('\n'.join(lines[src['startLine']-1:b]))
# Confirmable 原矩阵只绑定为 input：必须补真实函数区段，不能让新增通知结构覆盖整 TU。
rel='src/Confirmable.cpp'
for name in ['feedU32le','feedU64le','feedString','feedU8','feedBytes','feedComparativeValue',
             'codeLowerOf','findingStateToken','findingRecordDigest','findingIdEngine',
             'tryParseFindingId','FindingId::generate','FindingId::fromCanonical',
             'FindingId::tryFromCanonical','FindingId::toCanonical','FindingRecord::coreProjection',
             'ConfirmableService::ConfirmableService','ConfirmableService::create',
             'ConfirmableService::submitConfirmation','ConfirmableService::submitRejection',
             'ConfirmableService::invalidate','ConfirmableService::tryFind','ConfirmableService::pendingFor',
             'ConfirmableService::armExpiration','ConfirmableService::expireIfDue',
             'ConfirmableService::verifyBinding','ConfirmableService::finishTransition']:
 lines=read(P+'diagnostics/'+rel).splitlines()
 symbol=next(l.strip() for l in lines if name+'(' in l and l and not l[0].isspace())
 add('diagnostics',rel,symbol,'Confirmable.'+name.replace('::','.'),'sdurws_ird_diagnostics')
# 同理补齐既有确认用例与夹具；新用例不挪到文件首行吞掉旧测试。
rel='test/ConfirmableTest.cpp';lines=read(P+'diagnostics/'+rel).splitlines()
tests=[i for i,l in enumerate(lines) if l.startswith('TEST(')]
add('diagnostics',rel,'#include','ConfirmableTest.support','sdurws_ird_diagnostics_test',0,tests[0])
for a,b in zip(tests,tests[1:]+[len(lines)]):
 if 'Review_' not in lines[a]:
  add('diagnostics',rel,lines[a],'ConfirmableTest.'+lines[a].split(',')[1].strip(' )'),'sdurws_ird_diagnostics_test',a,b)
# regex 构造的初始化列表自带花括号；区段止于下一个真实成员定义，不吞 policySnapshot。
for f in matrices[D+'reuse/functions/diagnostics.json']['functions']:
 for src in f['sources']:
  if src['path'].endswith('/Redaction.cpp') and src['symbol'].startswith('explicit Impl('):
   lines=read(src['path']).splitlines();src['endLine']=next(i for i,l in enumerate(lines) if 'std::shared_ptr<const RedactionPolicy> policySnapshot()' in l)
# 检查/补齐相邻声明；不会另造注释功能或重写未变文件矩阵。
for p in products:
 unit=p[len(P):].split('/')[0];matrix=matrices[D+'reuse/functions/'+unit+'.json'];lines=read(p).splitlines()
 sources=sorted((s for f in matrix['functions'] for s in f['sources'] if s['path']==p),key=lambda s:s['startLine'])
 if not sources:
  if p.endswith('.hpp'):
   symbol=next(l.strip() for l in lines if l.startswith(('class ConfirmableService','class DiagnosticsFactory','class LoggingPipeline')))
   add(unit,p.split('/'+unit+'/',1)[1],symbol,Path(p).stem+'.PublicContract','sdurws_ird_'+unit,0,len(lines))
  elif p.endswith('.cpp'):
   for i,line in enumerate(lines):
    t=line.strip()
    if not t or t.startswith(('/', '*', '#', 'return ', 'if ', 'if(', 'for ', 'for(', 'while ', 'switch ', 'catch', 'namespace ', 'using ', 'throw ', 'case ')):continue
    if '(' not in t or '=' in t.split('(',1)[0] or (t.endswith(';') and '{' not in t):continue
    prefix=t.split('(',1)[0]
    if (prefix.startswith('std::') and ' ' not in prefix) or prefix.count('<')!=prefix.count('>'):continue
    if not re.match(r'[A-Za-z_][\w:<>,~*& ]*$',prefix):continue
    indent=len(line)-len(line.lstrip())
    try:
     opened=next(j for j in range(i,min(len(lines),i+10)) if '{' in lines[j])
     if ';' in '\n'.join(lines[i:opened]):continue
     if lines[opened].count('{')==lines[opened].count('}') and opened==i:end=i+1
     else:end=next(j+1 for j in range(opened+1,len(lines)) if lines[j]==' '*indent+'}')
    except StopIteration:continue
    add(unit,p.split('/'+unit+'/',1)[1],t,Path(p).stem+'.'+re.sub(r'[^A-Za-z0-9]+','_',prefix).strip('_'),'sdurws_ird_'+unit,i,end)
  else:raise RuntimeError('no source coverage '+p)
  sources=sorted((s for f in matrix['functions'] for s in f['sources'] if s['path']==p),key=lambda s:s['startLine'])
  if not sources:raise RuntimeError('no definitions '+p)
 sources[0]['startLine']=1;front=sources[0]
 for src in sources[1:]:
  if src['startLine']>front['endLine']+1:front['endLine']=src['startLine']-1
  if src['endLine']>front['endLine']:front=src
 front['endLine']=len(lines)
 for src in sources:
  region='\n'.join(lines[src['startLine']-1:src['endLine']]);assert src['symbol'] in region,src;src['sha256']=digest(region)
# 初始化列表中的模式花括号不是函数体；最终切掉构造区段与下一个成员的误重叠。
for f in matrices[D+'reuse/functions/diagnostics.json']['functions']:
 for src in f['sources']:
  if src['path'].endswith('/Redaction.cpp') and src['symbol'].startswith('explicit Impl('):
   lines=read(src['path']).splitlines();src['endLine']=next(i for i,l in enumerate(lines) if 'std::shared_ptr<const RedactionPolicy> policySnapshot()' in l)
   src['sha256']=digest('\n'.join(lines[src['startLine']-1:src['endLine']]))
for p in updated:write(p,matrices[p])
# 只重钉已有授权的源码摘要，不添加权限。
p=D+'reuse/permissions.json';permissions=json.loads(old(p));prior=json.dumps(permissions,ensure_ascii=False)
def repin_permission(n):
 if isinstance(n,dict):
  if n.get('path') in hashes and 'sha256' in n:n['sha256']=hashes[n['path']]
  for v in n.values():repin_permission(v)
 elif isinstance(n,list):
  for v in n:repin_permission(v)
repin_permission(permissions)
if json.dumps(permissions,ensure_ascii=False)!=prior:write(p,permissions);updated.append(p)
template=json.loads(read(D+'tasks/foundation/POL-T13.json'))
task={'schemaVersion':'ird-task/1','taskId':'DIAG-T12','unit':'diagnostics','masterWp':'WP-D','title':'diagnostics专项评审整改：权威、安全投影、确认通知、目录和日志生命周期','status':'ready','branch':'codex/core-review-fixes','dependsOn':[],
'requirements':['ERR-01','NFR-SEC-07','NFR-REL-05','NFR-MNT-03'],
'designRefs':['units/diagnostics.md#4','units/diagnostics.md#6','units/diagnostics.md#7','units/diagnostics.md#9','reuse/policy.md#1'],
'allowedFiles':products+updated+[TASK,MANIFEST,D+'units/diagnostics.md',TRACE+'**'],
'forbiddenFiles':['RobWork/RobWork/src/**','RobWork/RobWorkSim/src/**','vcpkg/**'],
'outputs':['已确认缺陷的局部修复、正式回归及原始证据；D10在途I/O硬时限与契约冲突保留；实施待所有者独验'],
'verify':['cmake --build build --config Release --target sdurws_ird_diagnostics_test sdurws_ird_diagnostics_contract_test','cmake --build build --config Release --target ird_gates','pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile '+TASK],
'interUnit':True,'knownPitfalls':['P-DIAG-1'],
'acceptance':['按实施表逐根因核对；不改工程判定、确认绑定、去重键、轮转边界、owner或预算','真实UI装配原TU及文件、正式生产码供体回归；stub不冒充真实project/worker接线','passed/skipped/failed分列；全局既有阻断如实报告，不自签独验、不关闭旧任务'],
'note':'所有者指定C:工作树并要求按照评审建议修改，起点'+BASE+'。最小任务文件只供现有H6凭证/范围检查，ready不代表独验通过；不改旧任务状态。D10同步在途I/O取消接缝候选未获精确公共方案批准，不实施。',
'governanceAcceptance':template['governanceAcceptance'],'reuseGateAcceptance':template['reuseGateAcceptance'],'reuse':{'policy':'H1-H6','manifest':MANIFEST}}
write(TASK,task)
write(MANIFEST,{'schemaVersion':'ird-reuse-change/1','taskId':'DIAG-T12','taskFile':TASK,'files':[{'path':p,'sha256':hashes[p],'mode':'business','capabilities':[],'boundary':'DIAG-T12：沿唯一注册/目录/确认/两级安全日志设施修复；标准库和Windows CRT薄适配，无新核心/上游修改/权限或owner扩大；根因、保留与回滚见实施表','verify':TRACE+'IMPLEMENTATION.md'} for p in products]})
print(json.dumps({'products':products,'updated':updated},ensure_ascii=False,indent=2))
