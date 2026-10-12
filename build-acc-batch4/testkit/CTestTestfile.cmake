# CMake generated Testfile for 
# Source directory: D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit
# Build directory: D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/testkit
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(sdurws_ird_testkit_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/testkit/Debug/sdurws_ird_testkit_test.exe")
  set_tests_properties(sdurws_ird_testkit_test PROPERTIES  _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;164;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;177;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(sdurws_ird_testkit_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/testkit/Release/sdurws_ird_testkit_test.exe")
  set_tests_properties(sdurws_ird_testkit_test PROPERTIES  _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;164;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;177;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(sdurws_ird_testkit_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/testkit/MinSizeRel/sdurws_ird_testkit_test.exe")
  set_tests_properties(sdurws_ird_testkit_test PROPERTIES  _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;164;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;177;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(sdurws_ird_testkit_test "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/build-acc-batch4/testkit/RelWithDebInfo/sdurws_ird_testkit_test.exe")
  set_tests_properties(sdurws_ird_testkit_test PROPERTIES  _BACKTRACE_TRIPLES "D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;164;add_test;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;177;ird_add_gtest;D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt4/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt;0;")
else()
  add_test(sdurws_ird_testkit_test NOT_AVAILABLE)
endif()
