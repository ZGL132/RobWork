from pathlib import Path
p=Path('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/test/HostCompilePipelineTest.cpp')
s=p.read_text(encoding='utf-8-sig');pos=s.index('/** @brief MDL-17/AT-04：真实编译WC')
s=s[:pos]+'namespace sdurws::ird::ui::test {\nnamespace {\n'+s[pos:]+'\n} // namespace\n} // namespace sdurws::ird::ui::test\n'
s=s.replace('runtime::createHostPresentationView(snapshot);','runtime::createHostPresentationView(snapshot,m_query->view.id);')
s=s.replace('HostCompilePort port(m_rootId);ASSERT_TRUE(port.compileWorkCellAndDwc(makeCompileRequest()).published());','HostCompilePort port({m_rootId});\n    ASSERT_EQ(port.compileWorkCellAndDwc(makeCompileRequest()).status,project::CompileStatus::Published);')
s=s.replace('#include <cmath>','#include <cmath>\n#include <limits>\n#include <rw/kinematics/Kinematics.hpp>\n#include <rw/models/Device.hpp>\n#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>')
p.write_text(s,encoding='utf-8')
