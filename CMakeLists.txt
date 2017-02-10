cmake_minimum_required (VERSION 2.6)
project (Thermo)
cmake_policy(SET CMP0015 NEW)
include_directories(SQLiteCpp/include)
link_directories(SQLiteCpp/build)

set (LIBS boost_system boost_iostreams c stdc++ pthread SQLiteCpp rt sqlite3)

add_executable (thermo_server server.cpp)
target_link_libraries(thermo_server ${LIBS})

add_executable (thermo_monitor monitor.cpp)
target_link_libraries(thermo_monitor ${LIBS})

add_executable (thermo_set set.cpp)
target_link_libraries(thermo_set ${LIBS})

add_executable (thermo_run run.cpp)
target_link_libraries(thermo_run ${LIBS})

add_executable (thermo_cancel cancel.cpp)
target_link_libraries(thermo_cancel ${LIBS})

add_executable (thermo_pause pause.cpp)
target_link_libraries(thermo_pause ${LIBS})

add_executable (thermo_resume resume.cpp)
target_link_libraries(thermo_resume ${LIBS})

add_executable (thermo_quit quit.cpp)
target_link_libraries(thermo_quit ${LIBS})

add_executable (status.cgi status.cgi.cpp)
target_link_libraries(status.cgi ${LIBS} cgicc jsoncpp)

add_executable (log.cgi log.cgi.cpp)
target_link_libraries(log.cgi ${LIBS} cgicc jsoncpp)

add_executable (programs.cgi programs.cgi.cpp)
target_link_libraries(programs.cgi ${LIBS} cgicc jsoncpp)

add_executable (program.cgi program.cgi.cpp)
target_link_libraries(program.cgi ${LIBS} cgicc jsoncpp)

add_executable (delete.cgi delete.cgi.cpp)
target_link_libraries(delete.cgi ${LIBS} cgicc jsoncpp)

add_executable (command.cgi command.cgi.cpp)
target_link_libraries(command.cgi ${LIBS} cgicc jsoncpp)
