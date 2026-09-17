%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_wav_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null — codec does not need the engine

roundtrip_test() ->
    Samples = audio_samples:silence(s16, 8000, #{channels => 1, frames => 64}),
    {ok, Binary} = audio_wav:encode(Samples),
    <<"RIFF", _:32, "WAVE", _/binary>> = Binary,
    {ok, Decoded} = audio_wav:decode(Binary),
    s16 = audio_samples:format(Decoded),
    8000 = audio_samples:sample_rate(Decoded),
    1 = audio_samples:channels(Decoded),
    64 = audio_samples:frame_count(Decoded),
    {error, unsupported_format} = audio_wav:decode(<<"not a wav">>),
    ok.

tmp_dir() ->
    case os:getenv("TEMP") of
        false ->
            os:getenv("TMPDIR", "/tmp");
        Dir ->
            Dir
    end.

load_save_test() ->
    Samples = audio_samples:silence(f32, 16000, #{channels => 2, frames => 32}),
    Path = filename:join(tmp_dir(), "beam-audio-wav-test.wav"),
    ok = audio_wav:save(Samples, Path),
    {ok, Decoded} = audio_wav:load(Path),
    f32 = audio_samples:format(Decoded),
    2 = audio_samples:channels(Decoded),
    32 = audio_samples:frame_count(Decoded),
    ok = file:delete(Path),
    {error, enoent} = audio_wav:load("beam-audio-missing.wav"),
    ok.
