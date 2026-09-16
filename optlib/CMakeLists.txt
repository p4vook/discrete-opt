add_library(optlib SHARED
  anneal.cpp
  sched.cpp
)

target_include_directories(optlib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_features(optlib PUBLIC cxx_std_20)
