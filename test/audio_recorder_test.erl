%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_recorder_test).
-include_lib("eunit/include/eunit.hrl").

% eunit-null

recorder_silence_test() ->
    ok = audio:initialize([{backend, null}]),
    try
        {ok, Rec} = audio_recorder:open([]),
        false = audio_recorder:running(Rec),
        empty = audio_recorder:read(Rec),
        ok = audio_recorder:start(Rec),
        true = audio_recorder:running(Rec),
        {ok, Samples} = wait_read(Rec, 50),
        f32 = audio_samples:format(Samples),
        true = audio_samples:frame_count(Samples) > 0,
        true = is_zeroed(audio_samples:data(Samples)),
        ok = audio_recorder:stop(Rec),
        false = audio_recorder:running(Rec),
        ok = audio_recorder:destroy(Rec)
    after
        ok = audio:terminate()
    end.

wait_read(_Rec, 0) ->
    {error, timeout};
wait_read(Rec, N) ->
    case audio_recorder:read(Rec) of
        {ok, Samples} ->
            {ok, Samples};
        empty ->
            timer:sleep(20),
            wait_read(Rec, N - 1);
        Other ->
            Other
    end.

is_zeroed(Data) ->
    lists:all(fun(B) -> B =:= 0 end, binary_to_list(Data)).
