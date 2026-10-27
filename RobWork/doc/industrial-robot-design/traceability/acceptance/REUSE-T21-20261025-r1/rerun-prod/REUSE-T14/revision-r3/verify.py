"""隔离候选验证：复用原 oracle.py 的语料生成段，以真实 CPython/Fraction 对照。

不执行原脚本的探针或写回段，不覆盖历史 oracle-result.json；新增结果只写本目录。
argv: R3可执行文件 R2可执行文件；工作目录必须是仓库根。
"""
import ast
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
from fractions import Fraction

ROOT = Path.cwd()
HERE = Path(__file__).resolve().parent
OLD = HERE.parent / 'oracle.py'

# 仅复用已审计脚本中 bits 定义之前的导入、常量及两个语料生成循环。
# AST 截止边界显式校验，避免 import 原脚本触发历史证据覆盖。
tree = ast.parse(OLD.read_text(encoding='utf-8-sig'), filename=str(OLD))
stop = next(i for i, node in enumerate(tree.body)
            if isinstance(node, ast.FunctionDef) and node.name == 'bits')
scope = {}
exec(compile(ast.Module(body=tree.body[:stop], type_ignores=[]),
             str(OLD), 'exec'), scope)
cases = list(scope['cases'])
assert len(cases) == 13168
reused_cases = len(cases)

# 实际 binary64 位模式覆盖全指数域；期望由 Fraction 精确实数计算，非候选浮点公式。
rng = random.Random(20261024)
def real(bits):
    return struct.unpack('<d', struct.pack('<Q', bits))[0]
for _ in range(4000):
    v, r = [real(rng.getrandbits(64)) for _ in range(2)]
    rel, absolute = [abs(real(rng.getrandbits(64))) for _ in range(2)]
    cases.append((v, r, rel, absolute))

maximum = sys.float_info.max
for _ in range(200):
    v = math.ldexp(rng.uniform(.5, 1), 1024)
    r = -math.ldexp(rng.uniform(.5, 1), 1024)
    threshold = 1 + v / abs(r)
    for rel in (math.nextafter(threshold, 0), threshold,
                math.nextafter(threshold, math.inf)):
        gap = abs(Fraction(v) - Fraction(r)) - Fraction(rel) * abs(Fraction(r))
        absolutes = [0., math.ulp(0.), maximum]
        if 0 < gap < Fraction(maximum):
            a = float(gap)
            absolutes += [math.nextafter(a, 0), a, math.nextafter(a, math.inf)]
        for a in absolutes:
            cases += [(v, r, rel, a), (-v, -r, rel, a)]

historical = json.loads((HERE.parent / 'oracle-result.json').read_text())
cases += [tuple(float.fromhex(x) for x in row['input'])
          for row in historical['mismatches']]
for v in (0., -0., maximum, -maximum, math.inf, -math.inf, math.nan):
    for r in (0., maximum, -maximum, math.inf, math.nan):
        for rel, a in ((0., 0.), (1.5, 0.), (-1., 0.), (math.inf, 0.),
                       (0., math.inf), (math.nan, 0.), (0., -1.)):
            cases.append((v, r, rel, a))

def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]
protocol = ''.join(' '.join(f'{bits(x):016x}' for x in c) + '\n' for c in cases)
probe_stderr = {}
def run(executable):
    process = subprocess.run([str(Path(executable).resolve())], input=protocol,
                             text=True, capture_output=True)
    assert process.returncode == 0, (process.returncode, process.stderr)
    probe_stderr[Path(executable).name] = process.stderr
    lines = process.stdout.splitlines()
    assert len(lines) == len(cases), (len(lines), len(cases))
    return [tuple(map(int, line.split())) for line in lines]
r3, r2 = run(sys.argv[1]), run(sys.argv[2])
counts = dict(cases=len(cases), reused_real_cpython_cases=reused_cases,
              finite_valid=0, overflow=0, production_overflow_errors=0,
              wide_formula_errors=0, r3_overflow_errors=0,
              r3_unchanged_path_errors=0, r2_overflow_errors=0,
              r2_unchanged_path_errors=0, normal_rounding_differences=0)
failures = []
for c, (old, candidate, wide), (prior,) in zip(cases, r3, r2):
    v, r, rel, a = c
    valid = all(map(math.isfinite, c)) and rel >= 0 and a >= 0
    overflow = valid and math.isinf(v-r) and math.isinf(rel*abs(r)+a)
    if valid:
        expected = int(abs(Fraction(v)-Fraction(r))
                       <= Fraction(rel)*abs(Fraction(r))+Fraction(a))
        counts['finite_valid'] += 1
        counts['wide_formula_errors'] += wide != expected
        if overflow:
            counts['overflow'] += 1
            counts['production_overflow_errors'] += old != expected
            counts['r3_overflow_errors'] += candidate != expected
            counts['r2_overflow_errors'] += prior != expected
        else:
            counts['normal_rounding_differences'] += old != expected
        if wide != expected or (overflow and (candidate != expected or prior != expected)):
            failures.append(dict(input=[x.hex() for x in c], expected=expected,
                                 old=old, r3=candidate, r2=prior, wide=wide))
    if not overflow:
        counts['r3_unchanged_path_errors'] += candidate != old
        counts['r2_unchanged_path_errors'] += prior != old

# 结论分别计数；R2 失败不伪装为 R3 失败或被抹除。
r3_pass = not any(counts[k] for k in ('wide_formula_errors', 'r3_overflow_errors',
                                     'r3_unchanged_path_errors'))
inputs = [OLD, Path(__file__), HERE/'probe.cpp', HERE.parent/'C4Probe.cpp',
          HERE.parent/'revision-candidate/compare-candidate.cpp',
          ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Compare.cpp',
          ROOT/'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Compare.hpp',
          ROOT/'vcpkg/installed/x64-windows/include/boost/version.hpp',
          ROOT/'vcpkg/installed/x64-windows/include/boost/multiprecision/cpp_bin_float.hpp',
          ROOT/'vcpkg/installed/x64-windows/include/boost/multiprecision/cpp_int/multiply.hpp']
result = dict(candidate='R3 Boost cpp_bin_float fixed 128-bit product / 4224-bit sums',
              head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
              python=sys.version, counts=counts, r3_verdict='PASS' if r3_pass else 'FAIL',
              independent_acceptance='NOT RUN',
              probe_stderr=probe_stderr,
              protocol_sha256=hashlib.sha256(protocol.encode()).hexdigest(),
              failures=failures,
              inputs=[dict(path=str(p.relative_to(ROOT)).replace('\\', '/'),
                           raw_sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in inputs])
(HERE/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:result[k] for k in ('r3_verdict','counts','independent_acceptance')}, indent=2))
sys.exit(0 if r3_pass else 1)
