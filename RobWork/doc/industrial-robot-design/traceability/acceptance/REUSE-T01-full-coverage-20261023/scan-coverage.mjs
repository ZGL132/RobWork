import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import cp from 'node:child_process';
const out=path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/,'$1'));
const root=path.join(out,'repo');
const design='RobWork/doc/industrial-robot-design';
const read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/^\uFEFF/,'');
const json=p=>JSON.parse(read(p));
const write=(p,v)=>fs.writeFileSync(path.join(out,p),typeof v==='string'?v:JSON.stringify(v,null,2)+'\n');
const inventory=json(design+'/reuse/inventory.json').files;
const rows=fs.readdirSync(path.join(root,design,'reuse/functions')).filter(x=>x.endsWith('.json')).flatMap(f=>json(design+'/reuse/functions/'+f).functions.map(r=>({...r,matrixFile:design+'/reuse/functions/'+f})));
const refs=new Map(), inputs=new Map();
for(const r of rows){for(const s of r.sources??[]){if(!refs.has(s.path))refs.set(s.path,[]);refs.get(s.path).push({functionId:r.functionId,capabilityId:r.capabilityId,startLine:s.startLine,endLine:s.endLine,symbol:s.qualifiedSymbol??s.symbol,matrixFile:r.matrixFile});}
 for(const s of r.auditedSections??[]){let p=s.path??r.sources?.[0]?.path;if(!p)continue;if(!refs.has(p))refs.set(p,[]);refs.get(p).push({functionId:r.functionId,kind:'auditedSection',startLine:s.startLine,endLine:s.endLine,symbol:s.symbol??'',matrixFile:r.matrixFile});}
 for(const s of r.inputs??[]){if(!inputs.has(s.path))inputs.set(s.path,[]);inputs.get(s.path).push(r.functionId);}}
write('source-reference-index.json',Object.fromEntries(refs));
const strip=s=>s.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');
const files=inventory.map(f=>{
 const exists=fs.existsSync(path.join(root,f.path));const raw=exists?read(f.path):'';const code=strip(raw);const ext=path.extname(f.path).toLowerCase();const rs=refs.get(f.path)??[];
 const isHeader=['.h','.hpp','.hxx','.inl','.tpp'].includes(ext);const hasBody=/\)\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?\s*)?(?:override\s*)?(?:final\s*)?(?:->[^\n{]+\s*)?\{|\bTEST(?:_F|_P)?\s*\(/m.test(code);
 const template=/\btemplate\s*</.test(code);const mjs=ext==='.mjs';
 let candidateKind=mjs?'executable-mjs':f.role==='build'?'build':/\.(ps1|py|js|sh|cmake)$/.test(ext)?'script':ext==='.cpp'?'cpp-implementation':isHeader&&(hasBody||template)?'inline-template-header':isHeader?'declaration-dto-header-candidate':'other';
 return {...f,exists,lineCount:raw.split(/\r?\n/).length,sourceReferences:rs.length,sourceIntervals:rs,inputOnly:!rs.length&&inputs.has(f.path),candidateKind,hasInlineBody:hasBody,hasTemplate:template,sha256:crypto.createHash('sha256').update(raw.replace(/\r\n/g,'\n').replace(/\n*$/,'\n')).digest('hex')};
});
const units=[...new Set(files.map(f=>f.unit))].sort();
const counts=units.map(unit=>{const all=files.filter(f=>f.unit===unit), un=all.filter(f=>!f.sourceReferences);return {unit,baseline:all.length,sourceReferenced:all.length-un.length,noFunctionSource:un.length,unreferencedProduction:un.filter(f=>f.role==='production').length,unreferencedTest:un.filter(f=>f.role==='test').length,unreferencedBuild:un.filter(f=>f.role==='build').length,unreferencedOther:un.filter(f=>!['production','test','build'].includes(f.role)).length,candidateImplementationOrScript:un.filter(f=>f.candidateKind!=='declaration-dto-header-candidate'&&f.candidateKind!=='other').length,declarationDtoHeaderCandidates:un.filter(f=>f.candidateKind==='declaration-dto-header-candidate').length,inputOnly:un.filter(f=>f.inputOnly).length,relations:rows.filter(r=>r.ownerUnit===unit).length};});
const tracked=cp.execFileSync('git',['ls-files'],{cwd:root,encoding:'utf8',maxBuffer:20e6}).trim().split('\n');
const baseSet=new Set(inventory.map(f=>f.path));
const fresh=tracked.filter(p=>(p.startsWith('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/')||p.startsWith('RobWork/scripts/industrialrobot/'))&&(/\.(cpp|hpp|h|hxx|cxx|inl|tpp|mjs|py|ps1|sh|cmake|js|html)$/.test(p)||p.endsWith('CMakeLists.txt'))&&!baseSet.has(p));
const byExt={};for(const f of files.filter(f=>!f.sourceReferences)){const e=path.extname(f.path);byExt[e]=(byExt[e]??0)+1;}
const summary={frozenCommit:cp.execFileSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).trim(),baselineFiles:files.length,relations:rows.length,distinctFunctions:new Set(rows.map(r=>r.functionId)).size,distinctSourcePaths:refs.size,sourceReferencedBaseline:files.filter(f=>f.sourceReferences).length,noFunctionSourceBaseline:files.filter(f=>!f.sourceReferences).length,inputOnly:files.filter(f=>f.inputOnly).length,mjsTotal:files.filter(f=>f.path.endsWith('.mjs')).length,mjsNoSources:files.filter(f=>f.path.endsWith('.mjs')&&!f.sourceReferences).length,unreferencedByExtension:byExt,currentCodeFilesBeyondBaseline:fresh.length,missingBaseline:files.filter(f=>!f.exists).map(f=>f.path),decisionCounts:Object.fromEntries([...new Set(rows.map(r=>r.decision))].map(k=>[k,rows.filter(r=>r.decision===k).length]))};
write('inventory-analysis.json',{summary,units:counts,files,currentCodeFilesBeyondBaseline:fresh});
write('unit-counts.tsv',['unit\tbaseline\tsourceReferenced\tnoFunctionSource\tproduction\ttest\tbuild\tother\timplementationOrScriptCandidate\tdeclarationDtoCandidate\tinputOnly\trelations',...counts.map(f=>[f.unit,f.baseline,f.sourceReferenced,f.noFunctionSource,f.unreferencedProduction,f.unreferencedTest,f.unreferencedBuild,f.unreferencedOther,f.candidateImplementationOrScript,f.declarationDtoHeaderCandidates,f.inputOnly,f.relations].join('\t'))].join('\n')+'\n');
const un=files.filter(f=>!f.sourceReferences);
write('candidate-files.tsv',['unit\trole\tkind\tlines\tinputOnly\tpath',...un.map(f=>[f.unit,f.role,f.candidateKind,f.lineCount,f.inputOnly,f.path].join('\t'))].join('\n')+'\n');
write('mjs-coverage.json',files.filter(f=>f.path.endsWith('.mjs')));
write('current-files-beyond-baseline.json',fresh);
console.log(JSON.stringify(summary,null,2));console.log(counts.map(f=>`${f.unit}: baseline=${f.baseline} unreferenced=${f.noFunctionSource} impl/script=${f.candidateImplementationOrScript} dto/declaration=${f.declarationDtoHeaderCandidates} rows=${f.relations}`).join('\n'));
