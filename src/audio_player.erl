%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_player).
-moduledoc """
Playback voice.

It is not a process. `play/1` restarts from the beginning. `stop/1` stops
and rewinds. Several players may share one clip.

```erlang
{ok, Player} = audio_player:with_clip(Clip),
ok = audio_player:play(Player),
true = audio_player:playing(Player),
ok = audio_player:destroy(Player).
```

Pan is `-1.0` left through `1.0` right. A second player on a stream is
`{error, in_use}`.
""".

-export_type([object/0]).
-export([
    with_clip/1,
    with_stream/1,
    destroy/1
]).
-export([
    play/1,
    stop/1,
    pause/1,
    resume/1,
    playing/1
]).
-export([
    set_volume/2,
    volume/1,
    set_pan/2,
    pan/1,
    set_pitch/2,
    pitch/1,
    set_loop/2,
    looping/1
]).
-export([
    seek/2,
    cursor/1
]).

-doc "An opaque player resource.".
-opaque object() :: reference().

-doc """
Create a player from a clip.

Each player has its own cursor. The clip must stay live while the player is
live; clip destroy then returns `{error, in_use}`.
""".
-spec with_clip(audio:clip()) -> {ok, object()} | {error, audio:error()}.
with_clip(Clip) ->
    wrap_new(audio_nif:player_with_clip(Clip)).

-doc """
Create a player from a stream.

A second player on the same stream is `{error, in_use}`.
""".
-spec with_stream(audio:stream()) -> {ok, object()} | {error, audio:error()}.
with_stream(Stream) ->
    wrap_new(audio_nif:player_with_stream(Stream)).

-doc "Destroy a player.".
-spec destroy(object()) -> ok | {error, invalid_resource}.
destroy(Player) ->
    case audio_nif:player_destroy(Player) of
        ok ->
            audio_context:unregister(Player),
            ok;
        Other ->
            Other
    end.

-doc """
Start or restart from the beginning.

It seeks to frame 0 then starts, even if already playing.
""".
-spec play(object()) -> ok | {error, audio:error()}.
play(Player) ->
    audio_nif:player_play(Player).

-doc """
Stop and rewind.

It stops then seeks to frame 0.
""".
-spec stop(object()) -> ok | {error, audio:error()}.
stop(Player) ->
    audio_nif:player_stop(Player).

-doc """
Pause without rewinding.

`playing/1` becomes `false`.
""".
-spec pause(object()) -> ok | {error, audio:error()}.
pause(Player) ->
    audio_nif:player_pause(Player).

-doc """
Resume without seeking.

It is `ok` if already playing.
""".
-spec resume(object()) -> ok | {error, audio:error()}.
resume(Player) ->
    audio_nif:player_resume(Player).

-doc """
Whether the voice is mixing.

It is `false` when stopped, paused, or never started.
""".
-spec playing(object()) -> boolean() | {error, audio:error()}.
playing(Player) ->
    audio_nif:player_playing(Player).

-doc "Set volume. `0.0` silent, `1.0` unity. Negative, NaN, Inf are `{error, badarg}`.".
-spec set_volume(object(), float()) -> ok | {error, audio:error()}.
set_volume(Player, Volume) when is_number(Volume) ->
    audio_nif:player_set_volume(Player, float(Volume)).

-doc "The current volume.".
-spec volume(object()) -> float() | {error, audio:error()}.
volume(Player) ->
    audio_nif:player_volume(Player).

-doc "Set pan. `-1.0` left, `0.0` center, `1.0` right.".
-spec set_pan(object(), float()) -> ok | {error, audio:error()}.
set_pan(Player, Pan) when is_number(Pan) ->
    audio_nif:player_set_pan(Player, float(Pan)).

-doc "The current pan.".
-spec pan(object()) -> float() | {error, audio:error()}.
pan(Player) ->
    audio_nif:player_pan(Player).

-doc "Set pitch. Must be finite and greater than `0.0`.".
-spec set_pitch(object(), float()) -> ok | {error, audio:error()}.
set_pitch(Player, Pitch) when is_number(Pitch) ->
    audio_nif:player_set_pitch(Player, float(Pitch)).

-doc "The current pitch.".
-spec pitch(object()) -> float() | {error, audio:error()}.
pitch(Player) ->
    audio_nif:player_pitch(Player).

-doc "Enable or disable looping.".
-spec set_loop(object(), boolean()) -> ok | {error, audio:error()}.
set_loop(Player, Loop) when is_boolean(Loop) ->
    audio_nif:player_set_loop(Player, Loop).

-doc "Whether the player is looping.".
-spec looping(object()) -> boolean() | {error, audio:error()}.
looping(Player) ->
    audio_nif:player_looping(Player).

-doc """
Seek to a time in seconds.

Negative or non-finite values are `{error, badarg}`. A time past the end
clamps to the end.
""".
-spec seek(object(), float()) -> ok | {error, audio:error()}.
seek(Player, Seconds) when is_number(Seconds) ->
    audio_nif:player_seek(Player, float(Seconds)).

-doc "Playback cursor in seconds from the start.".
-spec cursor(object()) -> float() | {error, audio:error()}.
cursor(Player) ->
    audio_nif:player_cursor(Player).

wrap_new({ok, Player}) ->
    try audio_context:register(Player, self(), player) of
        ok ->
            {ok, Player};
        {error, Reason} ->
            audio_nif:player_destroy(Player),
            {error, Reason}
    catch
        Class:Reason:Stack ->
            audio_nif:player_destroy(Player),
            erlang:raise(Class, Reason, Stack)
    end;
wrap_new(Error) ->
    Error.
