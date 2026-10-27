import pathlib,json,subprocess,hashlib
root=pathlib.Path(__file__).parent; repo=root/'clone'; design=repo/'RobWork/doc/industrial-robot-design'
rows=[]
prime=int('0000000001000000000000000000013b',16)
for name,data in [('empty',bytes()),('a',bytes([97])),('zero16',bytes([0]*16)),('sequence16',bytes(range(16)))]:
 value=int('6c62272e07bb014262b821756295c58d',16)
 for octet in data: value=((value^octet)*prime)%(2**128)
 full=value.to_bytes(16,'big'); hi=int.from_bytes(full[:8],'big');lo=int.from_bytes(full[8:],'big')
 rows.append({'name':name,'hex':data.hex(),'hi':f'{hi:016x}','lo':f'{lo:016x}','fold':f'{hi^lo:016x}'})
expected=json.loads((design/'traceability/reuse/REUSE-T01/batch28/fnv-vectors.json').read_text(encoding='utf-8-sig'))
reports={}
for target in ('core','probe'):
 p=root/target; j=json.loads((p/'ird-test-report.json').read_text(encoding='utf-8-sig')); reports[target]={'exit':int((p/'exit.txt').read_text(encoding='utf-8-sig')),'summary':j['summary']}
result={'algorithm':'independent Python arbitrary-precision; prime parsed fixed 128-bit hexadecimal, modulo 2^128','vectorsEqual':rows==expected,'vectors':rows,'executions':reports}
(root/'independent-oracle.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8'); print(json.dumps(result,ensure_ascii=False,indent=2))
