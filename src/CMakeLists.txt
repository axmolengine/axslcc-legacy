cmake_minimum_required(VERSION 3.23..4.1)

set(SOURCE_FILES "axslcc.cpp"
                 "axslc-spec.h"
                 "axslc-writer.h" 
                 "axslc-writer.cpp")
				 
add_executable(axslcc ${SOURCE_FILES})

target_link_libraries(axslcc PRIVATE 
        sx
        glslang
        glslang-default-resource-limits
        OSDependent
        SPIRV
        spirv-cross-core
        spirv-cross-cpp
        spirv-cross-glsl
        spirv-cross-hlsl
        spirv-cross-reflect
        spirv-cross-util
        spirv-cross-msl)  

if (MSVC)
	if (ENABLE_D3D11_COMPILER)
			add_definitions(-DD3D11_COMPILER)    
			target_link_libraries(axslcc PRIVATE d3dcompiler)    
	endif()
elseif (NOT WIN32)
	
endif()

# static linking
if (STATIC_LINK AND NOT MSVC)
	if (CMAKE_BUILD_TYPE STREQUAL Release)
		set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
		#if (APPLE)
		#	add_custom_command(TARGET axslcc POST_BUILD COMMAND strip -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/axslcc)
		#elseif (NOT WIN32)
		#	add_custom_command(TARGET axslcc POST_BUILD COMMAND strip -g -X -x ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/axslcc)
		#endif()
	endif()
endif()

install(TARGETS axslcc 
        CONFIGURATIONS Release
        RUNTIME DESTINATION bin)
