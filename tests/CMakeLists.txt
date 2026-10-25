add_executable(test_logic test_logic.cpp)
target_link_libraries(test_logic PRIVATE werwolf_server_lib)
add_test(NAME logik COMMAND test_logic)

add_executable(test_network test_network.cpp)
target_link_libraries(test_network PRIVATE werwolf_server_lib)
add_test(NAME netzwerk COMMAND test_network)
set_tests_properties(netzwerk PROPERTIES TIMEOUT 120)
