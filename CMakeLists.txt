cmake_minimum_required(VERSION 3.5.0)
project(Veitor VERSION 0.1.0 LANGUAGES C CXX)

# set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")  # cmake 采用静态编译

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin) # 生成的二进制文件目录

link_directories(${PROJECT_SOURCE_DIR}/lib)  # 指定库文件目录

set(SOURCES
       src/vorpal.c
       # src/test.c
)

add_executable(Veitor ${SOURCES})

# target_link_libraries(Veitor easyx)  # 链接导入库

# target_include_directories(Veitor  # 头文件
#         PRIVATE
#         ${PROJECT_SOURCE_DIR}/include
#         )