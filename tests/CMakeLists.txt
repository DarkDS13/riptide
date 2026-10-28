add_executable(riptide_tests
    test_main.cpp
    test_data.cpp
    test_core.cpp
    test_decoder.cpp
)
target_compile_options(riptide_tests PRIVATE ${RIPTIDE_WARNINGS})
target_link_libraries(riptide_tests PRIVATE riptide ZLIB::ZLIB)

# One CTest entry per suite; the runner filters tests by name.
foreach(suite IN ITEMS bit_reader huffman crc32 gzip_format decoder block_finder
                       expansion_estimator thread_pool prefix_scan window window_resolver pipeline)
    add_test(NAME ${suite} COMMAND riptide_tests "${suite}.")
endforeach()
