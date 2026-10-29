juce_add_plugin(Lantern
    COMPANY_NAME "DeadRoom Audio"
    BUNDLE_ID com.deadroomaudio.lantern
    PLUGIN_MANUFACTURER_CODE DdRm
    PLUGIN_CODE Lntn
    FORMATS ${DEADROOM_FORMATS}
    PRODUCT_NAME "Lantern"
    DESCRIPTION "Passive-style linear-phase mastering EQ with tube warmth"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx EQ Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY EQ)

target_sources(Lantern PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

deadroom_configure_plugin(Lantern)
