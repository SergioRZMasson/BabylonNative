add_executable(LiteAssetContractTests Tests.cpp)
target_link_libraries(LiteAssetContractTests PRIVATE LiteLayer bgfx)
target_include_directories(LiteAssetContractTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteAssetContractTests PRIVATE cxx_std_20)
warnings_as_errors(LiteAssetContractTests)
add_test(NAME LiteAssetContractTests COMMAND LiteAssetContractTests)
add_executable(LiteAssetAccessorTests Accessors.cpp)
target_link_libraries(LiteAssetAccessorTests PRIVATE LiteLayer bgfx)
target_include_directories(LiteAssetAccessorTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Core/LiteLayer/Source")
target_compile_features(LiteAssetAccessorTests PRIVATE cxx_std_20)
warnings_as_errors(LiteAssetAccessorTests)
add_test(NAME LiteAssetAccessorTests COMMAND LiteAssetAccessorTests)
if(WIN32)
    add_executable(LiteAssetDataOnlyTests DataOnly.cpp)
    target_link_libraries(LiteAssetDataOnlyTests PRIVATE LiteLayer)
    target_compile_features(LiteAssetDataOnlyTests PRIVATE cxx_std_17)
    target_compile_options(LiteAssetDataOnlyTests PRIVATE /UNDEBUG)
    target_link_options(LiteAssetDataOnlyTests PRIVATE
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/LiteAssetDataOnlyTests.map")
    warnings_as_errors(LiteAssetDataOnlyTests)
    add_custom_command(TARGET LiteAssetDataOnlyTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DMAP=${CMAKE_CURRENT_BINARY_DIR}/LiteAssetDataOnlyTests.map"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckDataOnly.cmake")
    add_test(NAME LiteAssetDataOnlyTests COMMAND LiteAssetDataOnlyTests)
endif()
