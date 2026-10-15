# Sanitizers: `-DSANITIZERS=asan` (address + leak + undefined) or
# `-DSANITIZERS=tsan` (thread; incompatible with asan). Same convention as
# baldr-cpp and nova-cpp. Applies to every target defined after inclusion.

set(SANITIZERS "" CACHE STRING "Sanitizer set: '', 'asan' or 'tsan'")
set_property(CACHE SANITIZERS PROPERTY STRINGS "" asan tsan)

if(SANITIZERS STREQUAL "asan")
    set(_san_flags -fsanitize=address,leak,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
elseif(SANITIZERS STREQUAL "tsan")
    set(_san_flags -fsanitize=thread -fno-omit-frame-pointer)
elseif(NOT SANITIZERS STREQUAL "")
    message(FATAL_ERROR "Unknown SANITIZERS value: '${SANITIZERS}'")
endif()

if(_san_flags)
    message(STATUS "Sanitizers: ${SANITIZERS}")
    add_compile_options(${_san_flags})
    add_link_options(${_san_flags})
endif()
