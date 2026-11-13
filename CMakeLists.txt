cmake_minimum_required(VERSION 3.24)
project(CXEngine VERSION 0.10.14 LANGUAGES CXX)

# CX is a graphical 2D engine. The graphical application is built by default;
# headless core/CLI builds are an explicit option for server-side development.
option(CX_BUILD_GRAPHICAL_RUNTIME "Build the graphical desktop engine application" ON)
option(CX_FETCH_SFML "Fetch pinned SFML 3.1.0 when no installed package is found" ON)
option(CX_ENABLE_SFML_AUDIO "Enable SFML audio playback when available" OFF)
option(CX_ENABLE_X11_FILE_DROP "Enable the optional X11/XWayland native file-drop adapter" ON)
option(CX_BUILD_TESTS "Build engine core and tool regression tests" ON)

# Backwards compatibility for build directories created before the GUI option was renamed.
if(DEFINED CX_ENABLE_SFML)
    message(DEPRECATION "CX_ENABLE_SFML is deprecated; use CX_BUILD_GRAPHICAL_RUNTIME instead.")
    if(NOT CX_ENABLE_SFML)
        set(CX_BUILD_GRAPHICAL_RUNTIME OFF CACHE BOOL "Build the graphical desktop engine application" FORCE)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use the static MSVC runtime so Windows starter packages do not depend on a
# separately installed Visual C++ redistributable. SFML is configured after
# this setting, so its C++ targets use the same runtime convention.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(cx_core
    src/value.cpp
    src/collision.cpp
    src/effects.cpp
    src/attack_patterns.cpp
    src/parser.cpp
    src/runtime.cpp
    src/runtime_motion.cpp
    src/runtime_interaction.cpp
    src/project_scaffold.cpp
    src/runtime_statements.cpp
    src/runtime_expressions.cpp
    src/runtime_projectiles.cpp
    src/diagnostics.cpp
    src/rooms.cpp
    src/variable_storage.cpp
    src/audio.cpp
    src/text_format.cpp
)
target_include_directories(cx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(cx_core PUBLIC cxx_std_20)

find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(cx_core PRIVATE ZLIB::ZLIB)
    target_compile_definitions(cx_core PRIVATE CX_HAVE_ZLIB=1)
    message(STATUS "zlib found: variables.cxv uses DEFLATE compression")
else()
    message(STATUS "zlib not found: variables.cxv uses the built-in PackBits fallback; variables.csv remains editable")
endif()

if(MSVC)
    target_compile_options(cx_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(cx_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(cx-cli src/cli_main.cpp)
target_link_libraries(cx-cli PRIVATE cx_core)

if(CX_BUILD_GRAPHICAL_RUNTIME)
    find_package(SFML 3.1 CONFIG QUIET COMPONENTS Graphics Window System)
    if(NOT TARGET SFML::Graphics AND CX_FETCH_SFML)
        include(FetchContent)
        set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "Disable SFML examples" FORCE)
        set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "Disable SFML tests" FORCE)
        set(SFML_BUILD_NETWORK OFF CACHE BOOL "Disable SFML networking" FORCE)
        set(SFML_BUILD_AUDIO ${CX_ENABLE_SFML_AUDIO} CACHE BOOL "Build SFML Audio" FORCE)
        FetchContent_Declare(SFML
            GIT_REPOSITORY https://github.com/SFML/SFML.git
            GIT_TAG 3.1.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SFML)
    endif()

    if(NOT TARGET SFML::Graphics OR NOT TARGET SFML::Window OR NOT TARGET SFML::System)
        message(FATAL_ERROR
            "The graphical CX Engine requires SFML 3.1.0 (Graphics, Window, System). "
            "Install SFML 3.1.0 or configure with -DCX_FETCH_SFML=ON. "
            "For an explicit headless core/CLI build use -DCX_BUILD_GRAPHICAL_RUNTIME=OFF.")
    endif()

    add_executable(cx-engine src/sfml_main.cpp)
    target_link_libraries(cx-engine PRIVATE cx_core SFML::Graphics SFML::Window SFML::System)
    target_compile_definitions(cx-engine PRIVATE CX_WITH_SFML=1)

    if(CX_ENABLE_SFML_AUDIO)
        find_package(SFML 3.1 CONFIG QUIET COMPONENTS Audio)
        if(TARGET SFML::Audio)
            target_link_libraries(cx-engine PRIVATE SFML::Audio)
            target_compile_definitions(cx-engine PRIVATE CX_WITH_SFML_AUDIO=1)
            message(STATUS "SFML Audio found: graphical audio playback is enabled")
        else()
            message(WARNING "SFML Audio was requested but its target/dependencies are unavailable; graphical playback is disabled. Configure with -DCX_ENABLE_SFML_AUDIO=OFF to silence this warning.")
        endif()
    else()
        message(STATUS "SFML Audio playback disabled; procedural audio generation/export remains available")
    endif()

    if(UNIX AND NOT APPLE AND CX_ENABLE_X11_FILE_DROP)
        find_package(X11 QUIET)
        if(X11_FOUND AND TARGET X11::X11)
            target_sources(cx-engine PRIVATE src/x11_file_drop.cpp)
            target_link_libraries(cx-engine PRIVATE X11::X11)
            target_compile_definitions(cx-engine PRIVATE CX_WITH_X11_DND=1)
            message(STATUS "X11/XWayland file-drop adapter enabled")
        else()
            message(STATUS "X11 development files not found: native drag-and-drop is disabled; use /attach in the in-game terminal")
        endif()
        message(STATUS "Wayland note: SFML 3.1.0 uses XWayland on Linux; a Wayland session needs an available XWayland server")
    endif()

    if(MSVC)
        target_compile_options(cx-engine PRIVATE /W4 /permissive-)
    else()
        target_compile_options(cx-engine PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
else()
    message(STATUS "Building the core and developer CLI only; the graphical runtime was explicitly disabled")
endif()

if(CX_BUILD_TESTS)
    enable_testing()
    add_executable(cx-tests tests/runtime_tests.cpp)
    target_link_libraries(cx-tests PRIVATE cx_core)
    add_test(NAME cx-runtime-tests COMMAND cx-tests)

    # Keep manifest parsing/recovery cases separate so the general runtime test
    # translation unit remains comfortably below the project's source-size limit.
    add_executable(cx-project-scaffold-tests tests/project_scaffold_tests.cpp)
    target_link_libraries(cx-project-scaffold-tests PRIVATE cx_core)
    add_test(NAME cx-project-scaffold-tests COMMAND cx-project-scaffold-tests)

    add_executable(cx-storage-manifest-tests tests/variable_storage_manifest_tests.cpp)
    target_link_libraries(cx-storage-manifest-tests PRIVATE cx_core)
    add_test(NAME cx-storage-manifest-tests COMMAND cx-storage-manifest-tests)

    # Exercise the project save lock with cooperating threads on every platform.
    # This runs the Windows LockFileEx implementation in native Windows CI as well
    # as the POSIX flock implementation on Linux/macOS.
    add_executable(cx-storage-thread-tests tests/storage_thread_tests.cpp)
    target_link_libraries(cx-storage-thread-tests PRIVATE cx_core)
    add_test(NAME cx-storage-thread-tests COMMAND cx-storage-thread-tests)
    set_tests_properties(cx-storage-thread-tests PROPERTIES TIMEOUT 60)

    # Pure pattern geometry has its own small test target so changes to angle
    # generation can be tested without needing the full runtime fixture suite.
    add_executable(cx-attack-pattern-tests tests/attack_pattern_tests.cpp)
    target_link_libraries(cx-attack-pattern-tests PRIVATE cx_core)
    add_test(NAME cx-attack-pattern-tests COMMAND cx-attack-pattern-tests)

    # Keep projectile integration tests separate from the larger general runtime suite.
    add_executable(cx-projectile-tests tests/projectile_runtime_tests.cpp)
    target_link_libraries(cx-projectile-tests PRIVATE cx_core)
    add_test(NAME cx-projectile-tests COMMAND cx-projectile-tests)

    # Named entity metadata and collision-channel filtering are tested independently
    # from the larger projectile integration suite.
    add_executable(cx-collision-tests tests/collision_runtime_tests.cpp)
    target_link_libraries(cx-collision-tests PRIVATE cx_core)
    add_test(NAME cx-collision-tests COMMAND cx-collision-tests)

    # Deterministic position tweens, cancellation, and completion identity are
    # tested independently from collision and projectile integration.
    add_executable(cx-motion-tests tests/motion_runtime_tests.cpp)
    target_link_libraries(cx-motion-tests PRIVATE cx_core)
    add_test(NAME cx-motion-tests COMMAND cx-motion-tests)

    # Textured beams use renderer-neutral state and geometry so core tests do not need SFML.
    add_executable(cx-beam-effect-tests tests/beam_effect_tests.cpp)
    target_link_libraries(cx-beam-effect-tests PRIVATE cx_core)
    add_test(NAME cx-beam-effect-tests COMMAND cx-beam-effect-tests)

    # Named clip timing and instance-safe completion events are isolated from
    # movement/tween tests so animation sequencing can evolve independently.
    add_executable(cx-animation-tests tests/animation_runtime_tests.cpp)
    target_link_libraries(cx-animation-tests PRIVATE cx_core)
    add_test(NAME cx-animation-tests COMMAND cx-animation-tests)

    # Asset-level and per-room tile collision metadata are tested independently.
    add_executable(cx-room-collision-tests tests/room_collision_tests.cpp)
    target_link_libraries(cx-room-collision-tests PRIVATE cx_core)
    add_test(NAME cx-room-collision-tests COMMAND cx-room-collision-tests)

    # Deterministic nearest-target interaction focus and callback identity are
    # isolated from physical overlap/trigger tests.
    add_executable(cx-interaction-focus-tests tests/interaction_focus_tests.cpp)
    target_link_libraries(cx-interaction-focus-tests PRIVATE cx_core)
    add_test(NAME cx-interaction-focus-tests COMMAND cx-interaction-focus-tests)

    # Explicitly seeded, stream-isolated random functions have golden-vector and reload tests.
    add_executable(cx-random-stream-tests tests/random_stream_tests.cpp)
    target_link_libraries(cx-random-stream-tests PRIVATE cx_core)
    add_test(NAME cx-random-stream-tests COMMAND cx-random-stream-tests)

    add_executable(cx-language-expression-tests tests/language_expression_tests.cpp)
    target_link_libraries(cx-language-expression-tests PRIVATE cx_core)
    add_test(NAME cx-language-expression-tests COMMAND cx-language-expression-tests)

    add_executable(cx-object-reference-tests tests/object_reference_tests.cpp)
    target_link_libraries(cx-object-reference-tests PRIVATE cx_core)
    add_test(NAME cx-object-reference-tests COMMAND cx-object-reference-tests)

    add_executable(cx-event-syntax-tests tests/event_syntax_tests.cpp)
    target_link_libraries(cx-event-syntax-tests PRIVATE cx_core)
    add_test(NAME cx-event-syntax-tests COMMAND cx-event_syntax-tests)

    add_executable(cx-variable-effect-syntax-tests tests/variable_effect_syntax_tests.cpp)
    target_link_libraries(cx-variable-effect-syntax-tests PRIVATE cx_core)
    add_test(NAME cx-variable-effect-syntax-tests COMMAND cx-variable-effect-syntax-tests)

    add_executable(cx-room-transition-syntax-tests tests/room_transition_syntax_tests.cpp)
    target_link_libraries(cx-room-transition-syntax-tests PRIVATE cx_core)
    add_test(NAME cx-room-transition-syntax-tests COMMAND cx-room-transition-syntax-tests)

    # Keep delayed-continuation and room-context lifecycle regressions independent
    # from the large general runtime test translation unit.
    add_executable(cx-task-lifecycle-tests tests/task_lifecycle_tests.cpp)
    target_link_libraries(cx-task-lifecycle-tests PRIVATE cx_core)
    add_test(NAME cx-task-lifecycle-tests COMMAND cx-task-lifecycle-tests)

    # Room transitions are integration boundaries for runtime-global entities/effects,
    # room-scoped rendering state, canvases and wall-clock schedules.
    add_executable(cx-room-lifecycle-tests tests/room_lifecycle_tests.cpp)
    target_link_libraries(cx-room-lifecycle-tests PRIVATE cx_core)
    add_test(NAME cx-room-lifecycle-tests COMMAND cx-room-lifecycle-tests)

    # Linux-only subprocess coverage for save interruption/recovery, project-lock honoring,
    # and competing writers. No production failpoint hooks are needed.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(cx-storage-process-tests tests/storage_process_tests.cpp)
        target_link_libraries(cx-storage-process-tests PRIVATE cx_core)
        add_test(NAME cx-storage-process-tests COMMAND cx-storage-process-tests)
        set_tests_properties(cx-storage-process-tests PROPERTIES TIMEOUT 60)
    endif()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cx-room-editor-tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_room_editor_server.py)
        set_tests_properties(cx-room-editor-tests PROPERTIES TIMEOUT 60)
        # Exercise the source/quick release packager with temporary fixture trees;
        # this prevents a future release from quietly including caches or sample games.
        add_test(NAME cx-package-release-tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_package_release.py)
        set_tests_properties(cx-package-release-tests PROPERTIES TIMEOUT 60)
        add_test(NAME cx-package-reference-tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_package_reference.py)
        set_tests_properties(cx-package-reference-tests PROPERTIES TIMEOUT 30)
        add_test(NAME cx-release-assets-verifier-tests
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_verify_release_assets.py)
        set_tests_properties(cx-release-assets-verifier-tests PROPERTIES TIMEOUT 30)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET cx-engine)
            add_test(NAME cx-wayland-no-xwayland-guard-tests
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wayland_no_xwayland.py
                             $<TARGET_FILE:cx-engine>)
            set_tests_properties(cx-wayland-no-xwayland-guard-tests PROPERTIES TIMEOUT 10)
        endif()
    endif()
endif()

# Install only the native runtime executables and their non-system shared
# dependencies. CMake's runtime dependency scanner keeps bundled SFML shared
# libraries beside the binaries while excluding operating-system libraries.
# The runtime packager flattens bin/ and lib/ into CX-Engine/ so
# redistributable shared libraries sit beside the executables.
set(CX_INSTALL_RPATH "$ORIGIN")
if(APPLE)
    set(CX_INSTALL_RPATH "@loader_path;@loader_path/Frameworks")
endif()
set_target_properties(cx-cli PROPERTIES INSTALL_RPATH "${CX_INSTALL_RPATH}")
install(TARGETS cx-cli
    RUNTIME_DEPENDENCY_SET cx_runtime_dependencies
    RUNTIME DESTINATION bin
)
if(TARGET cx-engine)
    set_target_properties(cx-engine PROPERTIES INSTALL_RPATH "${CX_INSTALL_RPATH}")
    install(TARGETS cx-engine
        RUNTIME_DEPENDENCY_SET cx_runtime_dependencies
        RUNTIME DESTINATION bin
    )
endif()
if(TARGET sfml-graphics AND TARGET sfml-window AND TARGET sfml-system)
    install(TARGETS sfml-graphics sfml-window sfml-system
        RUNTIME_DEPENDENCY_SET cx_runtime_dependencies
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
    )
endif()
install(RUNTIME_DEPENDENCY_SET cx_runtime_dependencies
    PRE_EXCLUDE_REGEXES
        "^api-ms-.*"
        "^ext-ms-.*"
        # CMake lowercases Windows DLL names before PRE_EXCLUDE_REGEXES matching.
        "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder|shell32|shlwapi|dbgeng|d2d1|uiautomationcore|esent|coreuicomponents|certenroll|setupapi|windows\\.storage)\\.dll$"
        "^/lib/.*"
        "^/usr/lib/.*"
        "^[A-Za-z]:[/\\\\]Windows[/\\\\](System32|SysWOW64|SysArm32|WinSxS)[/\\\\].*"
    POST_EXCLUDE_REGEXES
        "^/System/Library/.*"
        "^/usr/lib/.*"
        "^/lib/.*"
        "^[A-Za-z]:[/\\\\]Windows[/\\\\](System32|SysWOW64|SysArm32|WinSxS)[/\\\\].*"
        "^[A-Za-z]:[/\\\\]Windows[/\\\\]SysWOW64[/\\\\].*"
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    FRAMEWORK DESTINATION Frameworks
)
