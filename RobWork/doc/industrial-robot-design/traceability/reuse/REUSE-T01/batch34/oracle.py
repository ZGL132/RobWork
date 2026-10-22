"""B34固定清单字节oracle；使用struct/hashlib，不读取产品输出生成期望。"""
import struct, hashlib, json

def fixed_manifest(deprecated=False):
    data = bytearray(b'IRDDCM2')
    def text(value):
        raw = value.encode('ascii')
        data.extend(struct.pack('<I', len(raw)))
        data.extend(raw)
    for value in ['RT-AUDIT', 'runtime', 'input-invalid', 'error',
                  'diag.rt-audit.title', 'diag.rt-audit.detail', '[]']:
        text(value)
    data.extend(bytes([0, 0]))
    text('user-retry')
    data.extend(bytes([1, 1, 1]))
    data.extend(struct.pack('<I', 0x01020304))
    data.extend(bytes([int(deprecated), int(deprecated)]))
    if deprecated:
        text('RT-NEXT')
    data.append(0)
    return bytes(data)

values = {'empty': b'IRDDCM2', 'one': fixed_manifest(), 'deprecated': fixed_manifest(True)}
print(json.dumps({key: {'bytes': len(raw), 'hex': raw.hex(),
                       'sha256': hashlib.sha256(raw).hexdigest()}
                  for key, raw in values.items()}, indent=2))
