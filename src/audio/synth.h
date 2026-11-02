// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/audio.md, section "synth.h and synth.c"
// ============================================================================
#ifndef SYNTH_H
#define SYNTH_H

// A small sound synthesiser. A recipe of up to four layers (an oscillator or noise, a pitch glide, an envelope, a low-pass
// that sweeps, a soft clip) is rendered once into 16 bit mono samples. It knows nothing about raylib or the sound card,
// so the tests can check every number it makes, and the same recipe gives the same samples on every machine.

#define SYNTH_RATE       22050
#define SYNTH_MAX_LAYERS 4
#define SYNTH_MAX_SECS   4.0f

typedef enum { SYN_SINE = 0, SYN_SQUARE = 1, SYN_SAW = 2, SYN_TRIANGLE = 3, SYN_NOISE = 4 } SynWave;

typedef struct {
    int   wave;          // SynWave
    float f0, f1;        // Hz at the start and at the end of the layer, gliding between them (ignored for noise)
    float level;         // gain of this layer
    float delay;         // seconds before the layer starts
    float attack;        // seconds to rise to full level
    float decay;         // seconds for the level to fall to 1/e after the attack: small is a click, large is a hum
    float length;        // seconds the layer lasts; 0 = until it has faded to -60 dB (attack + 6.9 * decay)
    float lp0, lp1;      // low-pass cutoff in Hz at the start and at the end of the layer; 0 = no filter
    float drive;         // 0 = clean; above 0 pushes the layer into a soft clip (grit)
} SynLayer;

typedef struct {
    int      layers;
    SynLayer layer[SYNTH_MAX_LAYERS];
    float    duration;   // total seconds; 0 = as long as the longest layer
    float    loopFade;   // above 0: the end is cross-faded into the start so the sound repeats without a click
} SynRecipe;

// How many samples Synth_Render will write for this recipe (at least 1, at most SYNTH_MAX_SECS worth).
int Synth_Frames(const SynRecipe *r, int rate);

// Renders into out (capacity cap samples) and returns the number written, never more than Synth_Frames. The seed picks the
// noise: the same recipe and seed always give the same samples, another seed gives another take. The result never clips:
// when the layers add up above 0.98 the whole sound is scaled down to 0.98 (it is never scaled up).
int Synth_Render(const SynRecipe *r, unsigned seed, int rate, short *out, int cap);

#endif
