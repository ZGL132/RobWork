import pathlib,subprocess,json,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
q=[
('up-planner-corrected',r'makeQToQPlanner|makeUniform|makeConstrained|makeEuclidean|static.*seed|virtual.*doInCollision',['RobWork/RobWork/src/rwlibs/pathplanners/rrt/RRTPlanner.hpp','RobWork/RobWork/src/rw/pathplanning/QSampler.hpp','RobWork/RobWork/src/rw/pathplanning/QConstraint.hpp','RobWork/RobWork/src/rw/math/MetricFactory.hpp','RobWork/RobWork/src/rw/math/Math.hpp']),
('up-optimization-corrected',r'Pareto|dominat|Latin.?Hyper|class.*Optimizer|double.*evaluate|virtual.*f\(',['RobWork/RobWork/src/sandbox/optimization','RobWork/RobWork/src/rwlibs/pathoptimization']),
('up-integrals',r'trapezoid|integrat|total energy|double.*energy|calcEnergy',['RobWork/RobWork/src/rw/math','RobWork/RobWorkSim/src/rwsim/dynamics/Body.hpp','RobWork/RobWorkSim/src/rwsim/dynamics/RigidBody.cpp','RobWork/RobWorkSim/src/rwsim/rwphysics/BodyIntegrator.hpp']),
('up-xml-json',r'load \(|save \(|make \(|read.*Property|write.*Property|json|xacro',['RobWork/RobWork/src/rw/core/DOMParser.hpp','RobWork/RobWork/src/rw/core/DOMElem.hpp','RobWork/RobWork/src/rw/loaders','RobWork/RobWork/src/rw/common/BINArchive.cpp']),
('up-test-build',r'ADD_RW_GTEST|add_rw_gtest|GTest|TestEnvironment|sdurw_pathplanners|sdurw_pathplanning|sdurw_core',['RobWork/RobWork/gtest/CMakeLists.txt','RobWork/RobWork/gtest/TestEnvironment.hpp','RobWork/RobWork/gtest/TestEnvironment.cpp','RobWork/RobWork/src/rw/CMakeLists.txt','RobWork/RobWork/src/rwlibs/CMakeLists.txt']),
('tests-corrected',r'^TEST|^TEST_F|expectNearRel|makeCandidate|digestOf|existingNodeFor|inline|golden',[p+'optimization/test/ParetoTest.cpp',p+'dynamics/test/StatisticsTest.cpp',p+'modeling/test/XacroExpandTest.cpp']),
('ui-workflow',r'^[A-Za-z_:<>].*::|WorkCellScene|setState|setWorkCell|addRender|remove|CloseSupervisor::|LifecycleService::|Comparison',[p+'ui/plugin/HostView3DPreviewBackend.cpp',p+'ui/src/HostController.cpp',p+'workflow/src/CloseSupervisor.cpp',p+'workflow/src/Lifecycle.cpp',p+'workflow/src/Comparison.cpp']),
('up-process',r'class ThreadPool|execute|ThreadPool \(|CreateProcess|ProcessLauncher',['RobWork/RobWork/src/rw/common/ThreadPool.hpp',p+'execution']),
('bounds-geometry',r'class AABB|class BoundingSphere|calcTightAABB|make.*Sphere|worldTframe|baseTend',['RobWork/RobWork/src/rw/geometry','RobWork/RobWork/src/rw/models/Device.hpp','RobWork/RobWork/src/rw/kinematics/Kinematics.hpp'])]
q.extend([
('golden-search',r'^function|Math\.|dominates|trapezoid|rneaReference',[p+'testdata/golden/dyn-two-link-analytic/1.0.0/generate/make_dyn_two_link_golden.mjs',p+'testdata/golden/opt-pareto-golden/1.0.0/generate/make_opt_pareto_golden.mjs']),
('up-host-scene',r'getWorkCell|setWorkCell|addRender|removeDrawable',['RobWork/RobWork/src/rw/graphics/WorkCellScene.hpp'])])
records=[]
for name,pat,paths in q:
 args=['rg','--with-filename','-n',pat,*paths];r=subprocess.run(args,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 (ev/(name+'.stdout.log')).write_bytes(r.stdout);(ev/(name+'.stderr.log')).write_bytes(r.stderr)
 records.append(dict(name=name,command=args,exitCode=r.returncode,stdout=name+'.stdout.log',stderr=name+'.stderr.log',lines=len(r.stdout.splitlines())))
 print(name,r.returncode,len(r.stdout.splitlines()),r.stderr.decode('utf-8',errors='replace')[:160])
(ev/'followup-searches.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
