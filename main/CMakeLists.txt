idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer esp_driver_uart
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
