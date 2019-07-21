cmake_minimum_required(VERSION 3.2)
PROJECT(AsyncGPUReadbackPlugin)
file(GLOB_RECURSE HPPS src/*.hpp)
file(GLOB_RECURSE HEADERS src/*.h)
file(GLOB_RECURSE SOURCES src/*.cpp)

find_package(OpenGL REQUIRED)
include_directories(${OpenGL_INCLUDE_DIR})

#GLEW
file(GLOB_RECURSE GLEW_SOURCES glew/src/*.c)
add_library(GLEW ${GLEW_SOURCES})
target_compile_definitions(GLEW PUBLIC -D GLEW_STATIC)
include_directories(glew/include)

source_group("Source Files" FILES ${SOURCES} ${HEADERS} ${HPPS})
include_directories("/")

add_library(${PROJECT_NAME} SHARED ${HEADERS} ${HPPS} ${SOURCES} )
target_compile_definitions(${PROJECT_NAME} PUBLIC -D GLEW_STATIC)
target_link_libraries(${PROJECT_NAME} ${OPENGL_LIBRARY} GLEW)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

