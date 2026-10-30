juce_add_plugin(Farfield
    COMPANY_NAME "DeadRoom Audio"
    BUNDLE_ID com.deadroomaudio.farfield
    PLUGIN_MANUFACTURER_CODE DdRm
    PLUGIN_CODE Frfd
    FORMATS ${DEADROOM_FORMATS}
    PRODUCT_NAME "Farfield"
    DESCRIPTION "Places dialogue at a distance in a room or outdoors"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Reverb Spatial
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY Reverb)

target_sources(Farfield PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

deadroom_configure_plugin(Farfield)
