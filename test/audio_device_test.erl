%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_device_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

null_device_names_test() ->
    ok = audio:initialize([{backend, null}]),
    try
        {ok, [
            #{name := <<"NULL Playback Device">>, kind := playback, default := true, id := PlayId}
        ]} = audio_device:list(playback),
        {ok, [
            #{name := <<"NULL Capture Device">>, kind := capture, default := true, id := CapId}
        ]} = audio_device:list(capture),
        true = is_binary(PlayId),
        true = is_binary(CapId),
        ok = audio:terminate(),
        ok = audio:initialize([{backend, null}, {device, PlayId}]),
        null = audio_context:backend()
    after
        ok = audio:terminate()
    end.
