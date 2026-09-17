%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_clip).
-moduledoc """
In-memory playable clip.

It uploads `audio:samples()` into an engine buffer. Copying the term does
not copy the native PCM. Several players may share one clip.

```erlang
{ok, Clip} = audio_clip:with_samples(Samples),
ok = audio_clip:destroy(Clip).
```

Empty samples and engine-side PCM larger than 256 MiB are rejected. Destroy
with a live player is `{error, in_use}`.
""".

-export_type([object/0]).
-export([
    with_samples/1,
    destroy/1,
    format/1,
    sample_rate/1,
    channels/1,
    frame_count/1,
    duration/1
]).

-include("audio.hrl").

-doc "An opaque clip resource.".
-opaque object() :: reference().

-doc """
Upload samples as a clip.

Empty clips are `{error, badarg}`. Engine-side f32 bytes above 256 MiB are
`{error, too_large}`.
""".
-spec with_samples(audio:samples()) -> {ok, object()} | {error, audio:error()}.
with_samples({Format, Rate, Channels, Frames, Data})
  when (Format =:= f32 orelse Format =:= s16),
       is_integer(Rate), Rate > 0,
       (Channels =:= 1 orelse Channels =:= 2),
       is_integer(Frames), Frames >= 0,
       is_binary(Data) ->
    case audio_nif:clip_with_samples(Format, Rate, Channels, Frames, Data) of
        {ok, Clip} ->
            try audio_context:register(Clip, self(), clip) of
                ok ->
                    {ok, Clip};
                {error, Reason} ->
                    audio_nif:clip_force_uninit(Clip),
                    {error, Reason}
            catch
                Class:Reason:Stack ->
                    audio_nif:clip_force_uninit(Clip),
                    erlang:raise(Class, Reason, Stack)
            end;
        Error ->
            Error
    end.

-doc """
Destroy a clip.

A live player holding the clip is `{error, in_use}`. An already-dead clip is
`{error, invalid_resource}`.
""".
-spec destroy(object()) -> ok | {error, invalid_resource | in_use}.
destroy(Clip) ->
    case audio_nif:clip_destroy(Clip) of
        ok ->
            audio_context:unregister(Clip),
            ok;
        {error, in_use} ->
            audio_context:unregister(Clip),
            {error, in_use};
        Other ->
            Other
    end.

-doc "The original sample format.".
-spec format(object()) -> audio:sample_format() | {error, audio:error()}.
format(Clip) ->
    case audio_nif:clip_info(Clip) of
        {ok, {Format, _Rate, _Channels, _Frames}} ->
            Format;
        Error ->
            Error
    end.

-doc "The clip sample rate.".
-spec sample_rate(object()) -> audio:sample_rate() | {error, audio:error()}.
sample_rate(Clip) ->
    case audio_nif:clip_info(Clip) of
        {ok, {_Format, Rate, _Channels, _Frames}} ->
            Rate;
        Error ->
            Error
    end.

-doc "The clip channel count.".
-spec channels(object()) -> audio:channels() | {error, audio:error()}.
channels(Clip) ->
    case audio_nif:clip_info(Clip) of
        {ok, {_Format, _Rate, Channels, _Frames}} ->
            Channels;
        Error ->
            Error
    end.

-doc "The clip frame count.".
-spec frame_count(object()) -> audio:frame_count() | {error, audio:error()}.
frame_count(Clip) ->
    case audio_nif:clip_info(Clip) of
        {ok, {_Format, _Rate, _Channels, Frames}} ->
            Frames;
        Error ->
            Error
    end.

-doc "Duration in seconds.".
-spec duration(object()) -> float() | {error, audio:error()}.
duration(Clip) ->
    case audio_nif:clip_info(Clip) of
        {ok, {_Format, Rate, _Channels, Frames}} ->
            Frames / Rate;
        Error ->
            Error
    end.
