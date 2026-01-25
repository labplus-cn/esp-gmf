if (${IDF_TARGET} STREQUAL "esp32p4")
list(APPEND COMPONENT_REQUIRES  esp_driver_ppa esp_hw_support esp_mm)
endif()

idf_component_register(SRC_DIRS "./"
                       INCLUDE_DIRS "./include"
                       PRIV_INCLUDE_DIRS "./private_include"
                       REQUIRES 
                       ${COMPONENT_REQUIRES}
                       esp_video_codec
                       esp_image_effects
                       gmf_core
)