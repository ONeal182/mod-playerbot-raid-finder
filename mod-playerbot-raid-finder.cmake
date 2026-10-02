# mod-playerbot-raid-finder — optional per-module CMake hook.
#
# Module sources under src/ are auto-collected into the shared `modules`
# library by modules/CMakeLists.txt. REQUIRES mod-playerbots (links against its
# public headers from the same `modules` library).
#
# Defines a STANDALONE unit-test executable for the pure, engine-independent
# parts (composition planner, reservation manager). It links only gtest, so it
# builds in seconds and does not depend on the core `unit_tests` target.

if (BUILD_TESTING)
  function(define_raid_finder_tests)
    # CMAKE_CURRENT_LIST_DIR is not safe inside a deferred call; build the path
    # explicitly (same technique as mod-ai-director / mod-dungeon-clear).
    set(MOD_PATH "${CMAKE_SOURCE_DIR}/modules/mod-playerbot-raid-finder")

    add_executable(raid_finder_tests
      "${MOD_PATH}/src/RfCompositionPlanner.cpp"
      "${MOD_PATH}/src/RfReservationManager.cpp"
      "${MOD_PATH}/tests/RfCompositionPlannerTest.cpp"
      "${MOD_PATH}/tests/RfReservationManagerTest.cpp"
    )

    target_link_libraries(raid_finder_tests gtest_main)

    target_include_directories(raid_finder_tests PRIVATE "${MOD_PATH}/src")

    set_target_properties(raid_finder_tests PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_test(NAME raid_finder COMMAND ${CMAKE_BINARY_DIR}/raid_finder_tests)
  endfunction()

  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL define_raid_finder_tests)
endif()
