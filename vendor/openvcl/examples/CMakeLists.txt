set(_EXAMPLES pdk sprite triangle triangle-gasp triangle_strip)

foreach(_example IN LISTS _EXAMPLES)
    string(REPLACE "-" "_" _target_name "${_example}")
    add_custom_target(example_${_target_name}
        COMMAND ${CMAKE_MAKE_PROGRAM}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_example}
        COMMENT "Building example: ${_example}"
        VERBATIM
    )
    add_dependencies(example_${_target_name} openvcl)
    list(APPEND _EXAMPLE_TARGETS example_${_target_name})
endforeach()

# sprite, triangle, triangle-gasp, and triangle_strip all link against libpdk.a
foreach(_dep sprite triangle triangle_gasp triangle_strip)
    add_dependencies(example_${_dep} example_pdk)
endforeach()

add_custom_target(examples ALL
    DEPENDS ${_EXAMPLE_TARGETS}
    COMMENT "Building all PS2 examples"
)
