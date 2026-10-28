# 复刻 reuse-contract.ps1 CheckFunctions 的行覆盖口径，计算提交态缺口：
# changed = git diff --name-only <baseline>..<tip> -- 产品树；
# covered = 所有 reuse/functions/*.json 的 sources 区段（按 path 汇总行号）；
# 缺口 = 变更文件中非空且未被任何区段覆盖的行。
import json, subprocess
from pathlib import Path

root = Path.cwd()
baseline = 'b12e8503cc1c2a1acc040f506842a02e126b9515'
tip = '59af1f9b449ee23fb78dfe547b3a7339df9d325d'
product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'

def git(*args):
    return subprocess.check_output(['git', *args], cwd=root).decode('utf-8', 'replace').splitlines()

changed = git('diff', '--name-only', baseline, tip, '--', product)
# 排除治理引导两个文件（functionalChanges 口径）
governance_bootstrap = {product + 'CMakeLists.txt', product + 'cmake/ird_gates.cmake'}

covered = {}
file_functions = {}
for jf in sorted((root / 'RobWork/doc/industrial-robot-design/reuse/functions').glob('*.json')):
    j = json.loads(jf.read_text(encoding='utf-8-sig'))
    for f in j.get('functions', []):
        for s in f.get('sources', []):
            p = s.get('path')
            if not p:
                continue
            covered.setdefault(p, set())
            file_functions.setdefault(p, []).append((jf.name, f.get('functionId'), f.get('decision'), f.get('status'), s.get('startLine'), s.get('endLine')))
            for n in range(int(s['startLine']), int(s['endLine']) + 1):
                covered[p].add(n)

print('changed product files:', len(changed))
for p in sorted(changed):
    if p in governance_bootstrap:
        continue
    fp = root / p
    if not fp.exists():
        print('DELETED', p)
        continue
    lines = fp.read_text(encoding='utf-8-sig').replace('\r\n', '\n').split('\n')
    cov = covered.get(p, set())
    gaps = [n for n, line in enumerate(lines, 1) if line.strip() and n not in cov]
    rows = file_functions.get(p, [])
    pending = [r for r in rows if r[3] not in ('migrated', 'closed')]
    if gaps or not rows:
        print(f'== {p}: lines={len(lines)} covered={len(cov & set(range(1, len(lines)+1)))} gaps={len(gaps)} rows={len(rows)} unresolved={len(pending)}')
        # 连续段摘要
        runs = []
        for n in gaps:
            if runs and n == runs[-1][1] + 1:
                runs[-1][1] = n
            else:
                runs.append([n, n])
        for a, b in runs:
            print(f'   L{a}-{b}: ' + ' | '.join(lines[a-1].strip()[:60] for _ in [0]) + ('' if a == b else ' ... ' + lines[b-1].strip()[:60]))
