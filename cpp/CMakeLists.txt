# The C++ core, and the GUI that plays it.
#
# The core is an incremental port graded against the Python game, which
# remains the reference. The GUI (built when SDL2 is available) runs the
# graded sim with Dear ImGui screens on top; nothing about the game itself
# is decided outside the core.
cmake_minimum_required(VERSION 3.16)
project(forcetris_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Warnings, in each compiler's own spelling: MSVC does not speak -W flags and
# answers them with a warning of its own, which is noise on a Windows build.
if (MSVC)
	set(FORCETRIS_WARNINGS /W3)
else()
	set(FORCETRIS_WARNINGS -Wall -Wextra -Wpedantic)
endif()

add_library(forcetris_core
	src/piece.cpp
	src/board.cpp
	src/kicks.cpp
	src/finesse.cpp
	src/attack.cpp
	src/spins.cpp
	src/sim.cpp
	src/replay.cpp
	src/hiscore.cpp
	src/rating.cpp
	src/bot.cpp
	src/profile.cpp
	src/career.cpp
	src/munch.cpp
)
target_include_directories(forcetris_core PUBLIC include)
# nlohmann/json.hpp, vendored, for the replay files.
target_include_directories(forcetris_core SYSTEM PRIVATE third_party)
target_compile_options(forcetris_core PRIVATE ${FORCETRIS_WARNINGS})

# The Android build: the same game sources built as the shared library
# SDLActivity loads. Everything desktop - the tests, the window binary -
# is skipped; the phone gets the game and nothing else.
if (ANDROID)
	find_package(SDL2 REQUIRED)
	add_library(imgui STATIC
		third_party/imgui/imgui.cpp
		third_party/imgui/imgui_draw.cpp
		third_party/imgui/imgui_tables.cpp
		third_party/imgui/imgui_widgets.cpp
		third_party/imgui/backends/imgui_impl_sdl2.cpp
		third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
	)
	target_include_directories(imgui PUBLIC
		third_party/imgui third_party/imgui/backends)
	target_link_libraries(imgui PUBLIC SDL2::SDL2)
	add_library(main SHARED
		gui/main.cpp
		gui/session.cpp
		gui/stats.cpp
		gui/config.cpp
		gui/audio.cpp
		gui/versus.cpp
	)
	target_link_libraries(main PRIVATE forcetris_core imgui SDL2::SDL2)
	target_include_directories(main SYSTEM PRIVATE third_party/stb)
	target_compile_options(main PRIVATE ${FORCETRIS_WARNINGS})
	return()
endif()

add_executable(equivalence tests/equivalence.cpp)
target_link_libraries(equivalence PRIVATE forcetris_core)
target_compile_options(equivalence PRIVATE ${FORCETRIS_WARNINGS})

add_executable(trace tests/trace.cpp)
target_link_libraries(trace PRIVATE forcetris_core)
target_compile_options(trace PRIVATE ${FORCETRIS_WARNINGS})

add_executable(replaycheck tests/replaycheck.cpp)
target_link_libraries(replaycheck PRIVATE forcetris_core)
target_compile_options(replaycheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(hiscorecheck tests/hiscorecheck.cpp)
target_link_libraries(hiscorecheck PRIVATE forcetris_core)
target_compile_options(hiscorecheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(cascadecheck tests/cascadecheck.cpp)
target_link_libraries(cascadecheck PRIVATE forcetris_core)
target_compile_options(cascadecheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(ratingcheck tests/ratingcheck.cpp)
target_link_libraries(ratingcheck PRIVATE forcetris_core)
target_compile_options(ratingcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(cheesecheck tests/cheesecheck.cpp)
target_link_libraries(cheesecheck PRIVATE forcetris_core)
target_compile_options(cheesecheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(versuscheck tests/versuscheck.cpp)
target_link_libraries(versuscheck PRIVATE forcetris_core)
target_compile_options(versuscheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(botcheck tests/botcheck.cpp)
target_link_libraries(botcheck PRIVATE forcetris_core)
target_compile_options(botcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(opponentcheck tests/opponentcheck.cpp)
target_link_libraries(opponentcheck PRIVATE forcetris_core)
target_compile_options(opponentcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(clearcheck tests/clearcheck.cpp)
target_link_libraries(clearcheck PRIVATE forcetris_core)
target_compile_options(clearcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(careercheck tests/careercheck.cpp)
target_link_libraries(careercheck PRIVATE forcetris_core)
target_compile_options(careercheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(fusecheck tests/fusecheck.cpp)
target_link_libraries(fusecheck PRIVATE forcetris_core)
target_compile_options(fusecheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(inputcheck tests/inputcheck.cpp gui/session.cpp)
target_link_libraries(inputcheck PRIVATE forcetris_core)
target_compile_options(inputcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(munchcheck tests/munchcheck.cpp)
target_link_libraries(munchcheck PRIVATE forcetris_core)
target_compile_options(munchcheck PRIVATE ${FORCETRIS_WARNINGS})

add_executable(profilecheck tests/profilecheck.cpp)
target_link_libraries(profilecheck PRIVATE forcetris_core)
target_compile_options(profilecheck PRIVATE ${FORCETRIS_WARNINGS})

# The game window needs SDL2; everything else builds without it, so a machine
# that only wants the graded core (or has no SDL2 yet) still gets the tests.
find_package(SDL2 QUIET)
if (SDL2_FOUND)
	add_library(imgui STATIC
		third_party/imgui/imgui.cpp
		third_party/imgui/imgui_draw.cpp
		third_party/imgui/imgui_tables.cpp
		third_party/imgui/imgui_widgets.cpp
		third_party/imgui/backends/imgui_impl_sdl2.cpp
		third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
	)
	target_include_directories(imgui PUBLIC
		third_party/imgui third_party/imgui/backends)
	if (TARGET SDL2::SDL2)
		target_link_libraries(imgui PUBLIC SDL2::SDL2)
	else()
		target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
		target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES})
	endif()

	add_executable(forcetris
		gui/main.cpp
		gui/session.cpp
		gui/stats.cpp
		gui/config.cpp
		gui/audio.cpp
		gui/versus.cpp
	)
	target_link_libraries(forcetris PRIVATE forcetris_core imgui)
	# stb_vorbis, vendored, for the music.
	target_include_directories(forcetris SYSTEM PRIVATE third_party/stb)
	target_compile_options(forcetris PRIVATE ${FORCETRIS_WARNINGS})
	if (TARGET SDL2::SDL2main)
		target_link_libraries(forcetris PRIVATE SDL2::SDL2main)
	endif()
else()
	message(STATUS "SDL2 not found - building the core and tests only")
endif()

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
# A replay file written by either engine has to mean the same thing to the
# other; the two dump the same file field by field and must agree.
add_test(NAME replay_cross
	COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay_cross.py
		$<TARGET_FILE:replaycheck>)
# The same submissions through both engines' codecs must produce the same
# hiscore.dat, byte for byte, and announce the same places.
add_test(NAME hiscore_cross
	COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../tools/hiscore_cross.py
		$<TARGET_FILE:hiscorecheck>)
# The cascade movement loop's collision verdicts, on the synthetic boards
# tools/test_cascade.py pins the Python engine with - the one corner no
# natural game, and so no trace, can reach.
add_test(NAME cascade_check COMMAND cascadecheck)
# The rating estimate's arithmetic: the official TR conversion's fixed
# points, monotonicity, and the rank ladder's order.
add_test(NAME rating_check COMMAND ratingcheck)
# The cheese modes, which are this side's own: the race's priming, cap,
# quota and win, and survival's rising floor, spelled out against the sim.
add_test(NAME cheese_check COMMAND cheesecheck)
# The versus wire: queued attack rising on quiet locks, cancellation, the
# eight-row lift, and the surge bank - this side's own rules, spelled out.
add_test(NAME versus_check COMMAND versuscheck)
# The opponent: the planner reaches tucks and spins, plays what it plans,
# survives, digs, and repeats itself under a seed.
add_test(NAME bot_check COMMAND botcheck)
# The embedded opponent in a versus replay: round trip, absence, and the
# tolerant reading of files that only half-have one.
add_test(NAME opponent_check COMMAND opponentcheck)
# The clear delay knob: the reference timing with it on, lock-frame
# resolution with it off, and an outcome identical either way.
add_test(NAME clear_check COMMAND clearcheck)
add_test(NAME input_check COMMAND inputcheck)
add_test(NAME fuse_check COMMAND fusecheck)
add_test(NAME career_check COMMAND careercheck)
# The munch: minomuncher's statistics re-derived over our own records,
# pinned on hand-built games.
add_test(NAME munch_check COMMAND munchcheck)
# The profile history: append, load, order, and the tolerance that lets
# newer builds add keys.
add_test(NAME profile_check COMMAND profilecheck)
if (SDL2_FOUND)
	# The whole window run headlessly under scripted-random input: proves the
	# loop, the drawing and the shutdown hold together with no display.
	add_test(NAME gui_smoke COMMAND forcetris)
	set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT
		"SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;FORCETRIS_SMOKE=1500;FORCETRIS_GUI_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/smoke-gui.cfg;FORCETRIS_REPLAYS=${CMAKE_CURRENT_BINARY_DIR}/smoke-replays;FORCETRIS_HISCORE=${CMAKE_CURRENT_BINARY_DIR};FORCETRIS_PROFILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-profile.dat;FORCETRIS_CAREER=${CMAKE_CURRENT_BINARY_DIR}/smoke-career.dat")
	# The same run in versus mode, ending in the viewer: proves the round is
	# recorded with the bot's side embedded and the two-board viewer draws.
	add_test(NAME gui_smoke_versus COMMAND forcetris)
	set_tests_properties(gui_smoke_versus PROPERTIES ENVIRONMENT
		"SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;FORCETRIS_SMOKE=1500;FORCETRIS_SMOKE_MODE=5;FORCETRIS_SMOKE_VIEW=1;FORCETRIS_GUI_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/smoke-versus-gui.cfg;FORCETRIS_REPLAYS=${CMAKE_CURRENT_BINARY_DIR}/smoke-versus-replays;FORCETRIS_HISCORE=${CMAKE_CURRENT_BINARY_DIR};FORCETRIS_PROFILE=${CMAKE_CURRENT_BINARY_DIR}/smoke-profile.dat;FORCETRIS_CAREER=${CMAKE_CURRENT_BINARY_DIR}/smoke-career.dat")
endif()
