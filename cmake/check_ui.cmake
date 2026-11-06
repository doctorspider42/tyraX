# A fresh fixture/config per run: never touch examples or the user's settings.
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(work "${CHECK_DIR}/${run_id}")
file(MAKE_DIRECTORY "${work}/config")
set(ENV{LOCALAPPDATA} "${work}/config")
set(ENV{XDG_CONFIG_HOME} "${work}/config")
execute_process(COMMAND "${EDITOR}" --new ui-id-check "${work}" 8 8 empty --no-terrain
    RESULT_VARIABLE result TIMEOUT 60)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Could not create UI fixture: ${result}")
endif()
set(project "${work}/ui-id-check")
file(WRITE "${project}/res/id-probe.obj" "o probe\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
file(WRITE "${work}/check.ui" "frames 30
click Assets
click id-probe.obj
expect 'Project/Add to scene'
expect 'Project/Open in Asset Browser##selection'
frames 5
shot \"${work}/assets.png\"
click 'Project/Open in Asset Browser##selection'
expect 'Asset Browser'
frames 5
quit
")
execute_process(COMMAND "${EDITOR}" --ui-script "${project}" --file "${work}/check.ui"
    RESULT_VARIABLE result TIMEOUT 90)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "UI check failed (${result}). Fixture and screenshots: ${work}")
endif()
message(STATUS "UI check passed. Fixture and screenshots: ${work}")
