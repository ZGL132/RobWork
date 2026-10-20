"""本轮复验清单：保留原功能与方法，关联本轮真实用例；匹配不等于整项通过。"""
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
DOC = ROOT / 'RobWork/doc/industrial-robot-design'


def rows(path):
    return [[c.strip() for c in line.strip().strip('|').split('|')]
            for line in path.read_text(encoding='utf-8-sig').splitlines()
            if line.startswith('|')]


def main():
    requirements = []
    for r in rows(DOC / 'traceability/builds/requirements-audit-20261018/FUNCTIONAL-MATRIX.md'):
        if not re.match(r'^(R\d\d-\d\d|U\d\d|J-\d\d)\b', r[0]):
            continue
        requirements.append(dict(id=r[0].split()[0], source=r[0], feature=r[1], method=r[2],
                                 historical_tests=re.findall(r'`([^`]+)`', r[4])))
    modeling = []
    for r in rows(DOC / 'traceability/builds/modeling-audit-20261019-601ffa0e/MATRIX.md'):
        if not re.fullmatch(r'[MHV]\d\d', r[0]):
            continue
        modeling.append(dict(id=r[0], source=r[1], feature=r[2], method=r[3], keywords=r[4]))
    assert len(requirements) == 120, len(requirements)
    assert len(modeling) == 47, len(modeling)
    results = []
    for path in sorted(OUT.glob('*.xml')):
        tree = ET.parse(path)
        cases = []
        for c in tree.iter('testcase'):
            status = 'FAIL' if c.find('failure') is not None else 'SKIP' if c.find('skipped') is not None else 'PASS'
            cases.append(dict(name=c.get('classname', '')+'.'+c.get('name', ''), status=status,
                              reason=' '.join(''.join(x.itertext()) for x in list(c) if x.tag in ('failure','skipped'))))
        results.append(dict(suite=path.stem, total=len(cases), passed=sum(c['status']=='PASS' for c in cases),
                            failed=sum(c['status']=='FAIL' for c in cases), skipped=sum(c['status']=='SKIP' for c in cases), cases=cases))
    names = {c['name']: [] for s in results for c in s['cases']}
    for s in results:
        for c in s['cases']:
            names[c['name']].append(dict(suite=s['suite'], status=c['status']))
    for row in requirements:
        row['current_exact_test_matches'] = {n:names[n] for n in row['historical_tests'] if n in names}
        row['warning'] = '这些仅是相同测试名本轮结果，不能证明该功能完整交互或视觉验收。'
    payload = dict(baseline='5fd9a3d192ea1f67484bed7aec089ec10719c51a', requirements=requirements,
                   modeling=modeling, executions=results)
    (OUT / 'inventory.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(requirements=len(requirements),modeling=len(modeling),
                          executions=[{k:v for k,v in s.items() if k!='cases'} for s in results]), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
