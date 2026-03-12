cmake_minimum_required(VERSION 3.16)

# ============================================
# Project Definition
# ============================================
# For interview: "I use modern CMake practices - target-based configuration,
# generator expressions, and proper dependency management. This ensures
# the build is reproducible across different environments."

project(skyguard
    VERSION 1.0.0
    DESCRIPTION "Real-Time Flight Monitoring & Conflict Detection System"
    LANGUAGES CXX
)

# Prevent in-source builds (best practice)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -B build")
endif()

# ============================================
# Build Configuration
# ============================================

# Require C++17 for std::optional, std::shared_mutex, structured bindings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE/tooling support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif()

# ============================================
# Compiler Detection & Platform-Specific Config
# ============================================
# For interview: "I handle cross-platform differences at the build level,
# not scattered through the code. This is critical for Red Hat compatibility."

message(STATUS "")
message(STATUS "========== SkyGuard Build Configuration ==========")
message(STATUS "Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler:   ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "System:         ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "==================================================")
message(STATUS "")

# Strict compiler warnings - safety-critical code demands zero warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall           # All standard warnings
        -Wextra         # Extra warnings
        -Wpedantic      # Strict ISO compliance
        -Wconversion    # Warn on implicit type conversions
        -Wshadow        # Warn on variable shadowing
        -Wcast-align    # Warn on potentially unsafe casts
        -Wformat=2      # Printf format string checks
        -Wnull-dereference  # Warn on null pointer dereference
    )

    # Treat warnings as errors in CI/Release
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR DEFINED ENV{CI})
        add_compile_options(-Werror)
    endif()

    # Position-independent code (required for shared libraries)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# MSVC-specific (for potential Windows port)
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# ============================================
# Dependencies
# ============================================

# Threading - required for worker pool
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# OpenSSL - required for WebSocket handshake (SHA-1, Base64)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL Version: ${OPENSSL_VERSION}")
endif()

# ============================================
# Source Files
# ============================================

set(SKYGUARD_SOURCES
    src/main.cpp
    src/radar_receiver.cpp
    src/processing_engine.cpp
    src/ai_validator.cpp
    src/websocket_server.cpp
)

set(SKYGUARD_HEADERS
    src/types.hpp
    src/flight_packet.hpp
    src/packet_queue.hpp
    src/radar_receiver.hpp
    src/processing_engine.hpp
    src/ai_validator.hpp
    src/websocket_server.hpp
    src/logger.hpp
)

# ============================================
# Main Executable
# ============================================

add_executable(skyguard ${SKYGUARD_SOURCES} ${SKYGUARD_HEADERS})

# Modern CMake: Use target properties, not global settings
target_include_directories(skyguard
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(skyguard
    PRIVATE
        Threads::Threads
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Linux-specific: Real-time extensions library
# For interview: "On Linux, I link against librt for real-time scheduling
# APIs like sched_setscheduler. This enables SCHED_FIFO priority."
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(skyguard PRIVATE rt)

    # Enable real-time capabilities
    target_compile_definitions(skyguard PRIVATE SKYGUARD_LINUX_RT)
endif()

# macOS-specific adjustments
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # macOS doesn't have SCHED_FIFO, use different priority mechanism
    target_compile_definitions(skyguard PRIVATE SKYGUARD_DARWIN)
endif()

# ============================================
# Build Type Configurations
# ============================================

# Debug: Sanitizers for catching memory bugs
# For interview: "In debug builds, I enable AddressSanitizer and
# UndefinedBehaviorSanitizer. These catch buffer overflows, use-after-free,
# and undefined behavior before they become production incidents."
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(skyguard PRIVATE
        $<$<CONFIG:Debug>:-g -O0 -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g -DNDEBUG>
    )

    target_link_options(skyguard PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
endif()

# ============================================
# Installation Rules
# ============================================
# For interview: "I define proper install rules so the system can be
# packaged for deployment. This integrates with systemd service files."

include(GNUInstallDirs)

install(TARGETS skyguard
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install systemd service file (Linux only)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/deploy/skyguard.service
        DESTINATION /etc/systemd/system
        COMPONENT config
    )
endif()

# ============================================
# CPack Configuration (for distribution)
# ============================================

set(CPACK_PACKAGE_NAME "skyguard")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR "SkyGuard Systems")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR "RPM;DEB")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl1.1")
endif()

include(CPack)

# ============================================
# Summary
# ============================================

message(STATUS "")
message(STATUS "SkyGuard configured successfully.")
message(STATUS "  Build with: cmake --build build")
message(STATUS "  Install with: cmake --install build")
message(STATUS "")
