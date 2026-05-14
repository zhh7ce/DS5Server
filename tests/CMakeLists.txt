# 查找 Google Test
find_package(GTest REQUIRED)

# 添加测试可执行文件
add_executable(ds5server_tests
    main_test.cpp
    test_audio_encoder.cpp
    test_audio_encoder_resample.cpp
    ${CMAKE_SOURCE_DIR}/src/audio_encoder.cpp
    ${CMAKE_SOURCE_DIR}/src/opus_output.cpp
)

# 包含目录
target_include_directories(ds5server_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PIPEWIRE_INCLUDE_DIRS}
    ${SPA_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
    ${SAMPLERATE_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(ds5server_tests PRIVATE
    GTest::gtest_main
    ${PIPEWIRE_LIBRARIES}
    ${SPA_LIBRARIES}
    ${OPUS_LIBRARIES}
    ${SAMPLERATE_LIBRARIES}
    pthread
)

# 编译选项
target_compile_options(ds5server_tests PRIVATE 
    -Wall 
    -Wextra 
    -Wpedantic
    -g
    -O0
)

# 注册测试
enable_testing()
add_test(NAME ds5server_tests COMMAND ds5server_tests)
