# CMake generated Testfile for 
# Source directory: D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime
# Build directory: D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/runtime
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(sdurws_ird_runtime_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/runtime/Debug/sdurws_ird_runtime_test.exe")
  set_tests_properties(sdurws_ird_runtime_test PROPERTIES  LABELS "ird" _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;341;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;354;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(sdurws_ird_runtime_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/runtime/Release/sdurws_ird_runtime_test.exe")
  set_tests_properties(sdurws_ird_runtime_test PROPERTIES  LABELS "ird" _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;341;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;354;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(sdurws_ird_runtime_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/runtime/MinSizeRel/sdurws_ird_runtime_test.exe")
  set_tests_properties(sdurws_ird_runtime_test PROPERTIES  LABELS "ird" _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;341;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;354;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(sdurws_ird_runtime_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/runtime/RelWithDebInfo/sdurws_ird_runtime_test.exe")
  set_tests_properties(sdurws_ird_runtime_test PROPERTIES  LABELS "ird" _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;341;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;354;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/CMakeLists.txt;0;")
else()
  add_test(sdurws_ird_runtime_test NOT_AVAILABLE)
endif()
