"""TK-T13：变动公共头自包含编译；只在专用证据目录生成源码/日志。"""
import argparse
import json
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--vcpkg-include', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)
product = args.repo / 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot'
includes = [product / 'testkit/include', product / 'core/include',
            args.repo / 'RobWork/RobWork/src', args.vcpkg_include,
            args.vcpkg_include / 'eigen3']
results = []
for header in ['Check.hpp', 'JsonLite.hpp', 'PerfBenchmark.hpp', 'ReproBenchmark.hpp',
               'SetCheck.hpp', 'gtest/AssertMacros.hpp']:
    base = args.out / Path(header).stem
    source = base.with_suffix('.cpp')
    source.write_text('#include <sdurws/ird/testkit/' + header + '>\n', encoding='utf-8')
    command = ['cl.exe', '/nologo', '/std:c++17', '/EHsc', '/MD', '/utf-8',
               '/DGTEST_LINKED_AS_SHARED_LIBRARY=1', '/Zs']
    command += ['/I' + str(x.resolve()) for x in includes] + [str(source.resolve())]
    run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    base.with_suffix('.log').write_bytes(run.stdout)
    results.append(dict(header=header, command=command, actualExit=run.returncode,
                        status='PASS' if run.returncode == 0 else 'FAIL'))
(args.out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('\n'.join(x['header'] + ': ' + x['status'] for x in results))
raise SystemExit(any(x['status'] != 'PASS' for x in results))
