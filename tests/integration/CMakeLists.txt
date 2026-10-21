cmake_minimum_required(VERSION 3.20)

# Firmware <-> host loopback test (M5): the STM32 protocol client
# (firmware/link/link_client.c) and the host HostController run against
# each other over a simulated USB wire, with a fake Windows audio system.
# Linux and Windows; no hardware.
project(integration_tests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

# protocol + host_core (the host's own tests are built too).
add_subdirectory(${REPO_DIR}/host host)

# The firmware's protocol client, compiled as in the firmware build.
add_library(link_client STATIC ${REPO_DIR}/firmware/link/link_client.c ${REPO_DIR}/firmware/link/dash_client.c)
target_include_directories(link_client PUBLIC ${REPO_DIR}/firmware/link ${REPO_DIR}/firmware/ui)
target_link_libraries(link_client PUBLIC protocol)
set_target_properties(link_client PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE host_core link_client)
if(MSVC)
    target_compile_options(link_client PRIVATE /W4 /WX)
    target_compile_options(test_integration PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(link_client PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
    target_compile_options(test_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME integration COMMAND test_integration)
