juce_add_plugin(Roomless
    COMPANY_NAME "DeadRoom Audio"
    BUNDLE_ID com.deadroomaudio.roomless
    PLUGIN_MANUFACTURER_CODE DdRm
    PLUGIN_CODE Rmls
    FORMATS ${DEADROOM_FORMATS}
    PRODUCT_NAME "Roomless"
    DESCRIPTION "Removes room reverb from recordings"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Restoration
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY NoiseReduction)

target_sources(Roomless PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

deadroom_configure_plugin(Roomless)
