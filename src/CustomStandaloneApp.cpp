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
        : StandaloneFilterWindow (title, bg, settings, false)
    {
        setUsingNativeTitleBar (true);

        for (int i = getNumChildComponents() - 1; i >= 0; --i)
            if (auto* btn = dynamic_cast<juce::TextButton*> (getChildComponent (i)))
                if (btn->getButtonText() == "Options")
                    btn->setVisible (false);
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
