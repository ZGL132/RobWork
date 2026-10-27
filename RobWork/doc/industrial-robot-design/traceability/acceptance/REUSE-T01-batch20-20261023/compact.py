import pathlib,re,json
r=pathlib.Path(__file__).parent;repo=r/'repo-isolated';defs=json.loads((repo/'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/batch20-definitions.json').read_text(encoding='utf-8-sig'));parts=[]
for d in defs:
 s=d['source'];lines=(repo/s['path']).read_text(encoding='utf-8-sig').splitlines(); text='\n'.join(lines[s['startLine']-1:s['endLine']]); text=re.sub(r'/\*.*?\*/','',text,flags=re.S);text=re.sub(r'(?m)^\s*//.*$','',text);text=re.sub(r'\n\s*\n','\n',text);parts.append('=== '+d['functionId']+' ===\n'+text)
(r/'all47-compact-code.txt').write_text('\n'.join(parts),encoding='utf-8')
