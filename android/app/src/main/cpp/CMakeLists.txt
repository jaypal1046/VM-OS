cmake_minimum_required(VERSION 3.22.1)
project("vmcore" C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/display
    ${CMAKE_CURRENT_SOURCE_DIR}/serial
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_core
)

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/jni_bindings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/display/display_bridge.c
    ${CMAKE_CURRENT_SOURCE_DIR}/serial/uart_ipc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/vm_core/vm_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/os_compositor.c
)

add_library(vmcore SHARED ${SOURCES})

find_library(log-lib log)
find_library(android-lib android)
find_library(jnigraphics-lib jnigraphics)

target_link_libraries(vmcore
    ${android-lib}
    ${log-lib}
    ${jnigraphics-lib}
)
