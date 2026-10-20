"""提交前核对本轮暂存路径；提交后再运行正式verify-task-scope冻结范围。"""
from pathlib import Path
import json,re,subprocess
task=json.loads(Path('RobWork/doc/industrial-robot-design/tasks/foundation/WP-13-T24.json').read_text(encoding='utf-8-sig'))
def matches(path,pattern):
    # 与任务范围门禁相同：**跨层，*单层；禁止子串放宽。
    regex=re.escape(pattern).replace(r'\*\*','.*').replace(r'\*','[^/]*')
    return re.fullmatch(regex,path,re.I) is not None
paths=subprocess.check_output(['git','diff','--cached','--name-only','-z']).decode().split('\0')
paths=[p for p in paths if p]
assert paths
bad=[p for p in paths if any(matches(p,x) for x in task['forbiddenFiles'])
     or not any(matches(p,x) for x in task['allowedFiles'])]
assert not bad,bad
print(f'STAGED SCOPE PASS: {len(paths)} paths within WP-13-T24; no forbidden paths')
