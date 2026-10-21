"""核对真实gtest XML与原生报告，零用例及集合/状态/计数失配均拒绝。"""
import json
import collections
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
    # testkit §7.2保留六值分类；gtest只有三类，环境缺供在XML表现为skipped。
    # 只为逐例对照投影，原生计数/原因另行完整保留，绝不把缺供折为通过。
    categories = ('passed', 'failed', 'skipped', 'notRun', 'envUnavailable', 'datasetInvalid')
    actual = {}
    for case in native['tests']:
        outcome = case['outcome']
        assert outcome in categories, name + ': unknown outcome: ' + outcome
        if outcome in ('notRun', 'envUnavailable', 'datasetInvalid'):
            assert case['reason'], name + ': inconclusive outcome without reason'
        actual[case['testId']] = outcome if outcome in ('passed', 'failed') else 'skipped'
    assert len(cases) == len(expected) == len(native['tests']), name + ': duplicate/missing cases'
    assert actual == expected, name + ': case ID/status mismatch: ' + str(set(actual.items()) ^ set(expected.items()))
    counts = dict(total=len(cases), passed=sum(v == 'passed' for v in expected.values()),
                  failed=sum(v == 'failed' for v in expected.values()), skipped=sum(v == 'skipped' for v in expected.values()))
    native_counts = dict(collections.Counter(case['outcome'] for case in native['tests']))
    native_counts = dict(total=len(cases), **{k: native_counts.get(k, 0) for k in categories})
    assert all(native['summary'][k] == v for k, v in native_counts.items()), name + ': native summary mismatch'
    decisive = not any(native_counts[k] for k in ('notRun', 'envUnavailable', 'datasetInvalid'))
    assert native['summary']['decisive'] == decisive, name + ': decisive mismatch'
    summary.append(dict(name=name, **counts, nativeCounts=native_counts, decisive=decisive, nativeMatchesXml=True))
result = dict(suites=summary, totals={k: sum(row[k] for row in summary) for k in ('total', 'passed', 'failed', 'skipped')})
result['nativeTotals'] = {k: sum(row['nativeCounts'][k] for row in summary) for k in ('total', *categories)}
filename = 'verified-test-summary-standalone.json' if all(n.startswith('standalone_') for n in sys.argv[1:]) else 'verified-test-summary.json'
(base / filename).write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
