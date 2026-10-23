from pathlib import Path
import json
offset=0x6c62272e07bb014262b821756295c58d
prime=(1<<88)+(1<<8)+0x3b
rows=[]
for name,data in [('empty',b''),('a',b'a'),('zero16',bytes(16)),('sequence16',bytes(range(16)))]:
 h=offset
 for byte in data: h=((h ^ byte)*prime)&((1<<128)-1)
 rows.append(dict(name=name,hex=data.hex(),hi=f'{h>>64:016x}',lo=f'{h&((1<<64)-1):016x}',fold=f'{(h>>64)^(h&((1<<64)-1)):016x}'))
print(json.dumps(rows,indent=2))
