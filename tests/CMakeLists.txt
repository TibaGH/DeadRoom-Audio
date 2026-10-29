juce_add_console_app(DspTests PRODUCT_NAME "DspTests")

target_sources(DspTests PRIVATE DspTests.cpp)

target_link_libraries(DspTests
    PRIVATE
        deadroom_common
        juce::juce_dsp
        juce::juce_audio_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(DspTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
