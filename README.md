# Audio library for the BEAM

[![Erlangsters Repository](https://img.shields.io/badge/erlangsters-beam--audio-%23a90432)](https://github.com/erlangsters/beam-audio)
![Supported Erlang/OTP Versions](https://img.shields.io/badge/erlang%2Fotp-28-%23a90432)
![Current Version](https://img.shields.io/badge/version-0.0.1-%23354052)
![License](https://img.shields.io/github/license/erlangsters/beam-audio)
[![Build Status](https://img.shields.io/github/actions/workflow/status/erlangsters/beam-audio/build.yml)](https://github.com/erlangsters/beam-audio/actions/workflows/build.yml)
[![Documentation Link](https://img.shields.io/badge/documentation-available-yellow)](http://erlangsters.github.io/beam-audio/)

:construction: It is a 0.0.1 development release. Use at your own risk.

The missing audio library of the BEAM ecosystem available for the Erlang and Elixir programming language.

It is a native-backed library on miniaudio 0.11.25, not a miniaudio binding. Public modules are `audio_*` with types on `audio`.

Linux is the runtime-verified platform: compile plus EUnit on the miniaudio null backend. macOS and Windows compile and run that same suite in CI. Hardware playback, capture, and device enumeration are not a 0.0.1 claim.

Written by the Erlangsters [community](https://about.erlangsters.org/) and released under the MIT [license](https://opensource.org/license/mit).

## Getting started

Initialize with the null backend in tests. A real device uses `initialize/0` or `initialize([])` and is a separate hardware verification, not CI.

```erlang
ok = audio:initialize([{backend, null}]),
Rate = 48000,
Frames = 480,
Sine = <<
    <<(math:sin(2.0 * math:pi() * 440.0 * I / Rate)):32/float-little>>
    || I <- lists:seq(0, Frames - 1)
>>,
Samples = audio_samples:from_binary(f32, Rate, 1, Sine),
{ok, Clip} = audio_clip:with_samples(Samples),
{ok, Player} = audio_player:with_clip(Clip),
ok = audio_player:play(Player),
ok = audio_player:destroy(Player),
ok = audio_clip:destroy(Clip),
ok = audio:terminate().
```

Compile success and EUnit on `{backend, null}` are not hardware playback.

Scope:

- Audio playback (in-memory clips, concurrent voices)
- Audio recording (poll `read/1`)
- WAV decode/encode
- Queued and file-backed streams
- Basic audio effects (later)
- Other audio formats (later)

## Installing the library

To use beam-audio in a rebar3 project, add it to your rebar.config.

```erlang
{deps, [
  {beam_audio, {git, "https://github.com/erlangsters/beam-audio.git", {tag, "0.0.1"}}}
]}.
```
