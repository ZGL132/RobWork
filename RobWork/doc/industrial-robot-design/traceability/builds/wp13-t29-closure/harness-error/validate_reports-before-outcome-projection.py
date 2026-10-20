"""核对真实gtest XML与原生报告，零用例及集合/状态/计数失配均拒绝。"""
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

base = pathlib.Path(__file__).resolve().parent
summary = []
for name in sys.argv[1:]:
    xml = ET.parse(base / (name + '.xml')).getroot()
    native = json.loads((base / (name + '-report.json')).read_text(encoding='utf-8-sig'))
    cases = xml.findall('.//testcase')
    if not cases:
        raise SystemExit('FAIL: zero cases: ' + name)
    expected = {}
    for case in cases:
        key = case.attrib['classname'] + '.' + case.attrib['name']
        state = 'failed' if case.find('failure') is not None else 'skipped' if case.find('skipped') is not None else 'passed'
        expected[key] = state
    actual = {case['testId']: case['outcome'] for case in native['tests']}
    assert len(cases) == len(expected) == len(native['tests']), name + ': duplicate/missing cases'
    assert actual == expected, name + ': case ID/status mismatch: ' + str(set(actual.items()) ^ set(expected.items()))
    counts = dict(total=len(cases), passed=sum(v == 'passed' for v in expected.values()),
                  failed=sum(v == 'failed' for v in expected.values()), skipped=sum(v == 'skipped' for v in expected.values()))
    assert all(native['summary'][k] == v for k, v in counts.items()), name + ': summary mismatch'
    summary.append(dict(name=name, **counts, nativeMatchesXml=True))
result = dict(suites=summary, totals={k: sum(row[k] for row in summary) for k in ('total', 'passed', 'failed', 'skipped')})
filename = 'verified-test-summary-standalone.json' if all(n.startswith('standalone_') for n in sys.argv[1:]) else 'verified-test-summary.json'
(base / filename).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
