# The C++ core: the parts of Forcetris that are pure logic.
#
# This is the first step of an incremental port. The Python game is still the
# one you play; this exists to be graded against it, so that when the rest
# follows there is something underneath that is known to behave identically.
cmake_minimum_required(VERSION 3.16)
project(forcetris_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

add_library(forcetris_core
	src/piece.cpp
	src/board.cpp
	src/kicks.cpp
	src/finesse.cpp
	src/attack.cpp
	src/spins.cpp
	src/sim.cpp
)
target_include_directories(forcetris_core PUBLIC include)
target_compile_options(forcetris_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(equivalence tests/equivalence.cpp)
target_link_libraries(equivalence PRIVATE forcetris_core)
target_compile_options(equivalence PRIVATE -Wall -Wextra -Wpedantic)

add_executable(trace tests/trace.cpp)
target_link_libraries(trace PRIVATE forcetris_core)
target_compile_options(trace PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
# The reference is generated rather than committed, so the check is always
# against the Python engine as it stands rather than as it once was.
add_test(NAME equivalence
	COMMAND ${CMAKE_COMMAND}
		-DPYTHON=python3
		-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
		-DBINARY=$<TARGET_FILE:equivalence>
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_equivalence.cmake)
add_test(NAME trace
	COMMAND ${CMAKE_COMMAND}
		-DPYTHON=python3
		-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
		-DBINARY=$<TARGET_FILE:trace>
		-DDUMPER=dump_trace.py
		-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_equivalence.cmake)
