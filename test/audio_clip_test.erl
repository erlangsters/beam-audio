%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_clip_test).
-include_lib("eunit/include/eunit.hrl").
-include("audio.hrl").

% eunit-null

with_null(Fun) ->
    ok = audio:initialize([{backend, null}]),
    try
        Fun()
    after
        ok = audio:terminate()
    end.

tone() ->
    audio_samples:silence(f32, 48000, #{channels => 1, frames => 480}).

empty_and_too_large_test() ->
    with_null(fun() ->
        Empty = {f32, 48000, 1, 0, <<>>},
        {error, badarg} = audio_clip:with_samples(Empty),
        TooLarge = {f32, 48000, 1, (?AUDIO_MAX_PCM_BYTES div 4) + 1, <<0, 0, 0, 0>>},
        {error, too_large} = audio_clip:with_samples(TooLarge)
    end).

with_samples_and_destroy_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        f32 = audio_clip:format(Clip),
        48000 = audio_clip:sample_rate(Clip),
        1 = audio_clip:channels(Clip),
        480 = audio_clip:frame_count(Clip),
        true = maps:is_key(Clip, audio_context:inner_resources()),
        ok = audio_clip:destroy(Clip),
        false = maps:is_key(Clip, audio_context:inner_resources()),
        {error, invalid_resource} = audio_clip:destroy(Clip)
    end).

owner_death_zero_players_test() ->
    with_null(fun() ->
        Parent = self(),
        Pid = spawn(fun() ->
            {ok, Clip} = audio_clip:with_samples(tone()),
            Parent ! {clip, Clip},
            receive
                after infinity -> ok
            end
        end),
        Clip = receive {clip, C} -> C end,
        Ref = monitor(process, Pid),
        exit(Pid, kill),
        receive {'DOWN', Ref, process, Pid, _} -> ok end,
        ok = wait_gone(Clip, 50),
        {error, invalid_resource} = audio_player:with_clip(Clip)
    end).

wait_gone(_Clip, 0) ->
    timeout;
wait_gone(Clip, N) ->
    case maps:is_key(Clip, audio_context:inner_resources()) of
        false ->
            ok;
        true ->
            timer:sleep(20),
            wait_gone(Clip, N - 1)
    end.
