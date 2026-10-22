import json,hashlib
h=0x6c62272e07bb014262b821756295c58d
for b in bytes(range(32)):h=((h^b)*((1<<88)+(1<<8)+0x3b))&((1<<128)-1)
print(json.dumps(dict(inputHex=bytes(range(32)).hex(),hi=f'{h>>64:016x}',lo=f'{h&((1<<64)-1):016x}',fold=f'{(h>>64)^(h&((1<<64)-1)):016x}',shaBoundaryVectors=[dict(length=n,sha256=hashlib.sha256(bytes(i%251 for i in range(n))).hexdigest()) for n in [0,1,55,56,63,64,65,119,120,127,128,129,4097,1000000]]),indent=2))
