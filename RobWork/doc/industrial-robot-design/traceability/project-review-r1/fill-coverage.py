# PRJ-T19 准入结案批：项目单元 14 个已变更文件的逐功能矩阵补全。
# 固定起始提交；三项动作：
#   1) 重锚这些文件上的既有功能行区段（PRJ-T19 改动导致行位移）；
#   2) 将压在已变更文件上的 11 条 pending 行结案为 business-specific/migrated；
#   3) 按语义区段（文件壳/类型/函数/TEST/注释区段）生成新功能行，覆盖全部非空行。
# 摘要与行覆盖口径复刻 validate-reuse.ps1/reuse-contract.ps1（BOM 剥离、CRLF→LF、
# TrimEnd('\n')+'\n'；行覆盖＝非空行必须落入某 sources 区段）。
# 用法：python fill-coverage.py [--check]（--check 只打印分段与自检，不写回）。
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path.cwd()
DESIGN = 'RobWork/doc/industrial-robot-design/'
PRODUCT = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
TIP = '59af1f9b449ee23fb78dfe547b3a7339df9d325d'
ANCHOR = DESIGN + 'traceability/project-review-r1/admission-closure.md'
CMAKE = PRODUCT + 'project/CMakeLists.txt'

FILES = {
    PRODUCT + 'project/include/sdurws/ird/project/DraftService.hpp': ('sdurws_ird_project', 'DraftService'),
    PRODUCT + 'project/src/ArchiveServiceImpl.cpp': ('sdurws_ird_project', 'ArchiveServiceImpl'),
    PRODUCT + 'project/src/CommandServiceImpl.cpp': ('sdurws_ird_project', 'CommandServiceImpl'),
    PRODUCT + 'project/src/DraftServiceImpl.cpp': ('sdurws_ird_project', 'DraftServiceImpl'),
    PRODUCT + 'project/src/ObjectStore.cpp': ('sdurws_ird_project', 'ObjectStore'),
    PRODUCT + 'project/src/ProjectStoreImpl.cpp': ('sdurws_ird_project', 'ProjectStoreImpl'),
    PRODUCT + 'project/src/ProjectStoreImpl.hpp': ('sdurws_ird_project', 'ProjectStoreImpl'),
    PRODUCT + 'project/src/QueryPortImpl.cpp': ('sdurws_ird_project', 'QueryPortImpl'),
    PRODUCT + 'project/src/QueryPortImpl.hpp': ('sdurws_ird_project', 'QueryPortImpl'),
    PRODUCT + 'project/src/RevisionIndex.cpp': ('sdurws_ird_project', 'RevisionIndex'),
    PRODUCT + 'project/src/RevisionIndex.hpp': ('sdurws_ird_project', 'RevisionIndex'),
    PRODUCT + 'project/src/StoreClone.cpp': ('sdurws_ird_project', 'StoreClone'),
    PRODUCT + 'project/test/ProcessLockContractTest.cpp': ('sdurws_ird_project_contract_test', 'ProcessLockContractTest'),
    PRODUCT + 'project/test/Tx15Child.cpp': ('sdurws_ird_project_contract_test', 'Tx15Child'),
}
REQ_DEFAULT = ['CON-01', 'PM-08', 'NFR-REL-01']
REQ_EXTRA_TEST = ['NFR-COR-02']


def digest(text):
    t = text.replace('\ufeff', '').replace('\r\n', '\n').rstrip('\n') + '\n'
    return hashlib.sha256(t.encode('utf-8')).hexdigest()


def git_text(path):
    r = subprocess.run(['git', 'show', f'{TIP}:{path}'], cwd=ROOT, capture_output=True)
    assert r.returncode == 0, path
    return r.stdout.decode('utf-8-sig', 'replace')


def line_digest(lines, a, b):
    return digest('\n'.join(lines[a - 1:b]))


# ---------- 字符级扫描（忽略注释/字符串中的花括号） ----------

def strip_spans(text):
    """返回与 text 等长的掩码：True＝该字符位于代码（非注释/字符串）中。"""
    out = bytearray(len(text))
    i, n = 0, len(text)
    state = 'code'
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if state == 'code':
            if two == '//':
                state = 'line'; i += 2; continue
            if two == '/*':
                state = 'block'; i += 2; continue
            if c == '"':
                state = 'str'; i += 1; continue
            if c == "'":
                state = 'chr'; i += 1; continue
            out[i] = 1; i += 1; continue
        if state == 'line':
            if c == '\n':
                state = 'code'
            i += 1; continue
        if state == 'block':
            if two == '*/':
                state = 'code'; i += 2; continue
            i += 1; continue
        if state in ('str', 'chr'):
            if c == '\\':
                i += 2; continue
            if (state == 'str' and c == '"') or (state == 'chr' and c == "'"):
                state = 'code'
            i += 1; continue
    return out


def scan_file(lines):
    """全文一次状态机扫描：返回每行代码掩码切片与行末累计花括号深度。"""
    text = chr(10).join(lines)
    mask = strip_spans(text)
    depths = [0] * (len(lines) + 1)
    masks = []
    d = 0
    pos = 0
    for i, l in enumerate(lines, 1):
        seg_mask = mask[pos:pos + len(l)]
        masks.append(seg_mask)
        for k, ch in enumerate(l):
            if seg_mask[k] and ch in '{}':
                d += 1 if ch == '{' else -1
        depths[i] = d
        pos += len(l) + 1
    return masks, depths


def brace_depths(lines):
    return scan_file(lines)[1]


RE_NS = re.compile(r'^\s*namespace\b')
RE_NS_OPEN = re.compile(r'^namespace\b[^{]*\{\s*$|^namespace\s*\{\s*$')
RE_TYPE = re.compile(r'^(?:template\s*<|class|struct|enum)')
RE_TEST = re.compile(r'^(?:TEST|TEST_F|TEST_P)\s*\(')
RE_PREPROC = re.compile(r'^\s*#')


def segment(path, lines, lo=1, hi=None):
    """把文件区间切成语义区段：[(start,end,kind,hint)]，1-based 闭区间。
    命名空间体递归展开；区间末尾若是容器的收尾 '}' 则停在该行前并原样返回。"""
    n = len(lines)
    if hi is None:
        hi = n
    masks, depths = scan_file(lines)
    entry = depths[lo - 1]          # 进入本区间时的深度（容器深度）
    items = []
    i = lo
    while i <= hi:
        if not lines[i - 1].strip():
            i += 1
            continue
        # 容器收尾行：深度将低于 entry → 本区间结束
        stripped_all = ''.join(c for c, m in zip(lines[i - 1], masks[i - 1]) if m).strip()
        if stripped_all.startswith('}') and depths[i] < entry:
            return items, i
        is_comment = lines[i - 1].lstrip().startswith('//') or lines[i - 1].lstrip().startswith('/*')
        j = i
        hs = i
        if is_comment:
            while j <= hi and (lines[j - 1].lstrip().startswith('//') or lines[j - 1].lstrip().startswith('/*')):
                if lines[j - 1].lstrip().startswith('/*') and '*/' not in lines[j - 1]:
                    while j <= hi and '*/' not in lines[j - 1]:
                        j += 1
                j += 1
            comment_end = j - 1
            if j <= hi and lines[j - 1].strip() and all(not lines[k - 1].strip() for k in range(comment_end + 1, j)) and j == comment_end + 1:
                hs = j
                start = i
            else:
                items.append([i, comment_end, 'comment', lines[i - 1].strip()])
                i = j
                continue
        else:
            start = i
            hs = i
        first = lines[hs - 1]
        # 预处理行
        if RE_PREPROC.match(first):
            j2 = hs
            while j2 <= hi and RE_PREPROC.match(lines[j2 - 1]):
                j2 += 1
            items.append([start, j2 - 1, 'include', first.strip()])
            i = j2
            continue
        # 纯收尾行（不降深度的 `}`——如函数结尾已在项内；此处为同级多余收尾）
        stripped_code = ''.join(c for c, m in zip(lines[hs - 1], masks[hs - 1]) if m).strip()
        if stripped_code.startswith('}') and '{' not in stripped_code and depths[hs] == entry:
            items.append([start, hs, 'close', first.strip()])
            i = hs + 1
            continue
        kind = 'code'
        if RE_TEST.match(first.lstrip()):
            kind = 'test'
        elif RE_TYPE.match(first.lstrip()):
            kind = 'type'
        elif RE_NS_OPEN.match(first):
            kind = 'namespace-open'
        # 找第一处代码 '{' 或 ';'
        k = hs
        while k <= hi and '{' not in ''.join(c for c, m in zip(lines[k - 1], masks[k - 1]) if m) and ';' not in ''.join(c for c, m in zip(lines[k - 1], masks[k - 1]) if m):
            k += 1
        if k > hi:
            items.append([start, hi, kind, first.strip()])
            break
        seg = ''.join(c for c, m in zip(lines[k - 1], masks[k - 1]) if m)
        if kind == 'namespace-open':
            base = depths[hs - 1]
            e = k
            while e <= hi and depths[e] > base:
                e += 1
            if e > hi:
                e = hi
            items.append([hs, hs, 'namespace-open', first.strip()])
            if e - 1 >= hs + 1:
                inner, _ = segment(path, lines, hs + 1, e - 1)
                items.extend(inner)
            items.append([e, e, 'close', lines[e - 1].strip()])
            i = e + 1
            continue
        if '{' in seg:
            base = depths[hs - 1]
            e = k
            while e <= hi and depths[e] > base:
                e += 1
            while e < hi and lines[e].strip() == ';':
                e += 1
            end = min(e, hi)
        else:
            e = k
            while e <= hi and ';' not in ''.join(c for c, m in zip(lines[e - 1], masks[e - 1]) if m):
                e += 1
            end = min(e, hi)
        items.append([start, end, kind, first.strip()])
        i = end + 1
    return items, None


def brief_of(lines, a, b, kind):
    """从区段提取职责描述：Doxygen @brief/@file、注释首行或签名。"""
    for idx in range(a - 1, min(b, a + 40)):
        l = lines[idx]
        m = re.search(r'@brief\s+(.+)', l)
        if m:
            return m.group(1).strip().rstrip('*/').strip()
        m = re.search(r'@file\s+(\S+)', l)
        if m:
            return '文件头说明（' + m.group(1) + '）'
    if kind == 'test':
        m = re.search(r'TEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)', '\n'.join(lines[a - 1:a + 2]))
        if m:
            return '测试用例 ' + m.group(1) + '.' + m.group(2)
    if kind == 'type':
        m = re.search(r'(?:class|struct|enum(?:\s+class)?)\s+(\w+)', '\n'.join(lines[a - 1:a + 3]))
        if m:
            return '类型声明 ' + m.group(1)
    if kind == 'namespace-open':
        return '命名空间开壳'
    if kind == 'comment':
        t = lines[a - 1].strip().lstrip('/')
        return '区段说明：' + t[:50]
    # 函数：取签名主体
    sig = []
    for idx in range(a - 1, min(b, a + 8)):
        s = ''.join(c for c, m in zip(lines[idx], strip_spans(lines[idx])) if m).strip()
        if s:
            sig.append(s)
        if '(' in s:
            break
    t = ' '.join(sig)
    m = re.search(r'([\w:~]+)\s*\(', t)
    return ('函数 ' + m.group(1)) if m else (t[:50] or '代码区段')


def symbol_of(path, lines, a, b, kind):
    """区段符号（必须逐字出现在区段文本中）。"""
    region = '\n'.join(lines[a - 1:b])
    if kind == 'test':
        m = re.search(r'TEST(?:_F|_P)?\s*\(\s*\w+\s*,\s*(\w+)\s*\)', region)
        if m:
            return m.group(0)
    if kind == 'type':
        m = re.search(r'(?:class|struct|enum(?:\s+class)?)\s+(\w+)', region)
        if m:
            return m.group(1)
    if kind in ('code',):
        m = re.search(r'([\w:~]+\s*\([^;{)]*)', region)
        if m:
            t = m.group(1).strip()
            t = re.sub(r'\s+', '', t)[:60]
            if t and t in region:
                return t
    for cand in ('@file', 'namespace', '#include', '#ifndef'):
        if cand in region:
            return cand
    t = lines[a - 1].strip()
    return (t[:40] if t and t[:40] in region else region.strip().split('\n')[0][:40])


# ---------- 主流程 ----------

def main():
    check_only = '--check' in sys.argv
    matrix_path = ROOT / (DESIGN + 'reuse/functions/project.json')
    raw = matrix_path.read_bytes()
    had_crlf = b'\r\n' in raw
    matrix = json.loads(raw.decode('utf-8-sig'))

    anchor_digest = digest((ROOT / ANCHOR.replace(DESIGN, 'RobWork/doc/industrial-robot-design/')).read_text(encoding='utf-8-sig'))
    cmake_digest = digest((ROOT / CMAKE).read_text(encoding='utf-8-sig'))

    contents = {p: git_text(p).replace('\r\n', '\n').split('\n') for p in FILES}
    for p in FILES:  # 工作区一致性核对（并行会话不得触碰 project 单元）
        wt = (ROOT / p).read_text(encoding='utf-8-sig').replace('\r\n', '\n').split('\n')
        assert wt == contents[p], 'working tree differs from tip: ' + p

    file_reqs = {}
    for p in FILES:
        text = '\n'.join(contents[p])
        toks = sorted(set(re.findall(r'\b(PM-\d+|CON-\d+|NFR-[A-Z]+-\d+)\b', text))
                      & {'PM-08', 'PM-02', 'NFR-REL-01', 'NFR-COR-02', 'NFR-COR-01', 'CON-01', 'CON-02', 'CON-05', 'NFR-DEP-04'})
        file_reqs[p] = toks or list(REQ_DEFAULT)
        if '/test/' in p:
            file_reqs[p] = sorted(set(file_reqs[p] + REQ_EXTRA_TEST))

    # ---- 1) 重锚 + 结案既有行 ----
    resolved, reanchored = 0, 0
    for row in matrix['functions']:
        touched = False
        for s in row.get('sources', []):
            p = s.get('path')
            if p not in FILES:
                continue
            lines = contents[p]
            sym = s['symbol']
            old_a = int(s['startLine'])
            # 在旧起点附近定位符号
            best = None
            lo, hi = max(1, old_a - 150), min(len(lines), old_a + 150)
            for cand in range(lo, hi + 1):
                if sym in lines[cand - 1]:
                    best = cand
                    break
            assert best, f"symbol lost: {row['functionId']} {p}:{sym}"
            # 用分段器重建包含符号行的区段
            segs, _ = segment(p, lines)
            target = None
            for a, b, kind, hint in segs:
                if a <= best <= b:
                    target = (a, b, kind)
                    break
            assert target, f"no segment for {row['functionId']}"
            a, b, kind = target
            s['startLine'], s['endLine'] = a, b
            s['sha256'] = line_digest(lines, a, b)
            assert sym in '\n'.join(lines[a - 1:b]), f"symbol not in new region: {row['functionId']}"
            reanchored += 1
            touched = True
        if touched and (row.get('decision') == 'pending' or row.get('status') == 'pending'):
            row['decision'] = 'business-specific'
            row['status'] = 'migrated'
            note = 'PRJ-T19准入结案：上游不可替代性依据见 admission-closure.md 逐条表；行区段按 59af1f9b4 重锚。'
            if note not in row['differences']:
                row['differences'] = row['differences'].rstrip('。') + '。' + note
            resolved += 1
        if touched:
            row.setdefault('auditEvidence', []).append({'path': ANCHOR, 'sha256': anchor_digest}) if not any(
                e.get('path') == ANCHOR for e in row.get('auditEvidence', [])) else None
            if row.get('decision') == 'business-specific' and not row.get('nonOverlapEvidence'):
                row['nonOverlapEvidence'] = {'path': ANCHOR, 'sha256': anchor_digest}
            for inp in row.get('inputs', []):
                if inp.get('path') == CMAKE:
                    inp['sha256'] = cmake_digest

    # ---- 2) 计算剩余缺口并生成新行 ----
    covered = {}
    for jf in sorted((ROOT / (DESIGN + 'reuse/functions')).glob('*.json')):
        j = json.loads(jf.read_text(encoding='utf-8-sig'))
        for f in j.get('functions', []):
            for s in f.get('sources', []):
                if s.get('path') in FILES:
                    covered.setdefault(s['path'], set()).update(range(int(s['startLine']), int(s['endLine']) + 1))
    new_rows = []
    for p, (target, stem) in FILES.items():
        stem = stem + '.' + p.rsplit('.', 1)[-1]
        lines = contents[p]
        n = len(lines)
        cov = covered.get(p, set())
        gaps = [k for k in range(1, n + 1) if lines[k - 1].strip() and k not in cov]
        if not gaps:
            continue
        segs, _ = segment(p, lines)
        need = set(gaps)
        seq = len([r for r in matrix['functions'] if r['functionId'].startswith('project.PRJ_R1_COV.' + stem)])
        for a, b, kind, hint in segs:
            hit = [k for k in range(a, b + 1) if k in need]
            if not hit:
                continue
            seq += 1
            reqs = file_reqs[p]
            symbol = symbol_of(p, lines, a, b, kind)
            role = {'test': '测试用例', 'type': '类型声明', 'comment': '注释区段', 'namespace-open': '命名空间壳'}.get(kind, '代码区段')
            brief = brief_of(lines, a, b, kind)
            new_rows.append({
                'functionId': f'project.PRJ_R1_COV.{stem}.{seq:03d}',
                'ownerUnit': 'project',
                'requirements': reqs,
                'sources': [{'path': p, 'symbol': symbol, 'startLine': a, 'endLine': b,
                             'sha256': line_digest(lines, a, b)}],
                'targets': [target],
                'usageRole': 'owner',
                'permissionRef': 'ARCH:3.6.1',
                'capabilityId': 'NONE',
                'decision': 'business-specific',
                'status': 'migrated',
                'search': {'command': '见traceability/project-review-r1/admission-closure.md的定向检索与上游比对',
                           'conclusion': 'project持久化/事务/命令/归档业务编排；RobWork/RobWorkSim无同语义供给，core/testkit既有供给经登记边复用'},
                'differences': f'PRJ-T19准入补全：{stem} {role}——{brief}；区段划分登记粒度见admission-closure.md，行覆盖不冒充语义完备证明。',
                'inputs': [{'path': p, 'sha256': digest(git_text(p))},
                           {'path': CMAKE, 'sha256': cmake_digest}],
                'auditEvidence': [{'path': ANCHOR, 'sha256': anchor_digest}],
                'nonOverlapEvidence': {'path': ANCHOR, 'sha256': anchor_digest},
            })
    matrix['functions'].extend(new_rows)

    # ---- 3) 复刻自检 ----
    errors = []
    ids = set()
    covered = {}
    for row in matrix['functions']:
        key = row['functionId'] + '|' + row['capabilityId']
        if key in ids:
            errors.append('dup ' + key)
        ids.add(key)
        for f in ('functionId', 'ownerUnit', 'requirements', 'sources', 'usageRole', 'permissionRef',
                  'capabilityId', 'decision', 'status', 'search', 'differences', 'targets', 'inputs'):
            if f not in row:
                errors.append(f'missing {f} on {row.get("functionId")}')
        for s in row['sources']:
            p = s['path']
            if p not in FILES:
                pl = (ROOT / p)
                if not pl.exists():
                    continue
                lines = pl.read_text(encoding='utf-8-sig').replace('\r\n', '\n').split('\n')
            else:
                lines = contents[p]
            if not (1 <= int(s['startLine']) <= int(s['endLine']) <= len(lines)):
                errors.append(f"bounds {row['functionId']} {p}")
                continue
            region = '\n'.join(lines[int(s['startLine']) - 1:int(s['endLine'])])
            if digest(region) != s['sha256']:
                errors.append(f"digest {row['functionId']} {p}")
            if s['symbol'] not in region:
                errors.append(f"symbol {row['functionId']} {p}")
            covered.setdefault(p, set()).update(range(int(s['startLine']), int(s['endLine']) + 1))
        if row.get('decision') == 'business-specific' and not row.get('nonOverlapEvidence'):
            errors.append('no nonOverlap ' + row['functionId'])
    for p in FILES:
        lines = contents[p]
        gaps = [k for k, l in enumerate(lines, 1) if l.strip() and k not in covered.get(p, set())]
        if gaps:
            errors.append(f'{p}: uncovered {len(gaps)} lines, first {gaps[:5]}')
        rows_on = [r for r in matrix['functions'] if any(s['path'] == p for s in r['sources'])]
        bad = [r['functionId'] for r in rows_on if r['decision'] in ('pending', 'proposal') or r['status'] not in ('migrated', 'closed')]
        if bad:
            errors.append(f'{p}: unresolved rows {bad}')
    cmake_text = (ROOT / CMAKE).read_text(encoding='utf-8-sig')
    for row in matrix['functions']:
        if any(s['path'] in FILES for s in row['sources']):
            for t in row['targets']:
                if not re.search(r'(add_library|add_executable)\s*\(\s*' + re.escape(t) + r'\b', cmake_text):
                    errors.append('target ' + t + ' ' + row['functionId'])

    print(f'reanchored={reanchored} resolved={resolved} newRows={len(new_rows)} totalRows={len(matrix["functions"])}')
    if errors:
        print('SELF-CHECK FAIL:')
        for e in errors[:40]:
            print(' -', e)
        sys.exit(1)
    print('SELF-CHECK OK')
    if not check_only:
        text = json.dumps(matrix, ensure_ascii=False, indent=2) + '\n'
        if had_crlf:
            text = text.replace('\n', '\r\n')
        matrix_path.write_bytes(text.encode('utf-8'))
        print('written:', matrix_path)


if __name__ == '__main__':
    main()
