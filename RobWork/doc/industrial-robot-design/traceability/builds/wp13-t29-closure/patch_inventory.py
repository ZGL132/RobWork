"""补丁等价盘点：正号只表示无完全等价补丁，不能直接判断功能缺失。"""
import json
from repository_inventory import branches, git, out

cache = {}
rows = []
for record in branches:
    if record['containedInMain'] or record['ref'].endswith('/main'):
        continue
    sha = record['sha']
    if sha not in cache:
        lines = git('cherry', 'redesign-main', sha).splitlines()
        plus = [line.split()[1] for line in lines if line.startswith('+ ')]
        product = []
        for commit in plus:
            paths = git('diff-tree', '--no-commit-id', '--name-only', '-r', commit).splitlines()
            code = [p for p in paths if p.startswith('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/')]
            if code:
                product.append(dict(commit=commit, subject=git('show', '-s', '--format=%s', commit), paths=code))
        cache[sha] = dict(nonEquivalent=len(plus), equivalent=len(lines)-len(plus), productCommits=product)
    rows.append(dict(ref=record['ref'], sha=sha, **cache[sha]))
(out/'patch-inventory.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps(dict(nonAncestorRefs=len(rows), uniqueHeads=len(cache),
    productCandidateHeads=sum(bool(row['productCommits']) for row in cache.values())), ensure_ascii=False))
