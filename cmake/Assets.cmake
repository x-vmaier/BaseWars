# Makes the asset folder available next to an executable (symlink on Unix, copy on Windows)
function(bw_link_assets target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets" VERBATIM)
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets" VERBATIM)
    endif()
endfunction()
