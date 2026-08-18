# Dump what the Python engine does, then grade the C++ side against it.
if (NOT DUMPER)
	set(DUMPER dump_reference.py)
endif()
set(REFERENCE "${CMAKE_CURRENT_BINARY_DIR}/${DUMPER}.out")
execute_process(
	COMMAND "${PYTHON}" "${ROOT}/tools/${DUMPER}" "${REFERENCE}"
	RESULT_VARIABLE dumped
	OUTPUT_VARIABLE dump_out
	ERROR_VARIABLE dump_err)
if (NOT dumped EQUAL 0)
	message(FATAL_ERROR "could not dump the reference:\n${dump_out}${dump_err}")
endif()
execute_process(
	COMMAND "${BINARY}" "${REFERENCE}"
	RESULT_VARIABLE graded)
if (NOT graded EQUAL 0)
	message(FATAL_ERROR "the C++ core disagrees with the Python engine")
endif()
