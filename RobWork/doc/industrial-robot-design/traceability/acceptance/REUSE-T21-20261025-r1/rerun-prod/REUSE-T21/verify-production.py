"""复用R3真实Python语料，独立Fraction核真实生产C4，保留旧可执行文件作不变路径对照。

argv: 新生产探针 原R3探针（仍链接旧生产）；工作目录为仓库根。
不执行R3的历史结果写回段，不覆盖旧结果或重新手写语料生成器。
"""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import math
from fractions import Fraction

ROOT = Path.cwd()
HERE = Path(__file__).resolve().parent
prior = HERE.parent/'REUSE-T14/revision-r3/verify.py'
tree = ast.parse(prior.read_text(encoding='utf-8-sig'), filename=str(prior))
stop = next(i for i, node in enumerate(tree.body)
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Tuple)
            and [x.id for x in node.targets[0].elts] == ['r3', 'r2'])
scope = {'__file__': str(prior)}
exec(compile(ast.Module(body=tree.body[:stop], type_ignores=[]), str(prior), 'exec'), scope)
cases, protocol = scope['cases'], scope['protocol']
def run(executable):
    p = subprocess.run([str(Path(executable).resolve())], input=protocol,
                       text=True, capture_output=True)
    assert p.returncode == 0, (p.returncode, p.stderr)
    rows = [tuple(map(int, line.split())) for line in p.stdout.splitlines()]
    assert len(rows) == len(cases)
    return rows, p.stderr
actual, allocation = run(sys.argv[1])
baseline, _ = run(sys.argv[2])
counts = dict(cases=len(cases), overflow=0, fixed=0, overflow_errors=0,
              unchanged_path_errors=0, normal_rounding_differences=0)
failures = []
for c, (current,), (old, _, _) in zip(cases, actual, baseline):
    v, r, rel, a = c
    valid = all(map(math.isfinite, c)) and rel >= 0 and a >= 0
    overflow = valid and math.isinf(v-r) and math.isinf(rel*abs(r)+a)
    if overflow:
        counts['overflow'] += 1
        expected = int(abs(Fraction(v)-Fraction(r)) <= Fraction(rel)*abs(Fraction(r))+Fraction(a))
        counts['fixed'] += old != expected and current == expected
        counts['overflow_errors'] += current != expected
        if current != expected:
            failures.append(dict(input=[x.hex() for x in c],expected=expected,actual=current))
    else:
        counts['unchanged_path_errors'] += old != current
        if valid:
            counts['normal_rounding_differences'] += current != int(
                abs(Fraction(v)-Fraction(r)) <= Fraction(rel)*abs(Fraction(r))+Fraction(a))
        if old != current:
            failures.append(dict(input=[x.hex() for x in c],old=old,actual=current))
paths=[prior,HERE/'production-probe.cpp',Path(__file__),
       ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Compare.cpp',
       ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/test/CompareTest.cpp',
       ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/CMakeLists.txt',
       ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Compare.hpp',
       ROOT/'vcpkg/installed/x64-windows/include/boost/multiprecision/cpp_bin_float.hpp',
       ROOT/'vcpkg/installed/x64-windows/include/boost/multiprecision/cpp_int/multiply.hpp']
result=dict(base='2136a89d5cd65e7183c734956df68291862e0fc0',counts=counts,
            failures=failures,production_allocation_log=allocation,
            protocol_sha256=hashlib.sha256(protocol.encode()).hexdigest(),
            independent_acceptance='NOT RUN',
            executables=[dict(path=str(Path(x).resolve()),raw_sha256=hashlib.sha256(Path(x).read_bytes()).hexdigest()) for x in sys.argv[1:3]],
            inputs=[dict(path=p.relative_to(ROOT).as_posix(),raw_sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths])
ok=not failures and counts['fixed']>0 and allocation.strip()=='production-new-calls=0'
result['verdict']='PASS' if ok else 'FAIL'
(HERE/'production-oracle.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:result[k] for k in ['verdict','counts','production_allocation_log']},indent=2))
sys.exit(0 if ok else 1)
