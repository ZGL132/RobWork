import pathlib,sys,json
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
items=[
(p+'trajectory/src/Planner.cpp',158,193),(p+'trajectory/src/Planner.cpp',617,690),
(p+'optimization/src/Generator.cpp',110,205),(p+'optimization/src/Pareto.cpp',185,250),
(p+'kinematics/src/Envelope.cpp',270,330),(p+'kinematics/src/Envelope.cpp',375,440),
(p+'ui/plugin/HostView3DPreviewBackend.cpp',360,469),(p+'ui/plugin/HostView3DPreviewBackend.cpp',506,530),
(p+'workflow/src/CloseSupervisor.cpp',55,78),(p+'workflow/src/CloseSupervisor.cpp',95,180),
(p+'testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs',63,103),
(p+'testdata/golden/opt-pareto-golden/1.0.0/generate/make_opt_pareto_golden.mjs',353,450),
(p+'testkit/src/Check.cpp',44,100),
('RobWork/RobWork/src/sandbox/optimization/Optimizer.hpp',20,83),
('RobWork/RobWork/src/rw/common/BINArchive.hpp',286,311),
('RobWork/doc/industrial-robot-design/reuse/migration.md',133,149),
('RobWork/doc/industrial-robot-design/reuse/audit-contract.md',18,26)]
out=[]
for path,a,b in items:
 lines=(repo/path).read_text(encoding='utf-8-sig').splitlines()
 out.append(f'FILE {path} [{a}-{b}]\n'+'\n'.join(f'{n}: {lines[n-1]}' for n in range(a,min(b,len(lines))+1)))
(ev/'supplemental-sections.log').write_text('\n\n'.join(out),encoding='utf-8')
print('saved',len(items),'sections')
