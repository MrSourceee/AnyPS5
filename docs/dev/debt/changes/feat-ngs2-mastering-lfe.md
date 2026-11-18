# libSceNgs2.native

## change [NGS2 mastering voice LFE, limiter and peak meter params]

- [NGS2 mastering voice LFE, limiter and peak meter params](../../../core/libs/prx/libSceNgs2.native/src/Voice.cpp) (rack `0x3000`) - enabled LFE throws outside matching 4/6/8 channels at 48 kHz or across an unmeasured pause/restart without setup. Only channel 3 is filtered; `fc <= 80` selects 80 Hz, otherwise 120 Hz. These are calculated Chebyshev I coefficients, not bit-exact console data. [PS5 13.590.001](https://github.com/boykopovar/AnyPS5/issues/8039#issuecomment-6792157201): impulses, bypass, setup reset; [13.42 DSP on a PC](https://github.com/boykopovar/AnyPS5/issues/21876#issuecomment-6948766633): two biquads keep history across coefficient reloads. Exact rounding and routing across different layouts are unverified. Limiter still throws when enabled except for a NaN threshold; peak meter modes 0–2 change no samples. PS4 behaviour is unchanged.
