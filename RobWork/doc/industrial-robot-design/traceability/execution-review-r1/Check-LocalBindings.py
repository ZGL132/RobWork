"""EX-T11 隔离核对：仅检查本批文件/片段摘要和范围，不替代正式复用门禁。"""
from pathlib import Path
import hashlib
import json

ROOT = Path.cwd()
DESIGN = ROOT / 'RobWork/doc/industrial-robot-design'


def digest(text):
    normalized = text.removeprefix('\ufeff').replace('\r\n', '\n').rstrip('\n') + '\n'
    return hashlib.sha256(normalized.encode('utf-8')).hexdigest()


def read(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')


manifest = json.loads((DESIGN / 'reuse/changes/EX-T11.json').read_text(encoding='utf-8-sig'))
products = {row['path']: row for row in manifest['files']}
covered = {path: set() for path in products}
segments = 0
for row in products.values():
    assert row['sha256'] == digest(read(row['path'])), row['path']
for unit in ['execution', 'core', 'ui', 'testkit']:
    matrix = json.loads((DESIGN / f'reuse/functions/{unit}.json').read_text(encoding='utf-8-sig'))
    for function in matrix['functions']:
        for source in function['sources']:
            if source['path'] not in products:
                continue
            lines = read(source['path']).splitlines()
            assert 1 <= source['startLine'] <= source['endLine'] <= len(lines), source
            region = '\n'.join(lines[source['startLine'] - 1:source['endLine']])
            assert source['symbol'] in region, source
            assert digest(region) == source['sha256'], source
            segments += 1
            if function['status'] in ['migrated', 'closed']:
                covered[source['path']].update(range(source['startLine'], source['endLine'] + 1))
        for source in function.get('inputs', []):
            if source['path'] in products:
                assert source['sha256'] == products[source['path']]['sha256'], source
        for source in function.get('auditEvidence', []) + [function.get('nonOverlapEvidence', {})]:
            if source.get('path', '').endswith('execution-review-r1/README.md'):
                assert source['sha256'] == digest(read(source['path'])), source
for path in products:
    missing = [i for i, line in enumerate(read(path).splitlines(), 1)
               if line.strip() and i not in covered[path]]
    assert not missing, (path, missing[:20])
print(f'LOCAL BINDINGS PASS: files={len(products)} sourceSegments={segments}; '
      '仅本批摘要/符号/非空行归属，非全量门禁或独立语义验收')
