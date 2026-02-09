cmake_minimum_required(VERSION 3.10)
project(bmc_daemon C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# BMC Daemon executable
add_executable(bmc_daemon
    src/main.c
    src/bmc_state.c
    src/sensor.c
    src/pid_control.c
    src/event_log.c
    src/secure_boot.c
    src/ipmi.c
)

target_include_directories(bmc_daemon PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${JSONC_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(bmc_daemon
    ${JSONC_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    Threads::Threads
    m
)

# Install
install(TARGETS bmc_daemon DESTINATION bin)
