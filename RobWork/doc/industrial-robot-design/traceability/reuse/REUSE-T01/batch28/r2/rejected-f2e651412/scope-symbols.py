import pathlib,json,subprocess,hashlib
root=pathlib.Path(__file__).parent; repo=root/'clone'; d='RobWork/doc/industrial-robot-design/'
def git(*args):return subprocess.check_output(['git','-C',str(repo),*args])
head='f2e65141259cd43ee5e63799514a733908440355';base='f6d3de37ddbc8375a334b456b058e3d548141484'
paths=git('diff','--name-only',base,head).decode().splitlines()
rows=json.loads((root/'added-rows.json').read_text(encoding='utf8'))
q=[{'functionId':r['functionId'],'qualifiedSymbol':r['sources'][0]['qualifiedSymbol'],'source':r['sources'][0]['path'],'startLine':r['sources'][0]['startLine']} for r in rows]
checks={'changedPathCount':len(paths),'changedPaths':paths,'productionUpstreamVcpkgChanged':[p for p in paths if p.startswith(('RobWork/RobWorkStudio/src/','RobWork/RobWork/src/','RobWork/RobWorkSim/src/','vcpkg/'))],'qualifiedSymbolsWithDot':[r for r in q if '.' in r['qualifiedSymbol']],'detailNamespaceMissing':[r for r in q if r['functionId'].split('.')[-1] in ('tryParseId128','formatId128','fnv1a128','generateId128Bytes') and '::detail::' not in r['qualifiedSymbol']],'permissionRefs':list(set(r['permissionRef'] for r in rows)),'artifacts':[]}
for rel in ('build-prefix/Release/ird_reuse_audit_probe.exe','build-prefix/ird-core/Release/sdurws_ird_core_test.exe','build-prefix/ird-core/Release/sdurws_ird_core.lib'):
 p=root/rel;b=p.read_bytes();checks['artifacts'].append({'path':rel,'bytes':len(b),'rawSha256':hashlib.sha256(b).hexdigest()})
(root/'scope-and-symbols.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'changedPaths':len(paths),'productionChanges':checks['productionUpstreamVcpkgChanged'],'dotQualifiedSymbolCount':len(checks['qualifiedSymbolsWithDot']),'missingDetailNamespace':len(checks['detailNamespaceMissing']),'permissionRefs':checks['permissionRefs']},ensure_ascii=False))
