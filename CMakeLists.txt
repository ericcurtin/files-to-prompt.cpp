cmake_minimum_required(VERSION 3.10)
project(files-to-prompt.cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(files-to-prompt.cpp main.cpp)

# Add install rules
install(TARGETS files-to-prompt.cpp
        RUNTIME DESTINATION bin)
