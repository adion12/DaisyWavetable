# DaisyWavetable

A wavetable synthesizer module for the Daisy Seed, developed on the Versio platform. https://noiseengineering.us/pages/world-of-versio/

**Controls/Parameters:**

1. Output Gain
2. LFO Frequency - frequency of sinusoid low-frequency oscillator that modulates the morphing between two waveforms (min=0.05 Hz, max=7 Hz).
3. Harmonics - number of harmonics that will be generated (min=1, max=16). The amplitudes and phases are randomized in generation. This knob also controls the number of inharmonics that will be generated (min=0, max=15). The number of harmonics above the fundamental is displayed in binary on the LEDs. 
4. Inharmonics - gain for inharmonic content added to generated waveform (min=0, max=1).
5. Attack - attack time of AD envelope when in drum mode (min=3ms, max=503ms).
6. Decay - decay time of AD envelope when in drum mode (min=8ms, max=2s).
7. Pitch - fundamental frequency of both waveforms (min=27.5 Hz, max=4186 Hz).
8. Play Mode - selects between oscillator mode (A), drum mode (B), and drum-sync mode (C).
9. Select Wave - select for generation for waveform 1 (X) or waveform 2 (Y)
10. Generate (FSU Button) - generate waveform given wave selection and "Harmonics" parameter. Wave generation will occur at LFO's peaks and troughs to avoid audible clipping in output.
11. FSU (FSU CV jack) - Trigger input for drum mode
