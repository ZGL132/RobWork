import pathlib,json,sys
sys.stdout.reconfigure(encoding='utf-8')
repo=pathlib.Path(__file__).parent/'repo';d=repo/'RobWork/doc/industrial-robot-design'
i=json.loads((d/'reuse/inventory.json').read_text(encoding='utf-8-sig'))
t=json.loads((d/'tasks/REUSE-T01.json').read_text(encoding='utf-8-sig'))
print('ACCEPTANCE',json.dumps(t.get('acceptance'),ensure_ascii=False,indent=2));print('state',t.get('implementationState'))
for f in i['files']:
 if f['unit']=='foundation-governance':print(f['role'],f['path'])
idx=json.loads((pathlib.Path(__file__).parent/'inventory-analysis.json').read_text(encoding='utf-8'))['sourceReferenceIndex']
for k,v in idx.items():
 if k.endswith('JsonLite.cpp') or k.endswith('HostView3DPreviewBackend.cpp') or '/test/' in k:print('MAPPED',k,json.dumps(v,ensure_ascii=False))
