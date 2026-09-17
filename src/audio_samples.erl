%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_samples).
-moduledoc """
CPU-side PCM samples.

It builds and inspects `audio:samples()` tuples. Interleaved little-endian
`f32` or `s16`. Empty samples are a valid term; they are not a valid clip.

```erlang
Silence = audio_samples:silence(f32, 48000, #{channels => 1, frames => 480}),
0.01 = audio_samples:duration(Silence).
```

It does not require a running audio context.
""".

-export([
    from_binary/4,
    format/1,
    sample_rate/1,
    channels/1,
    frame_count/1,
    data/1,
    duration/1,
    convert/2,
    crop/3,
    silence/3
]).

-doc """
Build samples from an interleaved PCM binary.

`FrameCount` is inferred from `byte_size`. A size that is not a multiple of
the frame size is a `function_clause`.
""".
-spec from_binary(
    audio:sample_format(),
    audio:sample_rate(),
    audio:channels(),
    binary()
) -> audio:samples().
from_binary(f32, Rate, 1, Data)
  when is_integer(Rate), Rate > 0, is_binary(Data), byte_size(Data) rem 4 =:= 0 ->
    {f32, Rate, 1, byte_size(Data) div 4, Data};
from_binary(f32, Rate, 2, Data)
  when is_integer(Rate), Rate > 0, is_binary(Data), byte_size(Data) rem 8 =:= 0 ->
    {f32, Rate, 2, byte_size(Data) div 8, Data};
from_binary(s16, Rate, 1, Data)
  when is_integer(Rate), Rate > 0, is_binary(Data), byte_size(Data) rem 2 =:= 0 ->
    {s16, Rate, 1, byte_size(Data) div 2, Data};
from_binary(s16, Rate, 2, Data)
  when is_integer(Rate), Rate > 0, is_binary(Data), byte_size(Data) rem 4 =:= 0 ->
    {s16, Rate, 2, byte_size(Data) div 4, Data}.

-doc "The sample format.".
-spec format(audio:samples()) -> audio:sample_format().
format({Format, _Rate, _Channels, _Frames, _Data}) ->
    Format.

-doc "The sample rate in Hertz.".
-spec sample_rate(audio:samples()) -> audio:sample_rate().
sample_rate({_Format, Rate, _Channels, _Frames, _Data}) ->
    Rate.

-doc "The channel count.".
-spec channels(audio:samples()) -> audio:channels().
channels({_Format, _Rate, Channels, _Frames, _Data}) ->
    Channels.

-doc "The frame count.".
-spec frame_count(audio:samples()) -> audio:frame_count().
frame_count({_Format, _Rate, _Channels, Frames, _Data}) ->
    Frames.

-doc "The interleaved PCM binary.".
-spec data(audio:samples()) -> binary().
data({_Format, _Rate, _Channels, _Frames, Data}) ->
    Data.

-doc """
Duration in seconds.

It is `FrameCount / SampleRate` as a float.
""".
-spec duration(audio:samples()) -> float().
duration({_Format, Rate, _Channels, Frames, _Data}) ->
    Frames / Rate.

-doc """
Convert format and/or channel count.

Format convert is s16 ↔ f32. Stereo→mono is the arithmetic mean of L and R.
Mono→stereo copies the sample to both channels. There is no resample.
""".
-spec convert(audio:samples(), audio:sample_format() | #{
    format => audio:sample_format(),
    channels => audio:channels()
}) -> {ok, audio:samples()} | {error, audio:error()}.
convert(Samples, Format) when Format =:= f32; Format =:= s16 ->
    convert(Samples, #{format => Format});
convert({Format, Rate, Channels, Frames, Data}, Opts) when is_map(Opts) ->
    OutFormat = maps:get(format, Opts, Format),
    OutChannels = maps:get(channels, Opts, Channels),
    case {valid_format(OutFormat), valid_channels(OutChannels)} of
        {true, true} ->
            AfterFormat = convert_format(Format, OutFormat, Channels, Data),
            AfterChannels = convert_channels(
                OutFormat,
                Channels,
                OutChannels,
                AfterFormat
            ),
            {ok, {OutFormat, Rate, OutChannels, Frames, AfterChannels}};
        {false, _} ->
            {error, badarg};
        {_, false} ->
            {error, unsupported_channels}
    end.

-doc """
Crop a frame range.

A range outside the samples is a `function_clause`.
""".
-spec crop(
    audio:samples(),
    non_neg_integer(),
    audio:frame_count()
) -> audio:samples().
crop({Format, Rate, Channels, Frames, Data}, Start, Count)
  when is_integer(Start), Start >= 0,
       is_integer(Count), Count >= 0,
       Start + Count =< Frames ->
    FrameBytes = bytes_per_frame(Format, Channels),
    Part = binary:part(Data, Start * FrameBytes, Count * FrameBytes),
    {Format, Rate, Channels, Count, Part}.

-doc "Build a silent samples term.".
-spec silence(
    audio:sample_format(),
    audio:sample_rate(),
    #{channels := audio:channels(), frames := audio:frame_count()}
) -> audio:samples().
silence(Format, Rate, #{channels := Channels, frames := Frames})
  when (Format =:= f32 orelse Format =:= s16),
       is_integer(Rate), Rate > 0,
       (Channels =:= 1 orelse Channels =:= 2),
       is_integer(Frames), Frames >= 0 ->
    Bytes = Frames * bytes_per_frame(Format, Channels),
    {Format, Rate, Channels, Frames, <<0:(Bytes * 8)>>}.

valid_format(f32) -> true;
valid_format(s16) -> true;
valid_format(_) -> false.

valid_channels(1) -> true;
valid_channels(2) -> true;
valid_channels(_) -> false.

bytes_per_sample(f32) -> 4;
bytes_per_sample(s16) -> 2.

bytes_per_frame(Format, Channels) ->
    bytes_per_sample(Format) * Channels.

convert_format(Format, Format, _Channels, Data) ->
    Data;
convert_format(s16, f32, _Channels, Data) ->
    << <<(S / 32768.0):32/float-little>> || <<S:16/signed-little>> <= Data >>;
convert_format(f32, s16, _Channels, Data) ->
    << <<(clamp_s16(F)):16/signed-little>> || <<F:32/float-little>> <= Data >>.

convert_channels(_Format, Channels, Channels, Data) ->
    Data;
convert_channels(f32, 2, 1, Data) ->
    << <<((L + R) / 2.0):32/float-little>>
        || <<L:32/float-little, R:32/float-little>> <= Data >>;
convert_channels(s16, 2, 1, Data) ->
    << <<((L + R) div 2):16/signed-little>>
        || <<L:16/signed-little, R:16/signed-little>> <= Data >>;
convert_channels(f32, 1, 2, Data) ->
    << <<F:32/float-little, F:32/float-little>>
        || <<F:32/float-little>> <= Data >>;
convert_channels(s16, 1, 2, Data) ->
    << <<S:16/signed-little, S:16/signed-little>>
        || <<S:16/signed-little>> <= Data >>.

clamp_s16(F) when F > 1.0 -> 32767;
clamp_s16(F) when F < -1.0 -> -32768;
clamp_s16(F) -> trunc(F * 32767.0).
