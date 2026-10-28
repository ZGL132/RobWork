"""只复制本轮证据；不改写原件、不复制二进制构建树、不清理目录。"""
import hashlib
import json
import re
import shutil
import io
import subprocess
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

repo = Path.cwd().resolve()
build = repo / 'build/diagnostics-fix-20261028'
review = repo / 'build/diagnostics-review-20261028'
dest = repo / 'RobWork/doc/industrial-robot-design/traceability/diagnostics-review-r1/evidence'
assert repo.as_posix() == 'C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork'
assert dest.is_relative_to(repo) and build.is_relative_to(repo) and review.is_relative_to(repo)
dest.mkdir(parents=True, exist_ok=True)

raw = {}

def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if target.read_bytes() != source.read_bytes():
            raise RuntimeError('refuse to overwrite evidence: ' + str(target))
    else:
        shutil.copyfile(source, target)

for source in sorted(build.iterdir()):
    if source.is_file() and source.suffix in ('.log', '.xml', '.json') and source.name != 'evidence-copy.log':
        raw[source.name] = source
for name in ('audit-bindings.py', 'sync-bindings.py', 'assemble-evidence.py'):
    copy(build / name, dest / 'tools' / name)
for source in sorted(review.iterdir()):
    if source.is_file() and (source.suffix in ('.log', '.xml', '.json') or source.name in (
        'review.md', 'coverage.md', 'probe.cpp', 'CMakeLists.txt', 'commands.ps1', 'translation-cause.bin')):
        raw['review/' + source.name] = source
        if source.name in ('review.md', 'coverage.md'):
            copy(source, dest / 'review' / source.name)
for name in ('probe-real-assembly', 'probe-crash-collision'):
    for source in sorted((review / name).iterdir()):
        if source.is_file():
            raw['review/' + name + '/' + source.name] = source

# ZIP 保留 ACP/UTF-8 混合的失败原件、CRLF 和尾随空格，不改 Git 属性或原始日志。
buffer = io.BytesIO()
with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as z:
    for name, source in sorted(raw.items()):
        info = zipfile.ZipInfo(name, date_time=(2026, 10, 28, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, source.read_bytes())
archive = dest / 'raw-evidence.zip'
archive.write_bytes(buffer.getvalue())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert all(z.read(name) == source.read_bytes() for name, source in raw.items())

# 只移除本脚本刚生成且逐字节等于原件的旧松散副本；不递归删除，不动 build 原件。
obsolete = []
for name, source in raw.items():
    target = (dest / name).resolve()
    assert target.is_relative_to(dest.resolve())
    if name not in ('review/review.md', 'review/coverage.md') and target.is_file():
        assert target.read_bytes() == source.read_bytes(), str(target)
        obsolete.append(target)
empty_old_copy = dest / 'evidence-copy.log'
if empty_old_copy.is_file():
    assert empty_old_copy.stat().st_size == 0
    obsolete.append(empty_old_copy)
if obsolete:
    subprocess.run(['git', 'rm', '--cached', '--quiet', '--ignore-unmatch', '--'] + [p.relative_to(repo).as_posix() for p in obsolete], check=True)
    for target in obsolete:
        target.unlink()

checks = []
for name in ('red', 'red-lifecycle', 'green-1', 'diagnostics', 'contract', 'assembly', 'assembly-final',
             'policy', 'smoke-unit', 'smoke-contract', 'assertions', 'smoke-assertions'):
    report = json.loads((build / (name + '-report.json')).read_text(encoding='utf-8-sig'))['summary']
    xml_status = 'PASS'
    try:
        xml = ET.parse(build / (name + '.xml')).getroot()
        cases = xml.findall('.//testcase')
        total, skipped, failed = len(cases), sum(c.find('skipped') is not None for c in cases), sum(c.find('failure') is not None for c in cases)
    except ET.ParseError:
        # 首轮真实文件异常 what() 含 Windows ACP 字节，原 XML 声明 UTF-8 却不可解析。
        # 不改写失败原件；仅核其 ASCII 根统计，并与独立 per-exe JSON 对照。
        assert name == 'red', name
        header = re.search(rb'<testsuites ([^>]+)>', (build / (name + '.xml')).read_bytes()).group(1)
        attrs = dict(re.findall(rb'(\w+)="([0-9]+)"', header))
        total, failed, skipped = int(attrs[b'tests']), int(attrs[b'failures']), int(attrs.get(b'skipped', b'0'))
        xml_status = 'FAIL: raw legacy Windows exception bytes; root ASCII counts crosschecked only'
    actual = dict(total=total, passed=total-skipped-failed, skipped=skipped, failed=failed)
    assert all(report[k] == v for k, v in actual.items()), (name, report, actual)
    checks.append({'name': name, 'status': 'FAIL' if failed else 'PASS', **actual, 'xmlParseStatus': xml_status})

sources = {}
for line in subprocess.check_output(['git', 'diff', '--name-only', 'af375242c7b632d572c8190ee0d954aa149d80dc']).decode().splitlines():
    p = repo / line
    if p.is_file() and 'traceability/diagnostics-review-r1/' not in line:
        sources[line] = hashlib.sha256(p.read_bytes()).hexdigest()
manifest = json.loads((repo / 'RobWork/doc/industrial-robot-design/reuse/changes/DIAG-T12.json').read_text(encoding='utf-8'))
for row in manifest['files']:
    sources[row['path']] = hashlib.sha256((repo / row['path']).read_bytes()).hexdigest()

result = {'taskId': 'DIAG-T12', 'base': 'af375242c7b632d572c8190ee0d954aa149d80dc',
          'boundary': 'files 为 raw-evidence.zip 内部原始证据，逐字节核对且无 Git 换行转换；review 为修复前归档，原链接继续指向保留的 build 原件。源码哈希为打包时实际工作区原始字节，不冒充已提交 SHA。',
          'archive': {'path': 'raw-evidence.zip', 'bytes': archive.stat().st_size, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()},
          'tests': checks, 'workingSourceSha256': sources, 'files': []}
for name, source in sorted(raw.items()):
    data = source.read_bytes()
    result['files'].append({'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
(dest / 'evidence-index.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'tests': checks, 'evidenceFiles': len(result['files']), 'bytes': sum(r['bytes'] for r in result['files'])}, ensure_ascii=False, indent=2))
