cmake_minimum_required(VERSION 3.23)
project(luggage LANGUAGES CXX) 

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#-------- LUGGAGE IMPLEMENTATION --------
add_library(luggage_core)
target_sources(luggage_core
    PRIVATE
        src/lug_auxlib.cpp
        src/lug_baselib.cpp
        src/lug_mathlib.cpp
        src/lug_colllib.cpp
        src/lug_stdlib.cpp
        src/lug_strlib.cpp
        
        src/lug_chunk.cpp
        src/lug_compiler.cpp
        src/lug_debug.cpp
        src/lug_lexer.cpp
        src/lug_memory.cpp
        src/lug_object.cpp
        src/lug_opcodes.cpp
        src/lug_parser.cpp
        src/lug_table.cpp
        src/lug_value.cpp
        src/lug_vm.cpp
    PUBLIC 
        FILE_SET HEADERS
        BASE_DIRS include
        FILES 
            include/luggage.hpp
)


#-------- LUGGAGE MAIN EXE --------
add_executable(luggage)
target_sources(luggage
    PRIVATE 
        main.cpp    
)

target_link_libraries(luggage PRIVATE luggage_core)

#-------- TESTS --------
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()