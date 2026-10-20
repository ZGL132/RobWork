"""WP-14-T12：一次性冻结证据，拒绝覆盖已有历史目录，不携带构建二进制。"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

out = Path(__file__).resolve().parent
root = out.parents[1]
dest = root / 'RobWork/doc/industrial-robot-design/traceability/builds/requirements-close-20261019'
assert not dest.exists(), f'历史证据目录已存在，禁止覆盖：{dest}'
product = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'
patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD', '--', product], cwd=root)
(out / 'source-diff.patch').write_bytes(patch)
names = subprocess.check_output(['git', 'diff', '--name-only', 'HEAD', '--', product], cwd=root, text=True).splitlines()
inventory = [{'path': p, 'sha256': hashlib.sha256((root / p).read_bytes()).hexdigest()} for p in names]
(out / 'source-files.json').write_text(json.dumps(inventory, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
allowed = {'.log', '.xml', '.json', '.md', '.txt', '.png', '.ps1', '.py', '.cpp', '.rsp', '.patch'}
files = []
for file in sorted(out.rglob('*')):
    if not file.is_file() or 'install' in file.relative_to(out).parts or file.suffix.lower() not in allowed:
        continue
    target = dest / file.relative_to(out)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(file, target)
    files.append({'path': target.relative_to(dest).as_posix(), 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
(dest / 'snapshot-sha256.json').write_text(json.dumps(files, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(f'Frozen {len(files)} files at {dest}')
