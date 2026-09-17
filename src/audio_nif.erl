%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_nif).
-moduledoc false.

-on_load(init/0).

-export([
    engine_start/5,
    engine_stop/0,
    engine_info/0,
    set_master_volume/1,
    device_list/1
]).
-export([
    clip_with_samples/5,
    clip_destroy/1,
    clip_info/1,
    clip_force_uninit/1
]).
-export([
    player_with_clip/1,
    player_with_stream/1,
    player_destroy/1,
    player_play/1,
    player_stop/1,
    player_pause/1,
    player_resume/1,
    player_playing/1,
    player_set_volume/2,
    player_volume/1,
    player_set_pan/2,
    player_pan/1,
    player_set_pitch/2,
    player_pitch/1,
    player_set_loop/2,
    player_looping/1,
    player_seek/2,
    player_cursor/1
]).
-export([
    wav_decode/1,
    wav_encode/5
]).
-export([
    stream_from_file/1,
    stream_with_queue/4,
    stream_destroy/1,
    stream_force_uninit/1,
    stream_write/6,
    stream_available/1,
    stream_queued/1,
    stream_underrun/1,
    stream_duration/1
]).
-export([
    recorder_open/5,
    recorder_destroy/1,
    recorder_start/1,
    recorder_stop/1,
    recorder_running/1,
    recorder_read/2,
    recorder_overrun/1
]).
-export([
    owner_down/2,
    force_teardown/2
]).

init() ->
    LibName = "beam-audio",
    LibPath = case code:priv_dir(beam_audio) of
        {error, bad_name} ->
            case filelib:is_dir(filename:join(["..", priv])) of
                true ->
                    filename:join(["..", priv, LibName]);
                _ ->
                    filename:join([priv, LibName])
            end;
        PrivDir ->
            filename:join(PrivDir, LibName)
    end,
    erlang:load_nif(LibPath, undefined).

engine_start(_Backend, _Rate, _Channels, _Period, _Device) ->
    erlang:nif_error(beam_audio_not_loaded).
engine_stop() ->
    erlang:nif_error(beam_audio_not_loaded).
engine_info() ->
    erlang:nif_error(beam_audio_not_loaded).
set_master_volume(_Volume) ->
    erlang:nif_error(beam_audio_not_loaded).
device_list(_Kind) ->
    erlang:nif_error(beam_audio_not_loaded).
clip_with_samples(_Format, _Rate, _Channels, _Frames, _Data) ->
    erlang:nif_error(beam_audio_not_loaded).
clip_destroy(_Clip) ->
    erlang:nif_error(beam_audio_not_loaded).
clip_info(_Clip) ->
    erlang:nif_error(beam_audio_not_loaded).
clip_force_uninit(_Clip) ->
    erlang:nif_error(beam_audio_not_loaded).
player_with_clip(_Clip) ->
    erlang:nif_error(beam_audio_not_loaded).
player_with_stream(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
player_destroy(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_play(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_stop(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_pause(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_resume(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_playing(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_set_volume(_Player, _Volume) ->
    erlang:nif_error(beam_audio_not_loaded).
player_volume(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_set_pan(_Player, _Pan) ->
    erlang:nif_error(beam_audio_not_loaded).
player_pan(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_set_pitch(_Player, _Pitch) ->
    erlang:nif_error(beam_audio_not_loaded).
player_pitch(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_set_loop(_Player, _Loop) ->
    erlang:nif_error(beam_audio_not_loaded).
player_looping(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
player_seek(_Player, _Seconds) ->
    erlang:nif_error(beam_audio_not_loaded).
player_cursor(_Player) ->
    erlang:nif_error(beam_audio_not_loaded).
wav_decode(_Binary) ->
    erlang:nif_error(beam_audio_not_loaded).
wav_encode(_Format, _Rate, _Channels, _Frames, _Data) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_from_file(_Path) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_with_queue(_Format, _Rate, _Channels, _Capacity) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_destroy(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_force_uninit(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_write(_Stream, _Format, _Rate, _Channels, _Frames, _Data) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_available(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_queued(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_underrun(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
stream_duration(_Stream) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_open(_Format, _Rate, _Channels, _Buffer, _Device) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_destroy(_Recorder) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_start(_Recorder) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_stop(_Recorder) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_running(_Recorder) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_read(_Recorder, _MaxFrames) ->
    erlang:nif_error(beam_audio_not_loaded).
recorder_overrun(_Recorder) ->
    erlang:nif_error(beam_audio_not_loaded).
owner_down(_Kind, _Resource) ->
    erlang:nif_error(beam_audio_not_loaded).
force_teardown(_Kind, _Resource) ->
    erlang:nif_error(beam_audio_not_loaded).
