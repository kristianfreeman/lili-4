add_executable(lili_tests test_engine.cpp)
target_link_libraries(lili_tests PRIVATE lili::dsp)
add_test(NAME engine COMMAND lili_tests)

add_executable(lili_render render.cpp)
target_link_libraries(lili_render PRIVATE lili::dsp)

add_executable(lili_bench bench.cpp)
target_link_libraries(lili_bench PRIVATE lili::dsp)
