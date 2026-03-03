cmake_minimum_required(VERSION 3.10)
project(memory_pool)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项：区分编译器
if(MSVC)
    add_compile_options(/W4 /utf-8)
    # /O2 与 Debug 模式的 /RTC1 冲突，仅在 Release 模式下启用优化
    add_compile_options($<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(-Wall -O2)
endif()

# 查找pthread库（Windows上 Threads::Threads 自动映射为 Win32 线程）
find_package(Threads REQUIRED)

# 设置目录
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INC_DIR ${CMAKE_SOURCE_DIR}/include)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/tests)

# 源文件
file(GLOB SOURCES "${SRC_DIR}/*.cpp")

# 添加头文件目录
include_directories(${INC_DIR})

# 示例目录
set(SAMPLE_DIR ${CMAKE_SOURCE_DIR}/sample)

# 创建单元测试可执行文件
add_executable(unit_test 
    ${SOURCES}
    ${TEST_DIR}/UnitTest.cpp
)

# 创建性能测试可执行文件
add_executable(perf_test
    ${SOURCES}
    ${TEST_DIR}/PerformanceTest.cpp
)

# 创建游戏引擎示例可执行文件
add_executable(game_engine_sample
    ${SOURCES}
    ${SAMPLE_DIR}/GameEngineSample.cpp
)

# 链接线程库
target_link_libraries(unit_test PRIVATE Threads::Threads)
target_link_libraries(perf_test PRIVATE Threads::Threads)
target_link_libraries(game_engine_sample PRIVATE Threads::Threads)

# 添加测试命令（使用生成器表达式获取正确的可执行文件路径，兼容 Windows/Linux）
add_custom_target(test
    COMMAND $<TARGET_FILE:unit_test>
    DEPENDS unit_test
)

add_custom_target(perf
    COMMAND $<TARGET_FILE:perf_test>
    DEPENDS perf_test
)