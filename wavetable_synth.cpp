#include "daisy_versio.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

DaisyVersio hw;

#define FS 48000U

float randomFloat01()
{
	static uint32_t xorshiftState = 1; // Cannot be initialized to 0
    xorshiftState ^= xorshiftState << 13;
    xorshiftState ^= xorshiftState >> 17;
    xorshiftState ^= xorshiftState << 5;
	return (xorshiftState >> 8) * (1.0f / 16777216.0f);
}

constexpr uint16_t N = 2048; // number of samples for wave
constexpr uint16_t H_T = 16; // total possible number of harmonics for waves
float wavetable_0[H_T][N];
float wavetable_1[H_T][N];
float amp[H_T], phase[H_T];

struct Inharmonic {
	float freq_factor;
	float amplitude;
	float theta;
	float phase;
};
Inharmonic inharmonics_0[H_T-1];
Inharmonic inharmonics_1[H_T-1];

void generateWavetable(float wavetable[][N], Inharmonic* inharmonics, uint16_t H) // Generate waves up to the H-th harmonic
{
	uint16_t h, n;
	float theta, theta_inc;
	float x_rms = 0.0f;

	// Generate random amplitudes and phases for H harmonics
	for (h = 0; h < H; h++) {
		amp[h] = randomFloat01();
		phase[h] = TWOPI_F * randomFloat01();
		x_rms += (amp[h] * amp[h]);
	}
	x_rms /= 2.0f;
	x_rms = sqrtf(x_rms);

	// Scale all amplitudes by 1/x_rms
	for (h = 0; h < H; h++) { 
		amp[h] /= x_rms;
	}

	// First harmonic (fundamental)
	theta = phase[0];
	theta_inc = TWOPI_F / N;
	for (n = 0; n < N; n++) {
		wavetable[0][n] = amp[0] * sinf(theta);
		theta += theta_inc;
	}

	// Rest of harmonics
	for (h = 1; h < H; h++) {
		theta = phase[h];
		theta_inc = TWOPI_F * (h+1) / N;
		for (n = 0; n < N; n++) {
			wavetable[h][n] = (amp[h] * sinf(theta)) + wavetable[h-1][n];
			theta += theta_inc;
			if (theta > TWOPI_F) { theta -= TWOPI_F; }
		}
	}

	// Generate random factors, amplitudes, and phases for H-1 inharmonics
	for (h = 0; h < H-1; h++) {
		inharmonics[h].freq_factor = randomFloat01() + h + 1;
		inharmonics[h].amplitude = randomFloat01() / x_rms;
		inharmonics[h].theta = TWOPI_F * randomFloat01();
		inharmonics[h].phase = inharmonics[h].theta;
	}
}

float s0, s1, wave_0, wave_1, wave_out;

float frac;
float phase_wt = 0.0f;
uint16_t idx0, idx1;

float fund_freq, out_gain, inharmonic_gain;
uint16_t H_0 = 16;
uint16_t H_1 = 2;

float lfo_freq;
float lfo, lfo_phase = 0.0f;
bool lfo_peak, lfo_trough = 0;

AdEnv env;
bool drum_mode, sync_mode;

Metro tick;

uint16_t level, level_0, level_1, ih;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		// Drum & Sync mode
		if (drum_mode && hw.Gate())  {
			env.Trigger();
			if (sync_mode && !env.IsRunning()) {
				phase_wt = 0.0f;
				lfo_phase = 0.0f;
				for (ih = 0; ih < H_0-1; ih++) {
					inharmonics_0[ih].phase = inharmonics_0[ih].theta;
				}
				for (ih = 0; ih < H_1-1; ih++) {
					inharmonics_1[ih].phase = inharmonics_1[ih].theta;
				}
			}
		}
		
		// Compute wavetable phase and indices
		idx0 = (uint16_t) phase_wt;
		idx1 = (idx0 + 1) % N;
		frac = phase_wt - idx0;
		phase_wt += fund_freq * N / FS; // Increment wavetable phase
		if (phase_wt >= N) { phase_wt -= N; }

		// Choose best table based on fundamental
		if (fund_freq >= 1600.0f)
		{
			level = (FS / (2 * fund_freq)) + 1;
			level_0 = DSY_MIN(level, H_0);
			level_1 = DSY_MIN(level, H_1);
		} else {
			level_0 = H_0;
			level_1 = H_1;
		}

		// -- Wave 0 Process --
		// Harmonics (Wavetable Synthesis)
		s0 = wavetable_0[level_0-1][idx0];
		s1 = wavetable_0[level_0-1][idx1];
		wave_0 = s0 + (s1 - s0) * frac; // linearly interpolate

		// Inharmonics (Additive Synthesis)
		for (ih = 0; ih < level_0-1; ih++) {
			inharmonics_0[ih].phase += fund_freq * inharmonics_0[ih].freq_factor / FS; // Increment phase
			if (inharmonics_0[ih].phase >= 1) { inharmonics_0[ih].phase -= 1; } // Wrap phase
			wave_0 += inharmonic_gain * inharmonics_0[ih].amplitude * sinf(TWOPI_F * inharmonics_0[ih].phase);
		}

		wave_0 *= out_gain;

		// -- Wave 1 Process --
		// Harmonics (Wavetable Synthesis)
		s0 = wavetable_1[level_1-1][idx0];
		s1 = wavetable_1[level_1-1][idx1];
		wave_1 = s0 + (s1 - s0) * frac; // linearly interpolate

		// Inharmonics (Additive Synthesis)
		for (ih = 0; ih < level_1-1; ih++) {
			inharmonics_1[ih].phase += fund_freq * inharmonics_1[ih].freq_factor / FS; // Increment phase
			if (inharmonics_1[ih].phase >= 1) { inharmonics_1[ih].phase -= 1; } // Wrap phase
			wave_1 += inharmonic_gain * inharmonics_1[ih].amplitude * sinf(TWOPI_F * inharmonics_1[ih].phase);
		}

		wave_1 *= out_gain;

		// LFO
		lfo_phase += lfo_freq / FS; // Increment phase
		if (lfo_phase >= 1) { lfo_phase -= 1; } // Wrap phase
		lfo = (0.5f * cosf(TWOPI_F * lfo_phase)) + 0.5f;

		if (lfo > 0.99f) { lfo_peak = 1; }
		else { lfo_peak = 0; }
		if (lfo < 0.01f) { lfo_trough = 1; }
		else { lfo_trough = 0; }

		// Morph between waves
		wave_out = (lfo * wave_0) + ((1 - lfo) * wave_1);
		
		// Drum mode
		if (drum_mode) { wave_out *= env.Process(); }

		out[0][i] = wave_out;
		out[1][i] = wave_out;
		
	}
}

int main(void)
{
	hw.Init();
	hw.SetAudioBlockSize(4); // number of samples handled per callback
	hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
	
	generateWavetable(wavetable_0, inharmonics_0, H_0);
	generateWavetable(wavetable_1, inharmonics_1, H_1);

	// Attack-Decay Envelope
    env.Init(FS);
    env.SetTime(ADENV_SEG_ATTACK, .004);
    env.SetTime(ADENV_SEG_DECAY, .2);
	env.SetCurve(-1);
    env.SetMax(1.0f);
    env.SetMin(0.0f);

	// Set up metro to pulse
    tick.Init(1.0f, FS);

	static uint8_t idx, H_K, H_n;
	static bool switch1_state_n = hw.sw[1].Read() == 1;
	static bool switch1_state = !switch1_state_n;
	static bool trigger_generate0 = 0;
	static bool trigger_generate1 = 0;
	static float attack, decay;

	hw.StartAdc();
	hw.StartAudio(AudioCallback);
	
	while(1) {
		// Pots & Switches
		hw.ProcessAnalogControls(); // Normalize CV inputs
		fund_freq = 27.5 * exp2f(hw.GetKnobValue(6) * 7.25f);
		out_gain = 0.25f * hw.GetKnobValue(0);
		inharmonic_gain = hw.GetKnobValue(3);
		H_n = (15.01f * hw.GetKnobValue(2)) + 1;
		drum_mode = hw.sw[0].Read() != 1;
		sync_mode = hw.sw[0].Read() == 2;
		switch1_state_n = hw.sw[1].Read() == 1;
		
		// AD Envelope
		if (drum_mode) {
			attack = (0.5f * hw.GetKnobValue(4)) + .003f;
			decay  = (2.0f * hw.GetKnobValue(5)) + 0.008f;
			env.SetTime(ADENV_SEG_ATTACK, attack);
    		env.SetTime(ADENV_SEG_DECAY, decay);
		}

		// LFO
		lfo_freq = (6.95f * hw.GetKnobValue(1)) + 0.05f;
		if (sync_mode && (lfo_freq > 6.99f)) { lfo_freq = 0.45f / (attack + decay); }

		// LED Update
		if ((H_K != H_n) || (switch1_state != switch1_state_n)) {
			for(idx = 0; idx < 4; idx++)
			{
				if ((H_n - 1) & (8 >> idx)) {
					hw.SetLed(idx, switch1_state_n, !switch1_state_n, 0);
				} else {
					hw.SetLed(idx, 0, 0, 0);
				}
			}
			hw.UpdateLeds();
		}

		H_K = H_n;
		switch1_state = switch1_state_n;
		
		
		// Button 
		hw.tap.Debounce();
		if (hw.SwitchPressed() || (!drum_mode && hw.Gate())) {
			if (switch1_state) { trigger_generate0 = 1; } 
			else { trigger_generate1 = 1; }
		}

		// Wavetable Generation
		if (trigger_generate0 && lfo_trough) {
			H_0 = H_K;
			generateWavetable(wavetable_0, inharmonics_0, H_0);
			trigger_generate0 = 0;
		}
		if (trigger_generate1 && lfo_peak) {
			H_1 = H_K;
			generateWavetable(wavetable_1, inharmonics_1, H_1);
			trigger_generate1 = 0;
		}

	}
}
