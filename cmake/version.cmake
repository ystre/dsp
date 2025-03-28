function(dsp_extract_version)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/include/dsp/dsp.hh" file_contents)
    string(REGEX MATCH ".*DspVersionMajor = ([0-9]+)" _ "${file_contents}")
    if(NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract major version number from dsp/dsp.h")
    endif()
    set(ver_major ${CMAKE_MATCH_1})

    string(REGEX MATCH ".*DspVersionMinor = ([0-9]+)" _ "${file_contents}")
    if(NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract minor version number from dsp/dsp.h")
    endif()

    set(ver_minor ${CMAKE_MATCH_1})
    string(REGEX MATCH ".*DspVersionPatch = ([0-9]+)" _ "${file_contents}")
    if(NOT CMAKE_MATCH_COUNT EQUAL 1)
        message(FATAL_ERROR "Could not extract patch version number from dsp/dsp.h")
    endif()
    set(ver_patch ${CMAKE_MATCH_1})

    set(DSP_VERSION_MAJOR ${ver_major} PARENT_SCOPE)
    set(DSP_VERSION_MINOR ${ver_minor} PARENT_SCOPE)
    set(DSP_VERSION_PATCH ${ver_patch} PARENT_SCOPE)
    set(DSP_VERSION "${ver_major}.${ver_minor}.${ver_patch}" PARENT_SCOPE)
endfunction()
