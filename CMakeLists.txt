cmake_minimum_required (VERSION 2.6)
project (Thermo)
set (LIBS boost_system boost_iostreams c stdc++ pthread SQLiteCpp rt)

add_executable (server server.cpp)
target_link_libraries(server ${LIBS})

add_executable (monitor monitor.cpp)
target_link_libraries(monitor ${LIBS})

add_executable (set set.cpp)
target_link_libraries(set ${LIBS})

