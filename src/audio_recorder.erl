%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_recorder).
-moduledoc """
Capture session.

It is an opaque resource with poll `read/1`. There is no callback behaviour.
`open/1` does not start capture; `start/1` does.

```erlang
{ok, Rec} = audio_recorder:open([]),
ok = audio_recorder:start(Rec),
{ok, Samples} = audio_recorder:read(Rec),
ok = audio_recorder:destroy(Rec).
```

On the null backend, `read/1` eventually returns zeroed `f32` after start.
`empty` is not success while `running/1` is `true`.
""".

-export_type([object/0, option/0]).
-export([
    open/0,
    open/1,
    destroy/1,
    start/1,
    stop/1,
    running/1,
    read/1,
    read/2,
    overrun/1
]).

-include("audio.hrl").

-doc "An opaque recorder resource.".
-opaque object() :: reference().

-doc "A recorder option.".
-type option() ::
    {device, default | audio:device_id()} |
    {sample_rate, audio:sample_rate()} |
    {channels, audio:channels()} |
    {format, audio:sample_format()} |
    {buffer_frames, pos_integer()}.

-doc "Open a recorder with defaults.".
-spec open() -> {ok, object()} | {error, audio:error()}.
open() ->
    open([]).

-doc """
Open a capture session.

It does not start capture. Defaults are `f32`, the engine rate and channels,
device `default`, and a 1-second ring.
""".
-spec open([option()]) -> {ok, object()} | {error, audio:error()}.
open(Options) when is_list(Options) ->
    Rate0 = case audio_context:sample_rate() of
        {error, not_initialized} -> ?AUDIO_DEFAULT_SAMPLE_RATE;
        R -> R
    end,
    Channels0 = case audio_context:channels() of
        {error, not_initialized} -> ?AUDIO_DEFAULT_CHANNELS;
        C -> C
    end,
    case parse_options(Options, #{
        format => ?AUDIO_DEFAULT_FORMAT,
        sample_rate => Rate0,
        channels => Channels0,
        buffer_frames => Rate0,
        device => default
    }) of
        {ok, #{
            format := Format,
            sample_rate := Rate,
            channels := Channels,
            buffer_frames := Buffer,
            device := Device
        }} ->
            wrap_new(audio_nif:recorder_open(Format, Rate, Channels, Buffer, Device));
        {error, _} = Error ->
            Error
    end.

-doc "Destroy a recorder.".
-spec destroy(object()) -> ok | {error, invalid_resource}.
destroy(Recorder) ->
    case audio_nif:recorder_destroy(Recorder) of
        ok ->
            audio_context:unregister(Recorder),
            ok;
        Other ->
            Other
    end.

-doc "Start capture.".
-spec start(object()) -> ok | {error, audio:error()}.
start(Recorder) ->
    audio_nif:recorder_start(Recorder).

-doc "Stop capture.".
-spec stop(object()) -> ok | {error, audio:error()}.
stop(Recorder) ->
    audio_nif:recorder_stop(Recorder).

-doc "Whether capture is running.".
-spec running(object()) -> boolean() | {error, audio:error()}.
running(Recorder) ->
    audio_nif:recorder_running(Recorder).

-doc """
Read all currently queued frames.

`empty` if none. A successful read clears the sticky overrun flag.
""".
-spec read(object()) -> {ok, audio:samples()} | empty | {error, audio:error()}.
read(Recorder) ->
    read(Recorder, 0).

-doc "Read up to `MaxFrames` queued frames.".
-spec read(object(), pos_integer() | 0) ->
    {ok, audio:samples()} | empty | {error, audio:error()}.
read(Recorder, MaxFrames) when is_integer(MaxFrames), MaxFrames >= 0 ->
    case audio_nif:recorder_read(Recorder, MaxFrames) of
        {ok, {Format, Rate, Channels, Frames, Data}} ->
            {ok, {Format, Rate, Channels, Frames, Data}};
        Other ->
            Other
    end.

-doc """
Sticky overrun flag.

It is set when the capture ring dropped oldest frames. It is cleared by
`read/1,2` when that call returns data or empty after the flag was set.
""".
-spec overrun(object()) -> boolean() | {error, audio:error()}.
overrun(Recorder) ->
    audio_nif:recorder_overrun(Recorder).

wrap_new({ok, Rec}) ->
    try audio_context:register(Rec, self(), recorder) of
        ok ->
            {ok, Rec};
        {error, Reason} ->
            audio_nif:recorder_destroy(Rec),
            {error, Reason}
    catch
        Class:Reason:Stack ->
            audio_nif:recorder_destroy(Rec),
            erlang:raise(Class, Reason, Stack)
    end;
wrap_new(Error) ->
    Error.

parse_options([], Acc) ->
    {ok, Acc};
parse_options([{format, Format} | Rest], Acc) when Format =:= f32; Format =:= s16 ->
    parse_options(Rest, Acc#{format => Format});
parse_options([{sample_rate, Rate} | Rest], Acc) when is_integer(Rate), Rate > 0 ->
    parse_options(Rest, Acc#{sample_rate => Rate});
parse_options([{channels, Channels} | Rest], Acc) when Channels =:= 1; Channels =:= 2 ->
    parse_options(Rest, Acc#{channels => Channels});
parse_options([{buffer_frames, Frames} | Rest], Acc)
  when is_integer(Frames), Frames > 0 ->
    parse_options(Rest, Acc#{buffer_frames => Frames});
parse_options([{device, default} | Rest], Acc) ->
    parse_options(Rest, Acc#{device => default});
parse_options([{device, Id} | Rest], Acc) when is_binary(Id) ->
    parse_options(Rest, Acc#{device => Id});
parse_options(_, _) ->
    {error, badarg}.
