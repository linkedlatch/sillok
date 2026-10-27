# Fails if a file in CORE_DIR includes anything but standard headers or
# other core headers, keeping third-party code out of the core.
set(standard_headers
    algorithm array atomic bit cctype chrono cmath compare concepts cstddef
    cstdint cstdio cstdlib cstring exception filesystem fstream functional
    iterator limits map memory mutex numbers numeric optional set span sstream
    stdexcept stop_token string string_view system_error thread tuple
    type_traits unordered_map utility variant vector)
file(GLOB sources "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.cpp")
set(violations "")
foreach(source IN LISTS sources)
    file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS includes)
        if(line MATCHES "#[ \t]*include[ \t]*<([^>]+)>")
            if(NOT CMAKE_MATCH_1 IN_LIST standard_headers)
                list(APPEND violations "${source}: <${CMAKE_MATCH_1}>")
            endif()
        elseif(line MATCHES "#[ \t]*include[ \t]*\"([^\"]+)\"")
            if(NOT CMAKE_MATCH_1 MATCHES "^core/")
                list(APPEND violations "${source}: \"${CMAKE_MATCH_1}\"")
            endif()
        endif()
    endforeach()
endforeach()
if(violations)
    list(JOIN violations "\n" text)
    message(FATAL_ERROR "non-standard includes in src/core:\n${text}")
endif()
