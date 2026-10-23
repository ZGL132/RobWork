import pathlib,json,subprocess,hashlib,sys
root=pathlib.Path(sys.argv[1]); repo=root/'clone'; H='a02785a10eada414533d9789eedac3f5af654a71'; B='52285db6f0d276742531ecb7e7bcf73db283e563'; O='933dc6016c078b7d674fea68af19c038e0388979'; D='RobWork/doc/industrial-robot-design'; P=D+'/traceability/reuse/REUSE-T01/batch31'
def git(*a):return subprocess.check_output(['git','-c','core.longpaths=true','-C',str(repo),*a])
def blob(p,rev=H):return git('show',rev+':'+p)
def j(p,rev=H):return json.loads(blob(p,rev))
r=json.loads(blob(D+'/reuse/functions/core.json'))['functions']; old={x['functionId']:x for x in j(D+'/reuse/functions/core.json',B)['functions']}; changed=[x for x in r if x!=old.get(x['functionId'])]
print('changed rows',len(changed));print(json.dumps(changed[0],ensure_ascii=False,indent=2));print(git('diff',B,H,'--',D+'/reuse/catalog.json').decode());print('rejected manifest');print(blob(P+'/r2/rejected-933dc6016/safe-manifest.json')[:1000].decode());print('r2 manifest tail');print(blob(P+'/r2/artifact-manifest.json')[-500:].decode())
