juce_add_plugin(Duotone
    COMPANY_NAME "DeadRoom Audio"
    BUNDLE_ID com.deadroomaudio.duotone
    PLUGIN_MANUFACTURER_CODE DdRm
    PLUGIN_CODE Dutn
    FORMATS ${DEADROOM_FORMATS}
    PRODUCT_NAME "Duotone"
    DESCRIPTION "Mid/Side Baxandall tone EQ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx EQ Stereo
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY EQ)

target_sources(Duotone PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

deadroom_configure_plugin(Duotone)
