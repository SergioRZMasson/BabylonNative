# MIT-licensed RmlUI patch, same upstream source pin. This hook also runs for
# FETCHCONTENT_SOURCE_DIR_RMLUI overrides, which bypass PATCH_COMMAND.
function(babylon_lite_patch_rmlui source)
    set(_pin b7b4a0688262832eacf3b9abb41f8bbe73868af8)
    set(_directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(_patch "${_directory}/Patches/BoxShadowRenderManager.patch")
    set(_receipt "${CMAKE_BINARY_DIR}/RmlUiBoxShadowPatch.json")
    file(TO_CMAKE_PATH "${source}" _source_json)
    file(WRITE "${_receipt}"
        "{\n \"pin\":\"${_pin}\",\n \"source\":\"${_source_json}\",\n"
        " \"verified\":false\n}\n")
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --show-toplevel
        WORKING_DIRECTORY "${_directory}" RESULT_VARIABLE _root_result
        OUTPUT_VARIABLE _git_working_directory OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _root_result EQUAL 0)
        set(_git_working_directory "${_directory}")
    endif()
    file(LOCK "${source}/.babylon-lite-shadow-patch.lock" GUARD FUNCTION TIMEOUT 120)
    if(EXISTS "${source}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
            OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(NOT _head STREQUAL _pin)
            message(FATAL_ERROR "RmlUI cache must match source pin ${_pin}, not ${_head}.")
        endif()
    endif()

    set(_identity_files CMakeLists.txt LICENSE.txt)
    set(_identity_hashes
        cd4998ddca138152c48f91c9ca359b1e99193b056e1cd6116442911ad363eec4
        64c049f65ec810b805457dc1f24745002203f919ea5a16b6fcd7ca5f45ea298e)
    foreach(_index RANGE 0 1)
        list(GET _identity_files ${_index} _file)
        list(GET _identity_hashes ${_index} _expected)
        file(READ "${source}/${_file}" _content)
        string(REPLACE "\r\n" "\n" _content "${_content}")
        string(SHA256 _hash "${_content}")
        if(NOT _hash STREQUAL _expected)
            message(FATAL_ERROR "RmlUI pinned identity mismatch in ${_file}.")
        endif()
    endforeach()

    set(_files Source/Core/BoxShadowCache.h Source/Core/BoxShadowCache.cpp)
    set(_before
        68dc3dd3ad305a5770bc7bafc3a9e4eda67fbf4a80b83a224fdd35fbe4013922
        6a4490a03ac8619dab739a8e86a57eb791b806b40e8a8084abb5eb0d902c55bd)
    set(_after
        6275bf50f917e4df1b30d535bf9eeb946f8011f78605a5edc4ea23dd03e879ea
        95ff7070b8617599cc0ccd2b8b6f63bdbbc3dbc91f6149cd0a113bb30fc44fd2)
    set(_pristine_count 0)
    set(_patched_count 0)
    foreach(_index RANGE 0 1)
        list(GET _files ${_index} _file)
        list(GET _before ${_index} _expected_before)
        list(GET _after ${_index} _expected_after)
        file(READ "${source}/${_file}" _content)
        string(REPLACE "\r\n" "\n" _content "${_content}")
        string(SHA256 _hash "${_content}")
        set(_normalized_${_index} "${_content}")
        if(_hash STREQUAL _expected_before)
            math(EXPR _pristine_count "${_pristine_count}+1")
        elseif(_hash STREQUAL _expected_after)
            math(EXPR _patched_count "${_patched_count}+1")
        else()
            message(FATAL_ERROR "RmlUI patch source mismatch in ${_file}: ${_hash}.")
        endif()
    endforeach()
    if(NOT _pristine_count EQUAL 2 AND NOT _patched_count EQUAL 2)
        message(FATAL_ERROR "RmlUI shadow cache patch is partial; restore the exact source pin.")
    endif()

    file(SHA256 "${_patch}" _patch_hash)
    file(TO_CMAKE_PATH "${source}" _source_json)
    string(REPLACE ";" "\",\"" _before_json "${_before}")
    string(REPLACE ";" "\",\"" _after_json "${_after}")
    string(REPLACE ";" "\",\"" _files_json "${_files}")
    file(WRITE "${_receipt}"
        "{\n \"pin\":\"${_pin}\",\n \"source\":\"${_source_json}\",\n"
        " \"patchSha256\":\"${_patch_hash}\",\n \"hashPolicy\":\"canonical LF UTF-8\",\n"
        " \"files\":[\"${_files_json}\"],\n"
        " \"before\":[\"${_before_json}\"],\n \"after\":[\"${_after_json}\"],\n"
        " \"verified\":false\n}\n")
    if(_pristine_count EQUAL 2)
        foreach(_index RANGE 0 1)
            list(GET _files ${_index} _file)
            file(WRITE "${source}/${_file}" "${_normalized_${_index}}")
        endforeach()
        file(TO_CMAKE_PATH "${source}" _git_source)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check --unsafe-paths
            "--directory=${_git_source}" "${_patch}"
            WORKING_DIRECTORY "${_git_working_directory}" COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
            "--directory=${_git_source}" "${_patch}"
            WORKING_DIRECTORY "${_git_working_directory}" COMMAND_ERROR_IS_FATAL ANY)
    endif()
    foreach(_index RANGE 0 1)
        list(GET _files ${_index} _file)
        list(GET _after ${_index} _expected)
        file(READ "${source}/${_file}" _content)
        string(REPLACE "\r\n" "\n" _content "${_content}")
        string(SHA256 _hash "${_content}")
        if(NOT _hash STREQUAL _expected)
            message(FATAL_ERROR "RmlUI patch postcondition failed in ${_file}.")
        endif()
    endforeach()
    file(READ "${_receipt}" _json)
    string(REPLACE "\"verified\":false" "\"verified\":true" _json "${_json}")
    file(WRITE "${_receipt}" "${_json}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_patch}" "${_directory}/ApplyBoxShadowPatch.cmake")
    message(STATUS "RmlUI shadow cache: source pin and renderer-scoped patch verified.")
endfunction()
