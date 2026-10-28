"""DIAG-T12 本包只读复核；不替代 validate-reuse 或独立语义验收。"""
import hashlib
import json
import subprocess
from pathlib import Path

BASE = 'af375242c7b632d572c8190ee0d954aa149d80dc'
DESIGN = 'RobWork/doc/industrial-robot-design/'
PRODUCT = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
OUT = Path('build/diagnostics-fix-20261028')

def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def digest(text):
    return hashlib.sha256((text.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n').encode()).hexdigest()

def previous(path):
    return json.loads(subprocess.check_output(['git', 'show', BASE + ':' + path]).decode('utf-8-sig'))

task = load(DESIGN + 'tasks/foundation/DIAG-T12.json')
manifest = load(task['reuse']['manifest'])
changed = {row['path'] for row in manifest['files']}
errors = []
covered = {p: set() for p in changed}
added = []
ids = set()
inherited_unresolved = []
checked_sources = checked_inputs = 0
for unit in ('diagnostics', 'execution', 'ui'):
    path = DESIGN + 'reuse/functions/' + unit + '.json'
    matrix = load(path)
    old = {r['functionId']: r for r in previous(path)['functions']}
    for row in matrix['functions']:
        key = (row['functionId'], row['capabilityId'])
        if key in ids:
            errors.append('duplicate id ' + str(key))
        ids.add(key)
        if row['functionId'] in old:
            before = old[row['functionId']]
            for field in ('decision', 'status', 'ownerUnit', 'usageRole', 'permissionRef', 'capabilityId'):
                if before[field] != row[field]:
                    errors.append('changed existing semantics ' + str((key, field)))
        else:
            added.extend(dict(functionId=row['functionId'], **s) for s in row['sources'])
        for source in row['sources']:
            p = source['path']
            if p not in changed:
                continue
            checked_sources += 1
            lines = Path(p).read_text(encoding='utf-8-sig').splitlines()
            a, b = source['startLine'], source['endLine']
            region = '\n'.join(lines[a-1:b])
            if not (1 <= a <= b <= len(lines)) or source['symbol'] not in region or digest(region) != source['sha256']:
                errors.append('stale region ' + str(source))
            if row['status'] not in ('migrated', 'closed') or row['decision'] in ('pending', 'proposal'):
                before = old.get(row['functionId'])
                if not before or before['status'] != row['status'] or before['decision'] != row['decision']:
                    errors.append('new unresolved changed source ' + row['functionId'])
                else:
                    inherited_unresolved.append({'functionId': row['functionId'], 'path': p,
                                                 'decision': row['decision'], 'status': row['status']})
            covered[p].update(range(a, b+1))
        for bound in row.get('inputs', []):
            p = bound['path']
            if p in changed or p == DESIGN + 'units/diagnostics.md':
                checked_inputs += 1
                if digest(Path(p).read_text(encoding='utf-8-sig')) != bound['sha256']:
                    errors.append('stale input ' + str(bound))

coverage = []
for row in manifest['files']:
    p = row['path']
    text = Path(p).read_text(encoding='utf-8-sig')
    if row['sha256'] != digest(text):
        errors.append('stale manifest ' + p)
    missing = [i for i, line in enumerate(text.splitlines(), 1) if line.strip() and i not in covered[p]]
    if missing:
        errors.append('missing coverage ' + str((p, missing)))
    coverage.append({'path': p, 'lines': len(text.splitlines()), 'uncoveredNonemptyLines': missing})

permissions = load(DESIGN + 'reuse/permissions.json')
old_permissions = previous(DESIGN + 'reuse/permissions.json')
def erase_hashes(node):
    if isinstance(node, dict):
        return {k: erase_hashes(v) for k, v in node.items() if k != 'sha256'}
    if isinstance(node, list):
        return [erase_hashes(v) for v in node]
    return node
if erase_hashes(permissions) != erase_hashes(old_permissions):
    errors.append('permissions changed beyond source hashes')

result = {'taskId': 'DIAG-T12', 'base': BASE, 'bindingStatus': 'PASS' if not errors else 'FAIL',
          'admissionStatus': 'FAIL' if inherited_unresolved or errors else 'PASS',
          'boundary': '只核本包绑定、区段归属和既有权限/状态未变；不豁免全量门禁、不代表语义独验',
          'checkedSources': checked_sources, 'checkedInputs': checked_inputs,
          'files': coverage, 'addedSources': added, 'inheritedUnresolved': inherited_unresolved, 'errors': errors}
(OUT / 'binding-audit.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in result.items() if k not in ('files', 'addedSources', 'inheritedUnresolved')}, ensure_ascii=False, indent=2))
print('inherited unresolved rows:', len(inherited_unresolved))
for row in added:
    print(f"{row['path'].split('/')[-1]}:{row['startLine']}-{row['endLine']} {row['symbol']}")
raise SystemExit(bool(errors))
