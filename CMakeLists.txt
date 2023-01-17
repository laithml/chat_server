cmake_minimum_required(VERSION 3.23)
project(chat_server C)

set(CMAKE_C_STANDARD 99)

add_executable(chat_server chatServer.c chatServer.h)
