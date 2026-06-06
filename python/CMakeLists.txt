find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_cmf_bt bindings.cpp)
target_link_libraries(_cmf_bt PRIVATE back-tester-sim)

# Glue code over pybind templates: keep the strict warnings but do not fail on them.
target_compile_options(_cmf_bt PRIVATE -Wno-error)

# Drop the built module next to the Python package so `import cmf_bt` just works.
set_target_properties(_cmf_bt PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/python/cmf_bt)
