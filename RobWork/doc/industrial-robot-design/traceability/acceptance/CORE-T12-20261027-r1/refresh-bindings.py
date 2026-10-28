# CORE-T12 独立验收：收口编辑（units/core.md、core-review-r1/README.md）后
# 只刷新 reuse/functions/*.json 中对这两个文件的实际绑定摘要（digest-only 节点），
# 不触碰源码区段（带 startLine）钉扎、不改任务/changes 凭证。口径与实施批
# sync-bindings.py 一致：BOM 剥离、CRLF→LF、去尾换行再补一个 '\n' 后 sha256。
import json, hashlib
from pathlib import Path

root = Path.cwd()
d = 'RobWork/doc/industrial-robot-design/'
refreshed = [
    d + 'units/core.md',
    d + 'traceability/core-review-r1/README.md',
]

def digest_of(rel):
    text = (root / rel).read_text(encoding='utf-8-sig')
    return hashlib.sha256((text.replace('\r\n', '\n').rstrip('\n') + '\n').encode()).hexdigest()

def write(rel, obj, had_crlf):
    f = root / rel
    text = json.dumps(obj, ensure_ascii=False, indent=2) + '\n'
    if had_crlf:
        text = text.replace('\n', '\r\n')
    f.write_bytes(text.encode())

new_hash = {p: digest_of(p) for p in refreshed}
counts = {}
for jf in sorted((root / (d + 'reuse/functions')).glob('*.json')):
    raw = (jf).read_bytes()
    had_crlf = b'\r\n' in raw
    j = json.loads(raw.decode('utf-8-sig'))
    n = 0
    def visit(node):
        global n
        if isinstance(node, dict):
            if node.get('path') in new_hash and 'sha256' in node and 'startLine' not in node:
                if node['sha256'] != new_hash[node['path']]:
                    node['sha256'] = new_hash[node['path']]
                    n += 1
            for v in node.values():
                visit(v)
        elif isinstance(node, list):
            for v in node:
                visit(v)
    visit(j)
    if n:
        rel = str(jf.relative_to(root)).replace('\\', '/')
        write(rel, j, had_crlf)
        counts[rel] = n
print('acceptance binding refresh:', counts)
