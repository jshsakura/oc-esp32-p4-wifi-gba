//============================================================================
// SoundRG.cpp - Retro-Go sound backend for the Stella TIA audio core.
//============================================================================
#include "SoundRG.hxx"
#include "TIASound.hxx"

// The Atari 2600 TIA runs its audio at half the CPU clock: 1.19MHz / 2.
#define TIA_SOUND_FREQUENCY 31400

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SoundRG::SoundRG(uInt32 fragsize)
    : Sound(fragsize),
      myInitialized(false),
      myMuted(false),
      mySampleRate(32000)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SoundRG::~SoundRG()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SoundRG::init(Console* console, MediaSource* mediasrc, System* system,
                   double displayframerate)
{
    Sound::init(console, mediasrc, system, displayframerate);

    // displayframerate is 0.0 from Console; we drive the sample rate from the
    // host (retro-go passes it in via the app). Use a sensible default that
    // the caller can override before construction via setSampleRate().
    Tia_sound_init(TIA_SOUND_FREQUENCY, mySampleRate);
    Tia_volume(100);
    myInitialized = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SoundRG::set(uInt16 addr, uInt8 value, Int32 cycle)
{
    Sound::set(addr, value, cycle);
    if (myInitialized && !myMuted)
        Update_tia_sound(addr, value);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SoundRG::mute(bool state)
{
    myMuted = state;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SoundRG::reset()
{
    // Re-initialise the generator to silence any ongoing sound.
    if (myInitialized)
    {
        Tia_sound_init(TIA_SOUND_FREQUENCY, mySampleRate);
        Tia_volume(100);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SoundRG::isSuccessfullyInitialized() const
{
    return myInitialized;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 SoundRG::readSamples(Int16* out, uInt32 maxSamples)
{
    if (!myInitialized || maxSamples == 0)
        return 0;

    // Tia_process writes unsigned 8-bit samples centred on 0x80.
    uInt8* tmp = new uInt8[maxSamples];
    Tia_process(tmp, maxSamples);

    // Convert unsigned 8-bit -> signed 16-bit (mono).
    for (uInt32 i = 0; i < maxSamples; i++)
    {
        // Subtract the 0x80 centre and scale into the 16-bit range.
        Int16 s = (Int16)((Int32)(tmp[i] - 0x80) << 8);
        out[i] = s;
    }
    delete[] tmp;
    return maxSamples;
}
