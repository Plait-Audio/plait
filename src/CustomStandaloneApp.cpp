#if JucePlugin_Build_Standalone

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "ISODesign.h"

namespace
{

class ISOStandaloneWindow : public juce::StandaloneFilterWindow
{
public:
    ISOStandaloneWindow (const juce::String& title,
                         juce::Colour bg,
                         juce::PropertySet* settings)
        : StandaloneFilterWindow (title, bg, makeHolder (settings))
    {
        setUsingNativeTitleBar (true);

        for (int i = getNumChildComponents() - 1; i >= 0; --i)
            if (auto* btn = dynamic_cast<juce::TextButton*> (getChildComponent (i)))
                if (btn->getButtonText() == "Options")
                    btn->setVisible (false);
    }

private:
    // Build the plugin holder with 0 inputs / 2 outputs so that:
    //   • macOS never switches Bluetooth devices into Hands-Free/SCO mode
    //     (which would lock them to mono + 16 kHz)
    //   • The Audio/MIDI settings dialog omits the Input, Feedback Loop,
    //     and Active input channels rows entirely
    static std::unique_ptr<juce::StandalonePluginHolder> makeHolder (juce::PropertySet* settings)
    {
        juce::Array<juce::StandalonePluginHolder::PluginInOuts> channels;
        channels.add ({ 0, 2 }); // 0 inputs, 2 outputs (stereo)

        auto holder = std::make_unique<juce::StandalonePluginHolder> (
            settings,
            false,    // don't take ownership of settings (app properties owns it)
            "",       // no preferred device name
            nullptr,  // no preferred setup options
            channels);

        // Safety net: if saved prefs had a sub-44100 Hz rate, bump it up.
        // (Normally avoided by the 0-input fix above, but protects against
        //  stale settings files written before this fix was applied.)
        auto& dm    = holder->deviceManager;
        auto  setup = dm.getAudioDeviceSetup();
        if (setup.sampleRate < 44100.0)
        {
            setup.sampleRate = 44100.0;
            dm.setAudioDeviceSetup (setup, true);
        }

        return holder;
    }
};

class ISODrumsStandaloneApp : public juce::JUCEApplication
{
public:
    ISODrumsStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        appProperties_.setStorageParameters (options);
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override           { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);

        mainWindow_ = std::make_unique<ISOStandaloneWindow> (
            getApplicationName(),
            ISOPalette::Bg,
            appProperties_.getUserSettings());
        mainWindow_->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow_ = nullptr;
        appProperties_.saveIfNeeded();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow_ != nullptr)
            mainWindow_->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []() {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    ISOLookAndFeel lookAndFeel_;
    juce::ApplicationProperties appProperties_;
    std::unique_ptr<ISOStandaloneWindow> mainWindow_;
};

} // anonymous namespace

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new ISODrumsStandaloneApp();
}

#endif // JucePlugin_Build_Standalone
