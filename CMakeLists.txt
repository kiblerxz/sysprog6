cmake_minimum_required(VERSION 3.15)
project(lab6_threads C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)       # требование методички
link_libraries(Threads::Threads)     # подключаем pthread'ы

add_executable(find_one  src/find_one.c)
add_executable(find_all  src/find_all.c)
