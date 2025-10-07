function(get_all_targets _result _dir)
    get_property(_subdirs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        get_all_targets(${_result} "${_subdir}")
    endforeach()

    get_directory_property(_sub_targets DIRECTORY "${_dir}" BUILDSYSTEM_TARGETS)
    set(${_result} ${${_result}} ${_sub_targets} PARENT_SCOPE)
endfunction()

function(install_target_to_dir tgt dir)
  if (NOT WIN32)
    return()
  endif()

  if (NOT TARGET ${tgt})
    return()
  endif()
  
  if (IS_ABSOLUTE "${dir}")
    message(FATAL_ERROR "install_target_to_dir: 'dir' should be relative for ${CMAKE_INSTALL_PREFIX}, actual: ${dir}")
  endif()

  get_target_property(_imported ${tgt} IMPORTED)
  if (_imported)
    return()
  endif()

  get_target_property(_type ${tgt} TYPE)
  if (_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "OBJECT_LIBRARY")
    return()
  endif()
 
  install(TARGETS ${tgt}
    RUNTIME  DESTINATION ${dir}
    LIBRARY  DESTINATION ${dir}
    ARCHIVE  DESTINATION ${dir}
  )

  if (_type STREQUAL "EXECUTABLE" OR _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
    install(FILES $<TARGET_PDB_FILE:${tgt}> DESTINATION ${dir} OPTIONAL)
    
    if (COMMAND qt_generate_deploy_script)

      qt_generate_deploy_script(
        TARGET        ${tgt}
        OUTPUT_SCRIPT deploy_script
        CONTENT "
                # === executing on stage 'cmake --install' ===

                include(\"\${QT_DEPLOY_SUPPORT}\")

                qt_deploy_runtime_dependencies(
                  EXECUTABLE \"$<TARGET_FILE:${tgt}>\"
                  GENERATE_QT_CONF
                  BIN_DIR \"${dir}\"
                  PLUGINS_DIR \"./\${dir}\"
                  DEPLOY_TOOL_OPTIONS --no-translations --no-compiler-runtime
                )
                "
      )
      install(SCRIPT ${deploy_script})
    endif()
  endif()
endfunction()
