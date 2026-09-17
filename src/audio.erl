%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio).
-moduledoc """
Audio types and engine lifecycle.

It is the umbrella of `beam-audio`. Shared types live here. `initialize/1`
starts the singleton audio context; `terminate/0` stops it.

```erlang
ok = audio:initialize([{backend, null}]),
Samples = audio_samples:from_binary(f32, 48000, 1, SineBinary),
{ok, Clip} = audio_clip:with_samples(Samples),
{ok, Player} = audio_player:with_clip(Clip),
ok = audio_player:play(Player),
ok = audio_player:destroy(Player),
ok = audio_clip:destroy(Clip),
ok = audio:terminate().
```

Call `initialize/1` from a long-lived process. The caller is linked to the
context worker. Tests use `{backend, null}`.
""".

-export_type([
    error/0,
    option/0,
    options/0,
    backend/0,
    sample_format/0,
    sample_rate/0,
    channels/0,
    frame_count/0,
    samples/0,
    clip/0,
    player/0,
    stream/0,
    recorder/0,
    device_kind/0,
    device_id/0,
    device_info/0
]).
-export([
    initialize/0,
    initialize/1,
    terminate/0
]).

-include("audio.hrl").

-doc """
A tagged failure reason.

Public functions that can fail return `{ok, T} | {error, error()}`.
""".
-type error() ::
    already_initialized |
    not_initialized |
    device_failed |
    no_device |
    out_of_memory |
    invalid_resource |
    unsupported_format |
    unsupported_channels |
    decode_failed |
    encode_failed |
    format_mismatch |
    overrun |
    full |
    too_large |
    in_use |
    timeout |
    badarg |
    file:posix() |
    {aborted, term()}.

-doc """
An initialize backend.

`default` uses miniaudio's default backend order. `null` is the CI path.
""".
-type backend() :: default | null.

-doc """
An initialize option.

Unknown keys are `{error, badarg}`.
""".
-type option() ::
    {backend, backend()} |
    {sample_rate, sample_rate()} |
    {channels, channels()} |
    {device, default | device_id()} |
    {period_frames, pos_integer()}.

-doc "A list of initialize options.".
-type options() :: [option()].

-doc "A PCM sample format.".
-type sample_format() :: f32 | s16.

-doc "A sample rate in Hertz.".
-type sample_rate() :: pos_integer().

-doc "A channel count. v1 is mono or stereo.".
-type channels() :: 1 | 2.

-doc "A number of PCM frames.".
-type frame_count() :: non_neg_integer().

-doc """
CPU-side interleaved PCM.

It is a public tuple, not a NIF resource. Little-endian `f32` or `s16`.
""".
-type samples() :: {
    Format :: sample_format(),
    SampleRate :: sample_rate(),
    Channels :: channels(),
    FrameCount :: frame_count(),
    Data :: binary()
}.

-doc "An in-memory playable clip.".
-type clip() :: audio_clip:object().

-doc "A playback voice.".
-type player() :: audio_player:object().

-doc "A file-backed or queued stream.".
-type stream() :: audio_stream:object().

-doc "A capture session.".
-type recorder() :: audio_recorder:object().

-doc "A device kind.".
-type device_kind() :: playback | capture.

-doc "An encoded device identifier.".
-type device_id() :: audio_device:id().

-doc "A device list entry.".
-type device_info() :: audio_device:info().

-doc """
Initialize the audio context with defaults.

It is `initialize([])`.
""".
-spec initialize() -> ok | {error, error()}.
initialize() ->
    initialize([]).

-doc """
Initialize the audio context.

It starts the singleton worker and opens a playback device. A second call is
`{error, already_initialized}`. Device open failure is `{error, device_failed}`
or `{error, no_device}`.
""".
-spec initialize(options()) -> ok | {error, error()}.
initialize(Options) when is_list(Options) ->
    case parse_options(Options, #{
        backend => default,
        sample_rate => ?AUDIO_DEFAULT_SAMPLE_RATE,
        channels => ?AUDIO_DEFAULT_CHANNELS,
        period_frames => 0,
        device => default
    }) of
        {ok, Opts} ->
            case audio_context:start(Opts) of
                {ok, _Pid} ->
                    ok;
                {error, _} = Error ->
                    Error
            end;
        {error, _} = Error ->
            Error
    end.

-doc """
Stop the audio context.

It releases remaining resources and joins the device thread. It is `ok` when
the context is not running.
""".
-spec terminate() -> ok.
terminate() ->
    audio_context:stop().

parse_options([], Acc) ->
    {ok, Acc};
parse_options([{backend, Backend} | Rest], Acc)
  when Backend =:= default; Backend =:= null ->
    parse_options(Rest, Acc#{backend => Backend});
parse_options([{sample_rate, Rate} | Rest], Acc)
  when is_integer(Rate), Rate > 0 ->
    parse_options(Rest, Acc#{sample_rate => Rate});
parse_options([{channels, Channels} | Rest], Acc)
  when Channels =:= 1; Channels =:= 2 ->
    parse_options(Rest, Acc#{channels => Channels});
parse_options([{period_frames, Period} | Rest], Acc)
  when is_integer(Period), Period > 0 ->
    parse_options(Rest, Acc#{period_frames => Period});
parse_options([{device, default} | Rest], Acc) ->
    parse_options(Rest, Acc);
parse_options([{device, Id} | Rest], Acc) when is_binary(Id) ->
    parse_options(Rest, Acc#{device => Id});
parse_options(_, _) ->
    {error, badarg}.
