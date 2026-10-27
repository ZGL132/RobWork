# -*- coding: utf-8 -*-
"""按 validate-reuse.ps1 Digest 口径复算 manifest 逐文件 SHA256（验收自查）。"""
import hashlib

def digest(p):
    b = open(p, 'rb').read()
    t = b.decode('utf-8-sig').replace('\r\n', '\n').rstrip('\n') + '\n'
    return hashlib.sha256(t.encode('utf-8')).hexdigest()

pairs = [
    ('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Compare.cpp',
     'd9f458559214fb7d36bf70e82cfdb0917bc2780154ee6abed884b1499f9857bc'),
    ('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/test/CompareTest.cpp',
     'ea6c490e3dac3683f1d64e622f20473c57d0b0aeade898c1e76472bfd54b8444'),
    ('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/CMakeLists.txt',
     '687b120dfa8635b1fed6b7f21522c70d8d82da57c80d7dcea06cea2c6b93b377'),
]
for p, exp in pairs:
    got = digest(p)
    print(('MATCH' if got == exp else 'MISMATCH'), p, got)
