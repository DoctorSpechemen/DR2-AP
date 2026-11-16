cmake_minimum_required(VERSION 3.20)

# Link the C++ runtime into the plugin, so players do not need a Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(DR2AP LANGUAGES C CXX)

# The loader: DR2AP.asi. Small and rarely changed.
add_library(DR2AP SHARED "Loader.cpp")
target_compile_definitions(DR2AP PRIVATE UNICODE _UNICODE)
set_target_properties(DR2AP PROPERTIES CXX_STANDARD 17 PREFIX "" SUFFIX ".asi")

# The core: DR2AP_core.dll. This is the part that changes, and it can be reloaded while the game runs.
# Zydis (a disassembler) comes from the copy bundled with safetyhook in the fix's source tree.
# Look for it next to this folder, in either of the two places the fix's source may have been extracted to.
set(ZYDIS_DIR "")
foreach(candidate
	"${CMAKE_CURRENT_SOURCE_DIR}/../DR2Fix-source/Includes/safetyhook"
	"${CMAKE_CURRENT_SOURCE_DIR}/../Includes/safetyhook")
	if(ZYDIS_DIR STREQUAL "" AND EXISTS "${candidate}/Zydis.c")
		set(ZYDIS_DIR "${candidate}")
	endif()
endforeach()
if(ZYDIS_DIR STREQUAL "")
	message(FATAL_ERROR "Could not find Zydis.c. Expected the fix's source (with its Includes folder) next to the DR2AP folder.")
endif()

# safetyhook (a hooking library) lives in the same folder and needs Zydis.
add_library(DR2AP_core SHARED
	"Core.cpp"
	"${ZYDIS_DIR}/Zydis.c"
	"${ZYDIS_DIR}/safetyhook.cpp"
)
target_include_directories(DR2AP_core PRIVATE "${ZYDIS_DIR}")
# winmm is the Windows sound library, used to play the message chime.
target_link_libraries(DR2AP_core PRIVATE winmm)
target_compile_definitions(DR2AP_core PRIVATE UNICODE _UNICODE)
# safetyhook needs the newest C++ standard, the same setting the fix itself builds with.
set_target_properties(DR2AP_core PROPERTIES CXX_STANDARD 23 PREFIX "" SUFFIX ".dll")
