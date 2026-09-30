add_executable(MonthGridTest
  MonthGridTest.cpp
  ${REPO_ROOT}/src/util/MonthGrid.cpp
  ${REPO_ROOT}/src/activities/apps/standby/ChineseAlmanac.cpp
)

target_include_directories(MonthGridTest PRIVATE
  ${REPO_ROOT}/src
)

# MonthGrid depends on the Chinese lunar/solar-term tables.
target_compile_definitions(MonthGridTest PRIVATE ENABLE_CHINESE_VERSION)

target_link_libraries(MonthGridTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MonthGridTest)
