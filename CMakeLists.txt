cmake_minimum_required(VERSION 3.23..4.3)
project(axslcc)

 if (WIN32)
    set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

function(remove_compile_options DEST_VAR COMPILER_FLAGS FLAGS)
        separate_arguments(FLAGS)
        foreach(FLAG ${FLAGS})
            string(REPLACE "${FLAG}" "" COMPILER_FLAGS "${COMPILER_FLAGS}")
        endforeach()
        set(${DEST_VAR} ${COMPILER_FLAGS} PARENT_SCOPE)
endfunction()

if(WIN32)
    option(STATIC_LINK "Statically link with system standard libraries. Affects release build only" ON)
else()
    option(STATIC_LINK "Statically link with system standard libraries. Affects release build only" OFF)
endif()
option(ENABLE_SPVREMAPPER "Enables building of SPVRemapper" ON)
option(ENABLE_AMD_EXTENSIONS "Enables support of AMD-specific extensions" ON)
option(ENABLE_GLSLANG_BINARIES "Builds glslangValidator and spirv-remap" ON)
option(ENABLE_NV_EXTENSIONS "Enables support of Nvidia-specific extensions" ON)
option(ENABLE_HLSL "Enables HLSL input support" ON)
option(ENABLE_OPT "Enables spirv-opt capability if present" ON)

option(USE_CCACHE "Use ccache" OFF)
if (WIN32)
    option(ENABLE_D3D11_COMPILER "Use Direct3D11 compiler (d3dcompiler.lib + d3dcompiler_47.dll)" ON)
endif()

set(SX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

if(ENABLE_AMD_EXTENSIONS)
    add_definitions(-DAMD_EXTENSIONS)
endif(ENABLE_AMD_EXTENSIONS)

if(ENABLE_NV_EXTENSIONS)
    add_definitions(-DNV_EXTENSIONS)
endif(ENABLE_NV_EXTENSIONS)

if(ENABLE_HLSL)
    add_definitions(-DENABLE_HLSL)
endif(ENABLE_HLSL)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

if (MSVC)
	remove_compile_options(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" "/GR")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /wd4530")
	
	if (STATIC_LINK)
		remove_compile_options(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}" "/MD")
		remove_compile_options(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}" "/MD")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
	endif()
endif()

set(ENABLE_CTEST FALSE CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

if (NOT (IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang"))
    message(STATUS "Downloading glslang ...")
    find_program(GIT_COMMAND NAMES git REQUIRED)
    execute_process(COMMAND ${GIT_COMMAND} clone "https://github.com/KhronosGroup/glslang.git" "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang")
    execute_process(COMMAND ${GIT_COMMAND} -C "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang" checkout "16.2.0")
    find_program(PYTHON_COMMAND NAMES python3 python REQUIRED)
    execute_process(COMMAND ${PYTHON_COMMAND} "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang/update_glslang_sources.py" 
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang")
    set(GLSLANG_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/3rdparty/glslang")
    set(GLSLANG_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/glslang.patch")
    file(TO_CMAKE_PATH "${GLSLANG_SOURCE_DIR}" GLSLANG_SOURCE_DIR)
    file(TO_CMAKE_PATH "${GLSLANG_PATCH_FILE}" GLSLANG_PATCH_FILE)
    message(STATUS "Apply ${GLSLANG_PATCH_FILE} to ${GLSLANG_SOURCE_DIR} ...")
    execute_process(COMMAND ${GIT_COMMAND} -C "${GLSLANG_SOURCE_DIR}" apply --verbose --ignore-whitespace "${GLSLANG_PATCH_FILE}")
endif()
    
add_subdirectory(3rdparty/glslang)
add_subdirectory(3rdparty/spirv-cross)
add_subdirectory(3rdparty/sx)
add_subdirectory(src)


# ------------------------------------------------------------------------------
# Helper Function: get_all_targets
# Description: 
#   A helper function to collect all targets (including those in subdirectories)
#   into a list variable. This solves the limitation of CMake's native target listing.
# Parameters:
#   result - Output variable name to store the list of all targets
# ------------------------------------------------------------------------------
function(get_all_targets result)
    # Initialize an empty list to store targets
    set(targets "")
    
    # Define a recursive function to collect targets from all directories
    function(_get_all_targets_recursive dir)
        # Get targets in the current directory
        get_property(current_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
        
        # Add current directory's targets to the global list
        list(APPEND targets ${current_targets})
        
        # Get subdirectories of the current directory
        get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
        
        # Recursively process all subdirectories
        foreach(subdir IN LISTS subdirs)
            _get_all_targets_recursive(${subdir})
        endforeach()
        
        # Update the global targets list (PARENT_SCOPE to pass up)
        set(targets ${targets} PARENT_SCOPE)
    endfunction()

    # Start recursive collection from the top-level source directory
    _get_all_targets_recursive(${CMAKE_SOURCE_DIR})
    
    # Pass the collected targets to the output variable
    set(${result} ${targets} PARENT_SCOPE)
endfunction()

 # Get the list of all targets defined in the current CMake context
get_all_targets(all_targets)

# Iterate through each target in the collected list
foreach(target IN LISTS all_targets)
    # Check if the target is an IMPORTED target (external dependency)
    get_target_property(is_imported ${target} IMPORTED)
    
    # Skip imported targets to prevent modifying external libraries
    if(NOT is_imported)
        # Set C++ standard to 20 for the target (C++20)
        set_target_properties(${target} PROPERTIES
            C_STANDARD ${CMAKE_C_STANDARD}
            CXX_STANDARD ${CMAKE_CXX_STANDARD}
            # Enforce the standard (disable compiler-specific extensions)
            CXX_STANDARD_REQUIRED ON
        )
        
        # Optional: Print log to confirm the setting (comment out if not needed)
        message(STATUS "Set C++${CMAKE_CXX_STANDARD} standard for target: ${target}")
    endif()
endforeach()

if(MSVC_IDE)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT axslcc)
endif()
