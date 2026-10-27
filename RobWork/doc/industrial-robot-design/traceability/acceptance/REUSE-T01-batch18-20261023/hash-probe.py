import pathlib,hashlib,subprocess,json,sys
r=pathlib.Path(sys.argv[1]); repo=r/'repo'; p='RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/probe-r18/ird-test-report.json'; blob=subprocess.check_output(['git','show','HEAD:'+p],cwd=repo);raw=(repo/p).read_bytes(); variants={'raw':raw,'gitBlob':blob,'LF':raw.replace(b'\r\n',b'\n'),'blobLF':blob.replace(b'\r\n',b'\n'),'LFnoFinalNewline':raw.replace(b'\r\n',b'\n').rstrip(b'\n')}
out={k:hashlib.sha256(v).hexdigest() for k,v in variants.items()};(r/'original-native-hash-variants.json').write_text(json.dumps(out,indent=2));print(out)
