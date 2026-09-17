# Changelog

Notable user-facing changes to the Erlangsters [beam-audio](https://github.com/erlangsters/beam-audio) repository are documented here.

## 0.0.1

Initial release.

- Playback, recording, WAV, and queued or file streams on vendored miniaudio 0.11.25.
- Null-backend EUnit is the 0.0.1 verification path. Hardware I/O is not claimed.
- Linux is the runtime-verified platform. macOS and Windows compile and run the same null-backend tests in CI.
