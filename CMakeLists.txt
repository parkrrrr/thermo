cmake_minimum_required (VERSION 2.6)
project (Thermo)
set (LIBS boost_system boost_iostreams c stdc++ pthread SQLiteCpp rt sqlite3)

add_executable (server server.cpp)
target_link_libraries(server ${LIBS})

add_executable (monitor monitor.cpp)
target_link_libraries(monitor ${LIBS})

add_executable (set set.cpp)
target_link_libraries(set ${LIBS})

add_executable (run run.cpp)
target_link_libraries(run ${LIBS})

add_executable (cancel cancel.cpp)
target_link_libraries(cancel ${LIBS})

add_executable (pause pause.cpp)
target_link_libraries(pause ${LIBS})

add_executable (resume resume.cpp)
target_link_libraries(resume ${LIBS})

add_executable (quit quit.cpp)
target_link_libraries(quit ${LIBS})

add_executable (status.cgi status.cgi.cpp)
target_link_libraries(status.cgi ${LIBS} cgicc jsoncpp)

