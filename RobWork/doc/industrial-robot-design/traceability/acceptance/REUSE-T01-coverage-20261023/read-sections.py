import pathlib, json, sys
sys.stdout.reconfigure(encoding='utf-8')
ev=pathlib.Path(__file__).parent; repo=ev/'repo'
p='RobWork/RobWorkStudio/src/rwslibs/industrialrobot/'
sections=[
('optimization/src/Pareto.cpp',30,96),('optimization/src/Generator.cpp',39,94),
('dynamics/src/PowerEnergy.cpp',63,140),('kinematics/src/Envelope.cpp',127,216),
('modeling/src/XacroExpand.cpp',403,490),('execution/src/ChannelProtocol.cpp',236,318),
('project/src/Codec.cpp',160,255),('reporting/src/ReportCodec.cpp',33,130),
('testkit/src/JsonLite.cpp',80,164),('testkit/src/ProcessRunner.cpp',310,385),
('cmake/ird_gates.cmake',95,174),('trajectory/CMakeLists.txt',45,135),
('optimization/test/ParetoTest.cpp',30,115),('dynamics/test/StatisticsTest.cpp',1,130),
('modeling/test/XacroExpandTest.cpp',1,95)]
out=[]
for rel,a,b in sections:
 path=p+rel; lines=(repo/path).read_text(encoding='utf-8-sig').splitlines()
 out.append('\nFILE '+path+f' [{a}-{b}]\n'+'\n'.join(f'{i}: {lines[i-1]}' for i in range(a,min(b,len(lines))+1)))
text='\n'.join(out);(ev/'read-sections.log').write_text(text,encoding='utf-8');print(text)
