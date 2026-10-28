"""隔离构建：只读取起始 SHA/本树源码，沿既有集成目标的 include 与库供给。"""
from pathlib import Path
import re
import subprocess

REPO = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
EXECUTION = REPO / 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution'
BASE = '6a49e128c00f8cca3943083819ea59aea16a7be9'
(OUT / 'baseline').mkdir(exist_ok=True)
for relative in ['src/WorkerSupervisor.cpp', 'src/win32/ChannelPair.cpp', 'src/win32/ChannelPair.hpp']:
    path = (EXECUTION / relative).relative_to(REPO).as_posix()
    content = subprocess.check_output(['git', 'show', f'{BASE}:{path}'], cwd=REPO).decode('utf-8-sig')
    if relative.endswith('WorkerSupervisor.cpp'):
        # 仅适配已获批的返回类型，使旧“失败忽略”函数体可与新消费者同编译。
        content = content.replace('std::uint64_t WorkerSupervisor::aggregateJobMemoryBytes()',
                                  'std::optional<std::uint64_t> WorkerSupervisor::aggregateJobMemoryBytes()')
    (OUT / 'baseline' / Path(relative).name).write_text(content, encoding='utf-8')

prior = (OUT / 'compile-inputs.rsp').read_text(encoding='utf-8-sig')
includes = re.findall(r'/I"[^"]+"', prior)
libraries = list(dict.fromkeys(re.findall(r'"[^"]+\.lib"', prior)))
for variant in ['red', 'green']:
    options = ['/nologo', '/std:c++17', '/MD', '/O2', '/EHsc', '/utf-8',
               '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/DGTEST_LINKED_AS_SHARED_LIBRARY=1']
    if variant == 'red': options.append('/DEXR_BASELINE')
    options += includes
    options.append('/DIRD_EXECUTION_WORKER_EXE=\\"' +
                   (REPO / 'build/RobWorkStudio/bin/Release/sdurws_ird_execution_worker.exe').as_posix() + '\\"')
    options += ['"' + str(OUT / 'MemoryFailureProbe.cpp') + '"',
                '"' + str(OUT / 'OverlappedReadProbe.cpp') + '"',
                '"' + str(EXECUTION / 'test/TestMainReport.cpp') + '"',
                '/Fe"' + str(OUT / f'lifecycle-probes-{variant}.exe') + '"', '/link'] + libraries
    (OUT / f'probes-{variant}.rsp').write_text('\n'.join(options) + '\n', encoding='utf-8')
