%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_stream).
-moduledoc """
File-backed or queued PCM stream.

File streams are NIF-owned WAV decoders with no Erlang update tick. Queue
streams are filled with `write/2`. A stream has one player; a second
`audio_player:with_stream/1` is `{error, in_use}`.

```erlang
{ok, Stream} = audio_stream:with_queue([{capacity_frames, 1024}]),
ok = audio_stream:write(Stream, Samples),
{ok, Player} = audio_player:with_stream(Stream),
ok = audio_player:play(Player).
```

Destroy with a live player is `{error, in_use}`.
""".

-export_type([object/0, queue_option/0]).
-export([
    from_file/1,
    with_queue/1,
    destroy/1
]).
-export([
    write/2,
    available/1,
    queued/1,
    underrun/1,
    duration/1
]).

-include("audio.hrl").

-doc "An opaque stream resource.".
-opaque object() :: reference().

-doc "A queued-stream option.".
-type queue_option() ::
    {format, audio:sample_format()} |
    {sample_rate, audio:sample_rate()} |
    {channels, audio:channels()} |
    {capacity_frames, pos_integer()}.

-doc """
Open a file-backed WAV stream.

Non-WAV containers are `{error, unsupported_format}`. Play/stop live on
`audio_player:with_stream/1`.
""".
-spec from_file(file:name_all()) -> {ok, object()} | {error, audio:error()}.
from_file(Path) when is_list(Path); is_binary(Path) ->
    case peek_wav(Path) of
        ok ->
            wrap_new(audio_nif:stream_from_file(path_binary(Path)));
        {error, _} = Error ->
            Error
    end.

-doc """
Create an Erlang-fed PCM queue.

Defaults are `f32`, the engine sample rate and channels, and 8192 frames.
A write that does not fit is `{error, full}`.
""".
-spec with_queue([queue_option()]) -> {ok, object()} | {error, audio:error()}.
with_queue(Options) when is_list(Options) ->
    case parse_queue(Options, #{
        format => ?AUDIO_DEFAULT_FORMAT,
        sample_rate => default_rate(),
        channels => default_channels(),
        capacity_frames => 8192
    }) of
        {ok, #{
            format := Format,
            sample_rate := Rate,
            channels := Channels,
            capacity_frames := Capacity
        }} ->
            wrap_new(audio_nif:stream_with_queue(Format, Rate, Channels, Capacity));
        {error, _} = Error ->
            Error
    end.

-doc """
Destroy a stream.

A live player is `{error, in_use}`.
""".
-spec destroy(object()) -> ok | {error, invalid_resource | in_use}.
destroy(Stream) ->
    case audio_nif:stream_destroy(Stream) of
        ok ->
            audio_context:unregister(Stream),
            ok;
        {error, in_use} ->
            audio_context:unregister(Stream),
            {error, in_use};
        Other ->
            Other
    end.

-doc """
Write samples into a queued stream.

The whole chunk must fit or the call is `{error, full}` and writes nothing.
A file stream is `{error, badarg}`.
""".
-spec write(object(), audio:samples()) -> ok | {error, audio:error()}.
write(Stream, {Format, Rate, Channels, Frames, Data}) ->
    audio_nif:stream_write(Stream, Format, Rate, Channels, Frames, Data).

-doc "Free frames in a queued stream.".
-spec available(object()) -> non_neg_integer() | {error, audio:error()}.
available(Stream) ->
    audio_nif:stream_available(Stream).

-doc "Queued frames waiting to be mixed.".
-spec queued(object()) -> non_neg_integer() | {error, audio:error()}.
queued(Stream) ->
    audio_nif:stream_queued(Stream).

-doc "Whether the queue has underrun (played silence).".
-spec underrun(object()) -> boolean() | {error, audio:error()}.
underrun(Stream) ->
    audio_nif:stream_underrun(Stream).

-doc """
Stream duration in seconds.

File streams return the decoder length. Queued streams are unbounded and
return `undefined`. Playback cursor lives on `audio_player:cursor/1`.
""".
-spec duration(object()) -> float() | undefined | {error, audio:error()}.
duration(Stream) ->
    audio_nif:stream_duration(Stream).

wrap_new({ok, Stream}) ->
    try audio_context:register(Stream, self(), stream) of
        ok ->
            {ok, Stream};
        {error, Reason} ->
            audio_nif:stream_force_uninit(Stream),
            {error, Reason}
    catch
        Class:Reason:Stack ->
            audio_nif:stream_force_uninit(Stream),
            erlang:raise(Class, Reason, Stack)
    end;
wrap_new(Error) ->
    Error.

path_binary(Path) when is_binary(Path) ->
    Path;
path_binary(Path) when is_list(Path) ->
    unicode:characters_to_binary(Path).

peek_wav(Path) ->
    case file:open(Path, [read, raw, binary]) of
        {ok, Fd} ->
            case file:read(Fd, 12) of
                {ok, <<"RIFF", _:32, "WAVE">>} ->
                    file:close(Fd),
                    ok;
                {ok, _} ->
                    file:close(Fd),
                    {error, unsupported_format};
                eof ->
                    file:close(Fd),
                    {error, unsupported_format};
                {error, Reason} ->
                    file:close(Fd),
                    {error, Reason}
            end;
        {error, Reason} ->
            {error, Reason}
    end.

parse_queue([], Acc) ->
    {ok, Acc};
parse_queue([{format, Format} | Rest], Acc) when Format =:= f32; Format =:= s16 ->
    parse_queue(Rest, Acc#{format => Format});
parse_queue([{sample_rate, Rate} | Rest], Acc) when is_integer(Rate), Rate > 0 ->
    parse_queue(Rest, Acc#{sample_rate => Rate});
parse_queue([{channels, Channels} | Rest], Acc) when Channels =:= 1; Channels =:= 2 ->
    parse_queue(Rest, Acc#{channels => Channels});
parse_queue([{capacity_frames, Capacity} | Rest], Acc)
  when is_integer(Capacity), Capacity > 0 ->
    parse_queue(Rest, Acc#{capacity_frames => Capacity});
parse_queue(_, _) ->
    {error, badarg}.

default_rate() ->
    case audio_context:sample_rate() of
        {error, not_initialized} ->
            ?AUDIO_DEFAULT_SAMPLE_RATE;
        Rate ->
            Rate
    end.

default_channels() ->
    case audio_context:channels() of
        {error, not_initialized} ->
            ?AUDIO_DEFAULT_CHANNELS;
        Channels ->
            Channels
    end.
