import subprocess,json,pathlib,datetime
out=pathlib.Path('build/re-audit-20261021-b7abcba9')
def git(*args):
 p=subprocess.run(['git',*args],capture_output=True,text=True,encoding='utf-8');return {'exit':p.returncode,'stdout':p.stdout.strip(),'stderr':p.stderr.strip()}
scopes=['runtime','requirements','modeling','ui','optimization']
base='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
data={'time':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':git('rev-parse','HEAD'),'branch':git('branch','--show-current'),'initialStatus':git('status','--short'),'originTrackingRef':git('rev-parse','origin/redesign-main'),'frozenMain':git('rev-parse','main'),'t27':git('rev-parse','648c9509'),'t27InMain':git('merge-base','--is-ancestor','648c9509','HEAD'),'productScopeDiff':{s:git('diff','--name-only','5fd9a3d1','HEAD','--',base+s) for s in scopes},'productChangedSincePrevious':git('diff','--name-only','5fd9a3d1','HEAD','--',base),'onlineLatestConfirmed':False,'remoteFailure':'fetch GitHub direct failed to connect port 443 after 21144 ms; retained tracking ref only','excludedWorktree':'486b has uncommitted runtime and ui collision work; not built or included'}
(out/'baseline.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
