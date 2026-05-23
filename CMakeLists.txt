cmake_minimum_required(VERSION 3.16)
project(xbit_flasher
    VERSION 1.1.0
    DESCRIPTION "X-Bit (Xbit) Modchip Flasher for Original Xbox"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_HOOKDLL "Build the hookDll (Windows only)" OFF)
option(ENABLE_ASAN   "Enable AddressSanitizer"          OFF)
option(ENABLE_UBSAN  "Enable UndefinedBehaviorSanitizer" OFF)

# ── Dependencies ─────────────────────────────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)
    set(HIDAPI_TARGET PkgConfig::HIDAPI)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi)
    set(HIDAPI_TARGET PkgConfig::HIDAPI)
else()
    # Windows (vcpkg)
    find_package(hidapi CONFIG REQUIRED)
    set(HIDAPI_TARGET hidapi::hidapi)
endif()

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(xbit_flasher main.cpp)

target_include_directories(xbit_flasher PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(xbit_flasher PRIVATE
    ${HIDAPI_TARGET}
)

target_compile_options(xbit_flasher PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wno-unused-parameter>
)

if(ENABLE_ASAN)
    target_compile_options(xbit_flasher PRIVATE -fsanitize=address,leak)
    target_link_options(xbit_flasher PRIVATE -fsanitize=address,leak)
endif()

if(ENABLE_UBSAN)
    target_compile_options(xbit_flasher PRIVATE -fsanitize=undefined)
    target_link_options(xbit_flasher PRIVATE -fsanitize=undefined)
endif()

# ── Install rules ────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS xbit_flasher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES udev/99-xbit-flasher.rules
            DESTINATION /etc/udev/rules.d)
    install(CODE "execute_process(COMMAND udevadm control --reload-rules)")
endif()

# ── Windows hookDll (optional) ────────────────────────────────────────────────
if(BUILD_HOOKDLL AND WIN32)
    find_path(MINHOOK_INCLUDE_DIR MinHook.h)
    find_library(MINHOOK_LIBRARY NAMES minhook MinHook)

    add_library(xbit_hook SHARED
        hookDll/main.c
        hookDll/hook.c
    )
    target_include_directories(xbit_hook PRIVATE
        hookDll
        ${MINHOOK_INCLUDE_DIR}
    )
    target_link_libraries(xbit_hook PRIVATE ${MINHOOK_LIBRARY})
    install(TARGETS xbit_hook RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "xbit_flasher ${PROJECT_VERSION}")
message(STATUS "  Platform   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  HIDAPI     : ${HIDAPI_TARGET}")
message(STATUS "  HookDll    : ${BUILD_HOOKDLL}")
message(STATUS "")
