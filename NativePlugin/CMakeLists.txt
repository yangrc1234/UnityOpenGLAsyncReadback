cmake_minimum_required(VERSION 3.2)
PROJECT(AsyncGPUReadbackPlugin)
set (CMAKE_CXX_STANDARD 11)

file(GLOB_RECURSE HPPS src/*.hpp)
file(GLOB_RECURSE HEADERS src/*.h)
file(GLOB_RECURSE SOURCES src/*.cpp)

find_package(OpenGL REQUIRED)
include_directories(${OpenGL_INCLUDE_DIR})

#GLEW
file(GLOB_RECURSE GLEW_SOURCES glew/src/*.c)
add_library(GLEW ${GLEW_SOURCES})
target_compile_definitions(GLEW PUBLIC -D GLEW_STATIC)
set_property(TARGET GLEW PROPERTY POSITION_INDEPENDENT_CODE ON)
include_directories(glew/include)

source_group("Source Files" FILES ${SOURCES} ${HEADERS} ${HPPS})
include_directories("/")

add_library(${PROJECT_NAME} SHARED ${HEADERS} ${HPPS} ${SOURCES} )
target_compile_definitions(${PROJECT_NAME} PUBLIC -D GLEW_STATIC)
target_link_libraries(${PROJECT_NAME} ${OPENGL_LIBRARY} GLEW)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -Werror)
endif()