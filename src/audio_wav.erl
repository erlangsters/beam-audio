%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_wav).
-moduledoc """
WAV codec.

It decodes and encodes PCM `s16` and IEEE `f32` WAV as `audio:samples()`.
It does not require a running audio context.

```erlang
{ok, Samples} = audio_wav:decode(Binary),
{ok, Binary2} = audio_wav:encode(Samples).
```

A binary that is not RIFF/WAVE is `{error, unsupported_format}`. Engine-side
f32 larger than 256 MiB is `{error, too_large}`.
""".

-export([
    decode/1,
    encode/1,
    load/1,
    save/2
]).

-doc """
Decode a WAV binary.

A different container is `{error, unsupported_format}` before the decoder
runs.
""".
-spec decode(binary()) ->
    {ok, audio:samples()} |
    {error, unsupported_format | decode_failed | out_of_memory | too_large}.
decode(Binary) when is_binary(Binary) ->
    case Binary of
        <<"RIFF", _:32, "WAVE", _/binary>> ->
            case audio_nif:wav_decode(Binary) of
                {ok, {Format, Rate, Channels, Frames, Data}} ->
                    {ok, {Format, Rate, Channels, Frames, Data}};
                Error ->
                    Error
            end;
        _ ->
            {error, unsupported_format}
    end.

-doc """
Encode samples as a WAV binary.

Tagged errors only.
""".
-spec encode(audio:samples()) ->
    {ok, binary()} | {error, encode_failed | out_of_memory | too_large}.
encode({Format, Rate, Channels, Frames, Data})
  when (Format =:= f32 orelse Format =:= s16),
       is_integer(Rate), Rate > 0,
       (Channels =:= 1 orelse Channels =:= 2),
       is_integer(Frames), Frames > 0,
       is_binary(Data) ->
    audio_nif:wav_encode(Format, Rate, Channels, Frames, Data).

-doc "Load a WAV file.".
-spec load(file:name_all()) -> {ok, audio:samples()} | {error, audio:error()}.
load(Path) when is_list(Path); is_binary(Path) ->
    case file:read_file(Path) of
        {ok, Binary} ->
            decode(Binary);
        {error, Reason} ->
            {error, Reason}
    end.

-doc """
Save samples as a WAV file.

The samples come first, then the path.
""".
-spec save(audio:samples(), file:name_all()) -> ok | {error, audio:error()}.
save(Samples, Path) when is_list(Path); is_binary(Path) ->
    case encode(Samples) of
        {ok, Binary} ->
            file:write_file(Path, Binary);
        {error, _} = Error ->
            Error
    end.
