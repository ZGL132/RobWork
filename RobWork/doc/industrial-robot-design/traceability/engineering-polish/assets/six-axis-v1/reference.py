"""QP-01/02 独立参考：只用 Python 标准库，不调用 RobWork 产品计算。

从同目录 inputs.json 计算刚体变换与长方体解析惯量，并核验仓内黄金数据。
输出是解析案例的审查证据，不能替代产品导入、编译、求解或实测验收。
用法：python reference.py --repo <仓库根> --out <独立输出目录>
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def multiply(a, b):
    """固定次序的 4×4 矩阵乘法；不共享产品实现。"""
    return [[sum(a[i][k] * b[k][j] for k in range(4))
             for j in range(4)] for i in range(4)]


def transform(xyz, axis=(0, 0, 1), angle=0):
    """平移单位 m，轴在父参考系下，角度 rad；Rodrigues 旋转公式。"""
    norm = math.sqrt(sum(x * x for x in axis))
    x, y, z = [v / norm for v in axis]
    c, s, t = math.cos(angle), math.sin(angle), 1 - math.cos(angle)
    return [[t*x*x+c, t*x*y-s*z, t*x*z+s*y, xyz[0]],
            [t*x*y+s*z, t*y*y+c, t*y*z-s*x, xyz[1]],
            [t*x*z-s*y, t*y*z+s*x, t*z*z+c, xyz[2]],
            [0, 0, 0, 1]]


def fk(model, q):
    """父连杆→关节原点平移→局部轴旋转→下一级；末尾乘法兰→TCP。"""
    value = transform([0, 0, 0])
    for joint, angle in zip(model['joints'], q, strict=True):
        value = multiply(value, transform(joint['origin']))
        value = multiply(value, transform([0, 0, 0], joint['axis'], angle))
    tcp = model['tcpOffset']
    return multiply(value, transform(tcp['xyz'], tcp['axis'], tcp['angle']))


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2,
                               allow_nan=False) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    root = args.repo / 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/golden'
    inputs = json.loads(Path(__file__).with_name('inputs.json').read_text(encoding='utf-8'))
    integrity, inventory = [], []
    for manifest in sorted(root.glob('*/*/manifest.json')):
        data = json.loads(manifest.read_text(encoding='utf-8-sig'))
        inventory.append({k: data.get(k) for k in
                          ('datasetId', 'version', 'kind', 'referenceSource', 'generator')})
        for item in data.get('integrity', []):
            path = manifest.parent / item['path']
            raw = path.read_bytes() if path.is_file() else b''
            digest = hashlib.sha256(raw).hexdigest()
            integrity.append({'file': str(path.relative_to(args.repo)),
                              'exists': path.is_file(), 'sha256': digest,
                              'hashMatches': digest == item['sha256'],
                              'sizeMatches': len(raw) == item['sizeBytes']})
    golden = root / 'kin-fk-analytic/1.0.0'
    cases = json.loads((golden / 'inputs/fk-cases.json').read_text(encoding='utf-8'))
    expected = json.loads((golden / 'expected/fk-expected.json').read_text(encoding='utf-8'))
    comparisons = []
    for case, oracle in zip(cases['cases'], expected['cases'], strict=True):
        assert case['id'] == oracle['caseId']
        matrix = fk(cases['models'][case['model']], case['q'])
        tcp = oracle['tcp']
        ref = [[tcp['rotation'][a+b] for b in 'xyz'] + [tcp['position'][a]]
               for a in 'xyz'] + [[0, 0, 0, 1]]
        comparisons.append({'id': case['id'], 'matrix': matrix,
                            'maxPositionErrorM': max(abs(matrix[i][3]-ref[i][3]) for i in range(3)),
                            'maxRotationElementError': max(abs(matrix[i][j]-ref[i][j]) for i in range(3) for j in range(3))})
    poses = []
    for mount in inputs['mounts']:
        base = transform(mount['translationM'], mount['axis'], mount['angleRad'])
        for pose in inputs['poses']:
            poses.append({'mount': mount['id'], 'pose': pose['id'],
                          'T_world_tcp': multiply(base, fk(inputs['model'], pose['qRad']))})
    physical = []
    for part in inputs['physicalParts']:
        mass = part['massKg']
        a, b, c = part['boxDimensionsM']
        principal = [mass*(b*b+c*c)/12, mass*(a*a+c*c)/12, mass*(a*a+b*b)/12]
        # 主矩以质心为参考点，主轴与连杆局部坐标系平行；平行轴公式另行输出。
        center = part['comInLinkM']
        inertia = [[(principal[i] if i == j else 0) + mass *
                    ((sum(x*x for x in center) if i == j else 0) - center[i]*center[j])
                    for j in range(3)] for i in range(3)]
        physical.append({'id': part['id'], 'principalComKgM2': principal,
                         'I_linkOrigin_kgM2': inertia,
                         'spd': min(principal) > 0,
                         'triangle': max(principal) <= sum(principal)-max(principal)})
    result = {'schema': 'engineering-polish-reference/1',
              'scope': '独立参考与输入自检；非产品执行结果',
              'inputSha256': hashlib.sha256(Path(__file__).with_name('inputs.json').read_bytes()).hexdigest(),
              'goldenPoseComparison': comparisons, 'mountedPoses': poses,
              'physicalReference': physical,
              'symmetryCounterexample': {'unit': 'kg*m^2',
                  'matrix': [[1e-6, 5e-13, 0], [0, 1e-6, 0], [0, 0, 1e-6]],
                  'absoluteMaxDifference': 5e-13,
                  'relativeDifference': 5e-13 / math.sqrt(3e-12+2*(2.5e-13)**2),
                  'contractRelativeTolerance': 1e-12,
                  'note': '绝对判据放行、相对判据拒绝；产品复现另见 runtime 探针'}}
    dump(args.out / 'reference-results.json', result)
    dump(args.out / 'golden-integrity.json', integrity)
    dump(args.out / 'golden-inventory.json', inventory)
    assert all(x['maxPositionErrorM'] <= 1e-12 and x['maxRotationElementError'] <= 1e-12 for x in comparisons)
    assert all(x['spd'] and x['triangle'] for x in physical)
    bad = [x for x in integrity if not x['hashMatches'] or not x['sizeMatches'] or not x['exists']]
    print(json.dumps({'datasets': len(inventory), 'integrityFiles': len(integrity),
                      'integrityFailures': len(bad), 'goldenPosesCompared': len(comparisons),
                      'mountedReferences': len(poses), 'physicalParts': len(physical)}))
    if bad:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
