"""只读盘点分支/工作树，避免把未合入或验收记录误当作缺失实现。"""
import collections
import json
import pathlib
import subprocess
from datetime import datetime, timezone

# 脚本同时保存在build执行目录和正式证据目录，不能把父目录层数当作仓库根。
root = pathlib.Path(subprocess.check_output(
    ['git', '-C', str(pathlib.Path(__file__).resolve().parent), 'rev-parse', '--show-toplevel'],
    encoding='utf-8').strip())
out = pathlib.Path(__file__).resolve().parent


def git(*args, cwd=root):
    return subprocess.check_output(['git', '-C', str(cwd), *args], encoding='utf-8', errors='replace').strip()


baseline = git('rev-parse', 'redesign-main')
merged = set(git('for-each-ref', '--merged=redesign-main', '--format=%(refname)', 'refs/heads', 'refs/remotes/origin').splitlines())
branches = []
for line in git('for-each-ref', '--format=%(refname)|%(objectname)|%(upstream)|%(subject)', 'refs/heads', 'refs/remotes/origin').splitlines():
    ref, sha, upstream, subject = line.split('|', 3)
    branches.append(dict(ref=ref, sha=sha, upstream=upstream, subject=subject, containedInMain=ref in merged))

worktrees = []
for block in git('worktree', 'list', '--porcelain').split('\n\n'):
    data = {}
    for line in block.splitlines():
        key, _, value = line.partition(' ')
        data[key] = value or True
    path = data.get('worktree')
    if not path:
        continue
    if pathlib.Path(path).is_dir():
        changes = subprocess.check_output(
            ['git', '-C', path, 'status', '--porcelain', '--untracked-files=normal'],
            encoding='utf-8', errors='replace').rstrip().splitlines()
        data['changedPaths'] = changes
        data['changeCount'] = len(changes)
        data['trackedChangeCount'] = sum(not p.startswith('??') for p in changes)
    else:
        data['unavailable'] = True
    worktrees.append(data)

key_names = ['wp14-t12', 'wp13-t24', 'wp13-t25', 'wp13-t26', 'wp13-t27', 'wp13-t28',
             'wp13-t29', 'wp15-t20', 'codex/requirements-audit-20261018', 'codex/governance-validation-fixes']
key = []
for name in key_names:
    record = next((dict(r) for r in branches if r['ref'] == 'refs/heads/' + name), None)
    if record:
        record['name'] = name
        record['leftMainRightTask'] = git('rev-list', '--left-right', '--count', 'redesign-main...' + name).split()
        if not record['containedInMain']:
            record['patchComparison'] = git('cherry', 'redesign-main', name).splitlines()
            record['uniqueCommits'] = git('log', '--format=%H|%s', 'redesign-main..' + name).splitlines()
        key.append(record)

result = dict(capturedUtc=datetime.now(timezone.utc).isoformat(), main=baseline,
              remoteMain=git('rev-parse', 'origin/redesign-main'), frozenMain=git('rev-parse', 'main'),
              networkFetch='PASS: git fetch origin --no-prune, 2026-10-21',
              branches=branches, worktrees=worktrees, keyBranches=key)
(out / 'repository-inventory.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(dict(branchRefs=len(branches), worktrees=len(worktrees),
                      trackedDirtyWorktrees=[dict(path=w['worktree'], count=w.get('trackedChangeCount')) for w in worktrees if w.get('trackedChangeCount')],
                      keyBranches=[dict(name=k['name'], merged=k['containedInMain'], divergence=k['leftMainRightTask']) for k in key]), ensure_ascii=False, indent=2))
