"""验收者独立负实验复现：python independent-negatives.py <冻结测试树> <新的输出目录>。
仅临时修改并原字节恢复测试树；使用临时Git索引构造提交，不移动HEAD。
"""
import pathlib, subprocess, json, os, tempfile, sys
root=pathlib.Path(sys.argv[1]).resolve(); out=pathlib.Path(sys.argv[2]).resolve(); out.mkdir(parents=True,exist_ok=False)
ps='C:/Users/zgl18/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe'
val=root/'RobWork/scripts/industrialrobot/validate-reuse.ps1'; product='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
assert subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD']).decode().strip()=='6eab38df81473b3d5248464f3f06d26b6f062fbc'
def run(name,args,expected):
 r=subprocess.run([ps,'-NoProfile','-File',str(val),'-RepoRoot',str(root),*args],capture_output=True)
 text=(r.stdout+r.stderr).decode('utf-8',errors='replace'); (out/(name+'.log')).write_text(text+'\nexit='+str(r.returncode),encoding='utf-8')
 assert r.returncode!=0 and expected in text,(name,text)
p=root/product/'kinematics/src/AcceptanceUnknown.frob'; assert not p.exists(); p.write_text('// unknown source\n')
try:run('unknown-worktree',[],'REUSE-UNDECLARED')
finally:p.unlink()
fd,index=tempfile.mkstemp(prefix='acc-index-'); os.close(fd); os.unlink(index); env=os.environ.copy(); env['GIT_INDEX_FILE']=index
try:
 for suffix in ['mjs','opaque']:
  subprocess.run(['git','-C',str(root),'read-tree','6eab38df'],env=env,check=True)
  blob=subprocess.check_output(['git','-C',str(root),'hash-object','-w','--stdin'],input=b'// committed undeclared input\n').decode().strip()
  subprocess.run(['git','-C',str(root),'update-index','--add','--cacheinfo',f'100644,{blob},{product}kinematics/src/AcceptanceCommitted.{suffix}'],env=env,check=True)
  tree=subprocess.check_output(['git','-C',str(root),'write-tree'],env=env).decode().strip()
  commit=subprocess.check_output(['git','-C',str(root),'commit-tree',tree,'-p','6eab38df','-m','isolated acceptance negative object']).decode().strip()
  run('committed-'+suffix,['-Head',commit],'REUSE-UNDECLARED')
finally:
 if os.path.exists(index):os.unlink(index)
p=root/'RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json'; raw=p.read_bytes(); data=json.loads(raw); data['status']='ready'; data['reuse']={'policy':'H1-H6','manifest':'no-manifest'}; p.write_text(json.dumps(data),encoding='utf-8')
try:run('stage-ready',[],'REUSE-STAGE')
finally:p.write_bytes(raw)
run('final-debt',['-RequireMigrationComplete'],'REUSE-MIGRATION')
print('5项独立负实验均拒绝')
