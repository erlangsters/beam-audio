%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_stream_queue_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

with_null(Fun) ->
    ok = audio:initialize([{backend, null}]),
    try
        Fun()
    after
        ok = audio:terminate()
    end.

chunk(Frames) ->
    audio_samples:silence(f32, 48000, #{channels => 1, frames => Frames}).

full_and_underrun_test() ->
    with_null(fun() ->
        {ok, Stream} = audio_stream:with_queue([
            {format, f32},
            {sample_rate, 48000},
            {channels, 1},
            {capacity_frames, 8}
        ]),
        8 = audio_stream:available(Stream),
        0 = audio_stream:queued(Stream),
        undefined = audio_stream:duration(Stream),
        ok = audio_stream:write(Stream, chunk(8)),
        0 = audio_stream:available(Stream),
        8 = audio_stream:queued(Stream),
        {error, full} = audio_stream:write(Stream, chunk(1)),
        {ok, Player} = audio_player:with_stream(Stream),
        {error, in_use} = audio_player:with_stream(Stream),
        {error, in_use} = audio_stream:destroy(Stream),
        ok = audio_player:play(Player),
        true = audio_player:playing(Player),
        ok = audio_player:destroy(Player),
        {error, invalid_resource} = audio_stream:destroy(Stream)
    end).

destroy_player_then_stream_test() ->
    with_null(fun() ->
        {ok, Stream} = audio_stream:with_queue([
            {format, f32},
            {sample_rate, 48000},
            {channels, 1},
            {capacity_frames, 16}
        ]),
        ok = audio_stream:write(Stream, chunk(4)),
        {ok, Player} = audio_player:with_stream(Stream),
        ok = audio_player:destroy(Player),
        ok = audio_stream:destroy(Stream)
    end).

underrun_flag_test() ->
    with_null(fun() ->
        {ok, Stream} = audio_stream:with_queue([
            {format, f32},
            {sample_rate, 48000},
            {channels, 1},
            {capacity_frames, 32}
        ]),
        {ok, Player} = audio_player:with_stream(Stream),
        ok = audio_player:set_loop(Player, true),
        ok = audio_player:play(Player),
        ok = wait_underrun(Stream, 50),
        true = audio_stream:underrun(Stream),
        ok = audio_player:destroy(Player),
        ok
    end).

wait_underrun(_Stream, 0) ->
    {error, timeout};
wait_underrun(Stream, N) ->
    case audio_stream:underrun(Stream) of
        true ->
            ok;
        false ->
            timer:sleep(20),
            wait_underrun(Stream, N - 1);
        Error ->
            Error
    end.
