cmake_minimum_required(VERSION 3.20)

# Host build of the STM32 protocol client and its unit tests. The firmware
# compiles link_client.c directly (firmware/STM32AudioController/CMakeLists.txt).
project(link_client_tests LANGUAGES C)

set(PROTOCOL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../protocol)
set(UI_DIR ${CMAKE_CURRENT_LIST_DIR}/../ui)

add_library(link_client STATIC
    link_client.c
    dash_client.c
    ${PROTOCOL_DIR}/src/protocol.c)
target_include_directories(link_client PUBLIC . ${PROTOCOL_DIR}/include ${UI_DIR})
set_target_properties(link_client PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_options(link_client PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_link tests/test_link.c)
    target_link_libraries(test_link PRIVATE link_client)
    set_target_properties(test_link PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    target_compile_options(test_link PRIVATE -Wall -Wextra -Werror)
    add_test(NAME link COMMAND test_link)

    add_executable(test_dash tests/test_dash.c)
    target_link_libraries(test_dash PRIVATE link_client)
    set_target_properties(test_dash PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    target_compile_options(test_dash PRIVATE -Wall -Wextra -Werror)
    add_test(NAME dash COMMAND test_dash)
endif()
