%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_context_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

with_null(Fun) ->
    ok = audio:initialize([{backend, null}]),
    try
        Fun()
    after
        ok = audio:terminate()
    end.

initialize_null_test() ->
    ok = audio:initialize([{backend, null}]),
    true = audio_context:running(),
    null = audio_context:backend(),
    {error, already_initialized} = audio:initialize([{backend, null}]),
    ok = audio:terminate(),
    false = audio_context:running(),
    ok = audio:initialize([{backend, null}]),
    ok = audio:terminate(),
    ok.

unknown_option_test() ->
    {error, badarg} = audio:initialize([{backend, null}, {nope, 1}]),
    ok.

kill_then_reinit_test() ->
    Parent = self(),
    Pid = spawn(fun() ->
        ok = audio:initialize([{backend, null}]),
        Parent ! ready,
        receive
            after infinity -> ok
        end
    end),
    receive
        ready -> ok
    end,
    Ref = monitor(process, Pid),
    exit(Pid, kill),
    receive
        {'DOWN', Ref, process, Pid, _} -> ok
    end,
    ok = wait_stopped(50),
    ok = audio:initialize([{backend, null}]),
    true = audio_context:running(),
    ok = audio:terminate(),
    ok.

master_volume_test() ->
    with_null(fun() ->
        1.0 = audio_context:master_volume(),
        ok = audio_context:set_master_volume(0.5),
        true = abs(audio_context:master_volume() - 0.5) < 0.0001,
        {error, badarg} = audio_context:set_master_volume(-1.0)
    end).

wait_stopped(0) ->
    case audio_context:pid() of
        undefined -> ok;
        _ -> {error, still_running}
    end;
wait_stopped(N) ->
    case audio_context:pid() of
        undefined ->
            ok;
        _ ->
            timer:sleep(20),
            wait_stopped(N - 1)
    end.
