cmake_minimum_required(VERSION 3.13)
project(fw2_voidpanzer_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

# Host-only: game rules have no Pico SDK or hardware dependency.
add_executable(test_vp_game
    test_vp_game.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/vp_game.c)
target_include_directories(test_vp_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
if(NOT MSVC)
    target_link_libraries(test_vp_game m)
endif()
add_test(NAME vp_game COMMAND test_vp_game)
