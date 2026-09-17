%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_samples_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null — no engine required

duration_and_silence_test() ->
    Silence = audio_samples:silence(f32, 48000, #{channels => 1, frames => 480}),
    0.01 = audio_samples:duration(Silence),
    f32 = audio_samples:format(Silence),
    48000 = audio_samples:sample_rate(Silence),
    1 = audio_samples:channels(Silence),
    480 = audio_samples:frame_count(Silence),
    ok.

from_binary_test() ->
    Data = <<0, 0, 128, 63>>,
    Samples = audio_samples:from_binary(f32, 48000, 1, Data),
    1 = audio_samples:frame_count(Samples),
    Data = audio_samples:data(Samples),
    ?assertError(function_clause, audio_samples:from_binary(f32, 48000, 1, <<1, 2, 3>>)),
    ok.

convert_format_and_channels_test() ->
    Stereo = audio_samples:from_binary(
        f32,
        48000,
        2,
        <<0:32/float-little, 1:32/float-little>>
    ),
    {ok, Mono} = audio_samples:convert(Stereo, #{channels => 1}),
    1 = audio_samples:channels(Mono),
    <<Avg:32/float-little>> = audio_samples:data(Mono),
    true = abs(Avg - 0.5) < 0.0001,
    {ok, S16} = audio_samples:convert(Mono, s16),
    s16 = audio_samples:format(S16),
    {ok, Back} = audio_samples:convert(S16, f32),
    <<F:32/float-little>> = audio_samples:data(Back),
    true = abs(F - 0.5) < 0.01,
    ok.

crop_test() ->
    Samples = audio_samples:silence(s16, 8000, #{channels => 1, frames => 10}),
    Cropped = audio_samples:crop(Samples, 2, 3),
    3 = audio_samples:frame_count(Cropped),
    ?assertError(function_clause, audio_samples:crop(Samples, 8, 4)),
    ok.
