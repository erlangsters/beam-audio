Hearable digital audio is a series of **samples**. `beam-audio` does not
split that into separate "sound" and "music" types.

A short effect and a long track are the same PCM. What changes is how the
bytes are owned:

- `audio:samples()` is CPU-side interleaved PCM (a tuple plus a binary).
- `audio_clip` is an in-memory, device-ready buffer. Several `audio_player`
  voices can share one clip.
- `audio_stream` is long-form: a NIF-owned file decoder, or an Erlang-fed
  queue. One player per stream.

```erlang
ok = audio:initialize([{backend, null}]),
Samples = audio_samples:silence(f32, 48000, #{channels => 1, frames => 480}),
{ok, Clip} = audio_clip:with_samples(Samples),
{ok, Player} = audio_player:with_clip(Clip),
ok = audio_player:play(Player).
```

Playback and capture run on miniaudio's device thread. That thread never
calls Erlang. Recording is poll `read/1`. File streams have no
`UpdateMusicStream` tick.
