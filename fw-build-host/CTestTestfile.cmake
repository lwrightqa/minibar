# CMake generated Testfile for 
# Source directory: /home/user/tinybar/firmware/test/host
# Build directory: /home/user/tinybar/fw-build-host
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(core "/home/user/tinybar/fw-build-host/test_core")
set_tests_properties(core PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/core/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;54;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
add_test(calendar "/home/user/tinybar/fw-build-host/test_calendar")
set_tests_properties(calendar PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/calendar/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;55;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
add_test(net "/home/user/tinybar/fw-build-host/test_net")
set_tests_properties(net PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/net/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;56;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
add_test(jira "/home/user/tinybar/fw-build-host/test_jira")
set_tests_properties(jira PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/jira/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;57;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
add_test(ui "/home/user/tinybar/fw-build-host/test_ui")
set_tests_properties(ui PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/ui/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;58;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
add_test(board "/home/user/tinybar/fw-build-host/test_board")
set_tests_properties(board PROPERTIES  ENVIRONMENT "TB_FIXTURES=/home/user/tinybar/firmware/test/host/board/fixtures" _BACKTRACE_TRIPLES "/home/user/tinybar/firmware/test/host/CMakeLists.txt;47;add_test;/home/user/tinybar/firmware/test/host/CMakeLists.txt;59;tb_test_module;/home/user/tinybar/firmware/test/host/CMakeLists.txt;0;")
subdirs("core")
subdirs("calendar")
subdirs("jira")
subdirs("net")
subdirs("ui")
subdirs("board")
