# PRJ-T19 收口：任务 ready→done，验收记录 admissionClosure 同步。
import json
from pathlib import Path

NOTE_ADD = ('2026-10-28所有者指示收口：任务done。依据＝实施与回归面独验PASS（PRJ-T19-20261028-r1）'
            '＋准入结案批dad436e3f解决本任务全部REUSE-COVERAGE缺口且project单元过真实门禁-Head检查；'
            '树级门禁剩余runtime CMake生产证据冻结失败为POL-T13批（a167f4d4d）登记遗留，'
            '非本任务准入缺口，其解决不回退本收口。')

p = Path('RobWork/doc/industrial-robot-design/tasks/foundation/PRJ-T19.json')
raw = p.read_bytes()
had_crlf = b'\r\n' in raw
j = json.loads(raw.decode('utf-8-sig'))
j['status'] = 'done'
if not j['note'].endswith(NOTE_ADD):
    j['note'] = j['note'].rstrip() + NOTE_ADD
text = json.dumps(j, ensure_ascii=False, indent=2) + '\n'
if had_crlf:
    text = text.replace('\n', '\r\n')
p.write_bytes(text.encode('utf-8'))
print('task -> done')

a = Path('RobWork/doc/industrial-robot-design/traceability/acceptance/PRJ-T19-20261028-r1.json')
raw = a.read_bytes()
had = b'\r\n' in raw
j2 = json.loads(raw.decode('utf-8-sig'))
j2['admissionClosure']['status'] = 'closed-owner-directed'
tail = '。2026-10-28所有者指示收口：任务done；树级剩余失败为POL-T13批登记遗留，不构成本任务缺口。'
if tail not in j2['admissionClosure']['detail']:
    j2['admissionClosure']['detail'] = j2['admissionClosure']['detail'].rstrip('。') + tail
text = json.dumps(j2, ensure_ascii=False, indent=2) + '\n'
if had:
    text = text.replace('\n', '\r\n')
a.write_bytes(text.encode('utf-8'))
print('acceptance record -> closed-owner-directed')
