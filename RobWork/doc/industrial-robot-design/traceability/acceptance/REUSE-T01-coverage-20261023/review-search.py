import pathlib,subprocess,json,sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent;repo=ev/'repo';p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
queries=[
('planner','planPtpWithObstacleAvoidance|plan\(|doInCollision|makeQSampler|RRT|Math::seed|StopCriteria|QEdgeConstraint|sampleStraightPath|PolicySessionQConstraint',[p+'trajectory/src/Planner.cpp']),
('opt-pareto','dominates\(|buildFront\(|generateNextBatch\(|computeStaticMetrics\(',[p+'optimization/src/Pareto.cpp',p+'optimization/src/Generator.cpp',p+'optimization/src/Objective.cpp']),
('energy-envelope','PowerEnergyCalculator::compute|trapezoid|dt|computeApproximateEnvelope|probeReachability|bisection|binary|atan|sqrt|hypot',[p+'dynamics/src/PowerEnergy.cpp',p+'kinematics/src/Envelope.cpp']),
('xacro','escapeXmlText\(|escapeXmlAttribute\(|expandExpression|evaluateExpression|expandNode|eval\(|lookup\(|expand\(|parse\(|load_buffer|load_string',[p+'modeling/src/XacroExpand.cpp']),
('protocol','putU32|putU64|getU32|getU64|FrameAssembler::feed|FrameSequencer::accept|MessageReassembler::accept|fragmentMessage',[p+'execution/src/ChannelProtocol.cpp']),
('project-codec','class Writer|class Reader|struct Writer|struct Reader|void u32|void f64|readU32|writeU32|encode|decode|canonical|Blob',[p+'project/src/Codec.cpp',p+'reporting/src/ReportCodec.cpp']),
('testkit','parseValue\(|skipWhitespace\(|checkCloseWithin\(|checkAllCloseWithin\(|quoteWindowsArg\(|buildEnvironmentBlock\(|TestProcessRunner::start|CreateProcessW|posix_spawn|fork\(|execv',[p+'testkit/src/JsonLite.cpp',p+'testkit/src/Check.cpp',p+'testkit/src/ProcessRunner.cpp']),
('foundation','^function\(|^macro\(|find_package\(|find_library\(|find_path\(|target_link_libraries\(|add_library\(',[p+'cmake/ird_gates.cmake',p+'testkit/CMakeLists.txt',p+'trajectory/CMakeLists.txt']),
('sample-tests','^TEST|^TEST_F|reference|oracle|sqrt|eigen|rotation|CreateProcess|hypot|std::sin|std::cos',[p+'optimization/test/ParetoTest.cpp',p+'dynamics/test/PowerEnergyTest.cpp',p+'modeling/test/XacroExpandTest.cpp']),
('upstream-planner','makeQToQPlanner|makeSBL|QConstraint|QSampler|makeNormalizedInfinity|PathLength|pathLength',[ 'RobWork/RobWork/src/rw/pathplanning','RobWork/RobWork/src/rwlibs/pathplanners','RobWork/RobWork/src/rw/trajectory/PathUtil.hpp']),
('upstream-math','isNaN|isInf|close|Metric|normInf|dot\(|ran\(',['RobWork/RobWork/src/rw/math/Math.hpp','RobWork/RobWork/src/rw/math/MetricUtil.hpp','RobWork/RobWork/src/rw/math/Q.hpp']),
('upstream-tools','DOMParser|createParser|XMLParser|readXMLFile|evaluate|Expression|Evaluator|Pareto|dominat|Power|energy|integrat',[ 'RobWork/RobWork/src/rw/core/DOMParser.hpp','RobWork/RobWork/src/rw/loaders','RobWork/RobWork/src/rwlibs/optimization','RobWork/RobWorkSim/src/rwsim/util']),
]
records=[]
for name,pat,paths in queries:
    args=['rg','-n',pat,*paths]
    run=subprocess.run(args,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    (ev/(name+'.stdout.log')).write_bytes(run.stdout);(ev/(name+'.stderr.log')).write_bytes(run.stderr)
    out=run.stdout.decode('utf-8',errors='replace').splitlines()
    records.append({'name':name,'command':args,'exitCode':run.returncode,'stdout':name+'.stdout.log','stderr':name+'.stderr.log','lines':len(out)})
    print(name,'exit',run.returncode,'lines',len(out),'stderr',run.stderr.decode('utf-8',errors='replace').strip()[:240])
    for line in out[:5]:print(' ',line)
(ev/'searches.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')

