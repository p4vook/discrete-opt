instance        score   cpu_seconds     points
sc_157_0        94600   5.25    5
sc_330_0        23      68.17   7
sc_1000_11      149     14.46   5
sc_5000_1       31      97.11   7
sc_10000_5      92      110.32  5
sc_10000_2      195     47.10   5
total_points    34 / 42add_library(optlib SHARED
  anneal.cpp
  accept.cpp
  fenwick.cpp
  sched.cpp
)

target_include_directories(optlib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_features(optlib PUBLIC cxx_std_20)
