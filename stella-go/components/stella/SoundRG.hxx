//============================================================================
// SoundRG.hxx - Retro-Go sound backend for the Stella TIA audio core.
//
// The Stella core routes every TIA audio-register write through Sound::set().
// This subclass forwards those writes to the standalone TIASound.c generator
// (Ron Fries' emulator) and exposes a pull-style method to render the mono
// samples that retro-go's rg_audio_submit() consumes.
//============================================================================
#ifndef SOUNDRG_HXX
#define SOUNDRG_HXX

#include "Sound.hxx"
#include "bspf.hxx"

class SoundRG : public Sound
{
  public:
    SoundRG(uInt32 fragsize = 512);
    virtual ~SoundRG();

  public:
    // Called by Console after the system is built. We initialise the TIA
    // sound generator here once we know the display (and thus audio) rate.
    virtual void init(Console* console, MediaSource* mediasrc, System* system,
                      double displayframerate);

    // A TIA audio register was written. Forward it to the generator.
    virtual void set(uInt16 addr, uInt8 value, Int32 cycle);

    virtual void mute(bool state);
    virtual void reset();
    virtual bool isSuccessfullyInitialized() const;

    // Render up to `maxSamples` mono samples into `out` (signed 16-bit).
    // Returns the number of samples actually produced. retro-go duplicates
    // the mono value into left/right when submitting.
    uInt32 readSamples(Int16* out, uInt32 maxSamples);

  private:
    bool myInitialized;
    bool myMuted;
    uInt32 mySampleRate;
};

#endif // SOUNDRG_HXX
