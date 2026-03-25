#include "PluginEditor.h"
#include "PluginProcessor.h"

ISODrumsAudioProcessor::ISODrumsAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                     .withInput("Input",  juce::AudioChannelSet::stereo(), true)
#endif
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
                     )
#endif
{
    // SeparationEngine loads all 5 TorchScript models from BinaryData in its
    // constructor. This is intentionally done once at plugin load time so
    // separation calls have no warm-up cost.
}

ISODrumsAudioProcessor::~ISODrumsAudioProcessor()
{
    // Detach all transport sources before destruction
    activeTransport.store(nullptr);
    transportInput  .setSource(nullptr);
    transportKick   .setSource(nullptr);
    transportSnare  .setSource(nullptr);
    transportToms   .setSource(nullptr);
    transportHihat  .setSource(nullptr);
    transportCymbals.setSource(nullptr);
}

const juce::String ISODrumsAudioProcessor::getName() const { return JucePlugin_Name; }

bool ISODrumsAudioProcessor::acceptsMidi() const  { return false; }
bool ISODrumsAudioProcessor::producesMidi() const { return false; }
bool ISODrumsAudioProcessor::isMidiEffect() const { return false; }
double ISODrumsAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ISODrumsAudioProcessor::getNumPrograms() { return 1; }
int ISODrumsAudioProcessor::getCurrentProgram() { return 0; }
void ISODrumsAudioProcessor::setCurrentProgram(int) {}
const juce::String ISODrumsAudioProcessor::getProgramName(int) { return {}; }
void ISODrumsAudioProcessor::changeProgramName(int, const juce::String&) {}

void ISODrumsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    transportInput  .prepareToPlay(samplesPerBlock, sampleRate);
    transportKick   .prepareToPlay(samplesPerBlock, sampleRate);
    transportSnare  .prepareToPlay(samplesPerBlock, sampleRate);
    transportToms   .prepareToPlay(samplesPerBlock, sampleRate);
    transportHihat  .prepareToPlay(samplesPerBlock, sampleRate);
    transportCymbals.prepareToPlay(samplesPerBlock, sampleRate);
}

void ISODrumsAudioProcessor::releaseResources()
{
    transportInput  .releaseResources();
    transportKick   .releaseResources();
    transportSnare  .releaseResources();
    transportToms   .releaseResources();
    transportHihat  .releaseResources();
    transportCymbals.releaseResources();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ISODrumsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
}
#endif

void ISODrumsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    if (auto* t = activeTransport.load())
        t->getNextAudioBlock(juce::AudioSourceChannelInfo(buffer));
    else
        buffer.clear();
}

bool ISODrumsAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ISODrumsAudioProcessor::createEditor()
{
    return new ISODrumsAudioProcessorEditor(*this);
}

void ISODrumsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ignoreUnused(destData);
}

void ISODrumsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ISODrumsAudioProcessor();
}
