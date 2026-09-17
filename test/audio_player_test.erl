%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_player_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

with_null(Fun) ->
    ok = audio:initialize([{backend, null}]),
    try
        Fun()
    after
        ok = audio:terminate()
    end.

tone() ->
    Data = <<
        <<(math:sin(2.0 * math:pi() * 440.0 * I / 48000.0)):32/float-little>>
        || I <- lists:seq(0, 4799)
    >>,
    audio_samples:from_binary(f32, 48000, 1, Data).

play_stop_volume_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        {ok, Player} = audio_player:with_clip(Clip),
        false = audio_player:playing(Player),
        ok = audio_player:play(Player),
        true = audio_player:playing(Player),
        1.0 = audio_player:volume(Player),
        ok = audio_player:set_volume(Player, 0.25),
        true = abs(audio_player:volume(Player) - 0.25) < 0.0001,
        {error, badarg} = audio_player:set_volume(Player, -0.1),
        ok = audio_player:stop(Player),
        false = audio_player:playing(Player),
        ok = audio_player:play(Player),
        true = audio_player:playing(Player),
        ok = audio_player:destroy(Player),
        ok = audio_clip:destroy(Clip)
    end).

two_players_independent_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        {ok, P1} = audio_player:with_clip(Clip),
        {ok, P2} = audio_player:with_clip(Clip),
        ok = audio_player:play(P1),
        ok = audio_player:play(P2),
        true = audio_player:playing(P1),
        true = audio_player:playing(P2),
        ok = audio_player:destroy(P1),
        ok = audio_player:destroy(P2),
        ok = audio_clip:destroy(Clip)
    end).

in_use_and_restart_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        {ok, Player} = audio_player:with_clip(Clip),
        ok = audio_player:play(Player),
        {error, in_use} = audio_clip:destroy(Clip),
        false = maps:is_key(Clip, audio_context:inner_resources()),
        true = audio_player:playing(Player),
        ok = audio_player:play(Player),
        true = audio_player:playing(Player),
        Cursor = audio_player:cursor(Player),
        true = is_float(Cursor) orelse is_integer(Cursor),
        ok = audio_player:destroy(Player),
        false = maps:is_key(Player, audio_context:inner_resources()),
        {error, invalid_resource} = audio_clip:destroy(Clip)
    end).

destroy_player_then_clip_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        {ok, Player} = audio_player:with_clip(Clip),
        ok = audio_player:destroy(Player),
        ok = audio_clip:destroy(Clip)
    end).

owner_death_live_player_test() ->
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
        {ok, Player} = audio_player:with_clip(Clip),
        ok = audio_player:play(Player),
        Ref = monitor(process, Pid),
        exit(Pid, kill),
        receive {'DOWN', Ref, process, Pid, _} -> ok end,
        timer:sleep(50),
        true = audio_player:playing(Player),
        {error, in_use} = audio_clip:destroy(Clip),
        ok = audio_player:destroy(Player),
        {error, invalid_resource} = audio_clip:destroy(Clip)
    end).

owner_death_clip_and_player_test() ->
    with_null(fun() ->
        Parent = self(),
        Pid = spawn(fun() ->
            {ok, Clip} = audio_clip:with_samples(tone()),
            {ok, Player} = audio_player:with_clip(Clip),
            ok = audio_player:play(Player),
            Parent ! {ready, Clip, Player},
            receive
                after infinity -> ok
            end
        end),
        {Clip, Player} = receive {ready, C, P} -> {C, P} end,
        Ref = monitor(process, Pid),
        exit(Pid, kill),
        receive {'DOWN', Ref, process, Pid, _} -> ok end,
        ok = wait_empty(50),
        {ok, Clip2} = audio_clip:with_samples(tone()),
        {ok, Player2} = audio_player:with_clip(Clip2),
        ok = audio_player:destroy(Player2),
        ok = audio_clip:destroy(Clip2),
        {error, invalid_resource} = audio_player:destroy(Player),
        {error, invalid_resource} = audio_clip:destroy(Clip)
    end).

controls_test() ->
    with_null(fun() ->
        {ok, Clip} = audio_clip:with_samples(tone()),
        {ok, Player} = audio_player:with_clip(Clip),
        ok = audio_player:play(Player),
        ok = audio_player:pause(Player),
        false = audio_player:playing(Player),
        ok = audio_player:resume(Player),
        true = audio_player:playing(Player),
        ok = audio_player:set_pan(Player, -0.5),
        true = abs(audio_player:pan(Player) + 0.5) < 0.0001,
        {error, badarg} = audio_player:set_pan(Player, 1.5),
        ok = audio_player:set_pitch(Player, 1.5),
        true = abs(audio_player:pitch(Player) - 1.5) < 0.0001,
        {error, badarg} = audio_player:set_pitch(Player, 0.0),
        ok = audio_player:set_loop(Player, true),
        true = audio_player:looping(Player),
        ok = audio_player:seek(Player, 0.01),
        {error, badarg} = audio_player:seek(Player, -1.0),
        ok = audio_player:destroy(Player),
        ok = audio_clip:destroy(Clip)
    end).

wait_empty(0) ->
    case maps:size(audio_context:inner_resources()) of
        0 -> ok;
        _ -> {error, not_empty}
    end;
wait_empty(N) ->
    case maps:size(audio_context:inner_resources()) of
        0 ->
            ok;
        _ ->
            timer:sleep(20),
            wait_empty(N - 1)
    end.
