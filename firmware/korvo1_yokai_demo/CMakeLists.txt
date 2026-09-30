cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(korvo1_yokai_demo)

execute_process(
    COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/apply_managed_component_patches.py
            --project-root ${CMAKE_CURRENT_LIST_DIR}
            --build-root ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE managed_patch_result
)
if(NOT managed_patch_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply required managed-component patches")
endif()

option(YOKAI_CAMERA_DMA_DIAGNOSTICS "Temporarily trace camera DMA and frame integrity" OFF)
if(CONFIG_SOC_LCDCAM_CAM_SUPPORTED)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_CURRENT_LIST_DIR}/tools/prepare_camera_driver.py"
                 "$ENV{IDF_PATH}/components/esp_driver_cam/dvp/src/esp_cam_ctlr_dvp_cam.c")
    set(camera_diagnostic_args)
    if(YOKAI_CAMERA_DMA_DIAGNOSTICS)
        set(camera_diagnostic_args --diagnostics)
        target_compile_definitions(__idf_main PRIVATE VISION_FRAME_INTEGRITY_DIAGNOSTICS=1)
    endif()
    execute_process(
        COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/prepare_camera_driver.py
                "$ENV{IDF_PATH}" "${CMAKE_BINARY_DIR}/camera_dma_checked.c" ${camera_diagnostic_args}
        COMMAND_ERROR_IS_FATAL ANY
    )
    get_target_property(camera_sources __idf_esp_driver_cam SOURCES)
    list(FILTER camera_sources EXCLUDE REGEX "dvp/src/esp_cam_ctlr_dvp_cam\\.c$")
    set_property(TARGET __idf_esp_driver_cam PROPERTY SOURCES "${camera_sources}")
    target_sources(__idf_esp_driver_cam PRIVATE "${CMAKE_BINARY_DIR}/camera_dma_checked.c")
endif()
