idf_component_register(
    SRC_DIRS "./src"
    INCLUDE_DIRS "./include"
    REQUIRES 
    mbedtls
    esp_codec_dev
    esp_audio_codec
    esp_video_codec
    gmf_core
    gmf_io
    gmf_audio
    gmf_ai_audio
    gmf_video
    gmf_misc
)
