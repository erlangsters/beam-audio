# beam-audio

- Native-backed library in the `beam-platform` family.
- Public modules are `audio_*`. Types live on `audio`. OTP application is `beam_audio`.
- Vendored miniaudio 0.11.25 (`9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`) in `c_src/miniaudio/miniaudio.h`. Do not use Debian `libminiaudio-dev`. Do not copy raylib's header. Bump only as a logged decision.
- NIF target is `beam-audio`, written to `priv/` by rebar3 CMake hooks. Erlang loader uses `LibName = "beam-audio"`.
- EUnit uses `{backend, null}`. Compile plus that suite is the default verification. Hardware playback, capture, and enumeration must be logged in `beam-audio-logs.md` before they appear in the README.
- Runtime Erlang dependency is `worker` (and `spawn_mode` transitively). No `beam-graphics` or `beam-window`.
- This NIF uses `erl_nif.h` only. Do not link `ei`.
- On Apple, build with `MA_NO_RUNTIME_LINKING` and link CoreAudio, AudioToolbox, and CoreFoundation.
