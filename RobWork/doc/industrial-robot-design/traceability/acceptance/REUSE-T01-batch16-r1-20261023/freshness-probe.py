import pathlib,json,hashlib
B=pathlib.Path(__file__).parent;R=B/'repo';D=R/'RobWork/doc/industrial-robot-design'
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def norm(t):return t.lstrip('\ufeff').replace('\r\n','\n').rstrip('\n')+'\n'
def h(t):return hashlib.sha256(norm(t).encode()).hexdigest()
cases=[
 ('dynamics.StatisticsTest.expectNearRel','constexpr double kGoldenRelTol = 1e-12;','constexpr double kGoldenRelTol = 1e-6;','改变独立断言容差，从1e-12放宽为1e-6'),
 ('dynamics.StatisticsTest.prismAxialForce','constexpr double kPrismMass = 1.0;','constexpr double kPrismMass = 2.0;','支撑力从(1.0+0.5)*9.81改变为(2.0+0.5)*9.81'),
 ('dynamics.StatisticsTest.revTorque','constexpr double kRevMass = 0.5;','constexpr double kRevMass = 1.5;','回转力矩随参与质量改变'),
 ('dynamics.StatisticsTest.makePrismRevModel','constexpr double kPrismMass = 1.0;','constexpr double kPrismMass = 2.0;','CanonicalModel滑臂质量改变'),
 ('modeling.XacroExpandTest.makeBasicSource','<xacro:property name="prefix" value="arm"/>','<xacro:property name="prefix" value="changed"/>','原始Xacro字节、真实digest以及展开后的关节名称改变'),
]
rows={}
for u in ['dynamics','modeling']:
 for x in load(D/f'reuse/functions/{u}.json')['functions']:rows[x['functionId']]=x
out=[]
for fid,before,after,impact in cases:
 row=rows[fid];source=row['sources'][0];p=R/source['path'];original=p.read_text(encoding='utf-8-sig')
 assert original.count(before)==1,(fid,original.count(before))
 mutant=original.replace(before,after)
 def content(b,t):return '\n'.join(t.splitlines()[b['startLine']-1:b['endLine']]) if 'startLine' in b else t
 evidence=[]
 for kind in ['sources','inputs','auditEvidence']:
  for b in row[kind]:
   t=(R/b['path']).read_text(encoding='utf-8-sig');m=mutant if b['path']==source['path'] else t
   evidence.append({'kind':kind,'path':b['path'],'unchanged':h(content(b,t))==h(content(b,m)),'originalMatchesRegistration':h(content(b,t))==b['sha256']})
 out.append({'functionId':fid,'sourcePath':source['path'],'replacement':{'before':before,'after':after},'behaviorChange':impact,'wholeSourceHashChanges':h(original)!=h(mutant),'allRegisteredHashesUnchanged':all(x['unchanged'] for x in evidence),'allOriginalHashesMatched':all(x['originalMatchesRegistration'] for x in evidence),'registeredBindings':evidence,'execution':'仅内存替换，未写入任何仓库文件，未执行C++或门禁变异运行'})
assert all(x['wholeSourceHashChanges'] and x['allRegisteredHashesUnchanged'] and x['allOriginalHashesMatched'] for x in out)
(B/'freshness-probe.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'probeCases':len(out),'untrackedContextConfirmed':len(out),'productFilesWritten':0},ensure_ascii=False))
