cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo1_yokai_demo)

execute_process(
    COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/apply_managed_component_patches.py
            --project-root ${CMAKE_CURRENT_LIST_DIR}
    RESULT_VARIABLE managed_patch_result
)
if(NOT managed_patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply required managed-component patches")
endif()
