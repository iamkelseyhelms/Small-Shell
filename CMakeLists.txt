cmake_minimum_required(VERSION 3.6)
project(Small_Shell)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES main.c smallsh.c smallsh.h smallsh2.c)
add_executable(Small_Shell ${SOURCE_FILES})