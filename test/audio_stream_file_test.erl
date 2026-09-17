%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_stream_file_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

with_null(Fun) ->
    ok = audio:initialize([{backend, null}]),
    try
        Fun()
    after
        ok = audio:terminate()
    end.

tmp_dir() ->
    case os:getenv("TEMP") of
        false ->
            os:getenv("TMPDIR", "/tmp");
        Dir ->
            Dir
    end.

tmp_wav() ->
    Samples = audio_samples:silence(s16, 8000, #{channels => 1, frames => 800}),
    Path = filename:join(tmp_dir(), "beam-audio-stream.wav"),
    ok = audio_wav:save(Samples, Path),
    Path.

file_stream_in_use_test() ->
    with_null(fun() ->
        Path = tmp_wav(),
        try
            NotWav = filename:join(filename:dirname(Path), "beam-audio-not.wav"),
            ok = file:write_file(NotWav, <<"ID3not-a-wav">>),
            {error, unsupported_format} = audio_stream:from_file(NotWav),
            _ = file:delete(NotWav),
            {ok, Stream} = audio_stream:from_file(Path),
            Duration = audio_stream:duration(Stream),
            true = is_float(Duration),
            true = abs(Duration - 0.1) < 0.001,
            {ok, Player} = audio_player:with_stream(Stream),
            {error, in_use} = audio_player:with_stream(Stream),
            ok = audio_player:play(Player),
            true = audio_player:playing(Player),
            {error, in_use} = audio_stream:destroy(Stream),
            ok = audio_player:destroy(Player),
            {error, invalid_resource} = audio_stream:destroy(Stream)
        after
            _ = file:delete(Path)
        end
    end).

file_destroy_player_first_test() ->
    with_null(fun() ->
        Path = tmp_wav(),
        try
            {ok, Stream} = audio_stream:from_file(Path),
            {ok, Player} = audio_player:with_stream(Stream),
            ok = audio_player:destroy(Player),
            ok = audio_stream:destroy(Stream)
        after
            _ = file:delete(Path)
        end
    end).
