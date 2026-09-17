%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_context).
-moduledoc """
Audio context.

It is the singleton worker that owns engine and device lifecycle and the
owner table. `audio:initialize/1` starts it. Mixer-plane play/stop calls do
not round-trip through this process.

```erlang
ok = audio:initialize([{backend, null}]),
true = audio_context:running(),
ok = audio:terminate().
```

The calling process of `start/1` is linked. Owner death releases that
process's native objects.
""".

-behavior(worker).

-export([
    start/0,
    start/1,
    stop/0,
    pid/0,
    running/0,
    backend/0,
    sample_rate/0,
    channels/0,
    device/0,
    set_master_volume/1,
    master_volume/0,
    inner_resources/0
]).
-export([
    register/3,
    unregister/1
]).
-export([
    initialize/1,
    handle_request/3,
    handle_message/2,
    handle_notification/2,
    handle_task/2,
    handle_timeout/2,
    terminate/2
]).

-define(WORKER_NAME, '$beam_audio_context').
-define(STOP_REQUEST, '$beam_audio_stop_context').
-define(OWNER_DOWN_MESSAGE, '$beam_audio_owner_down').

-record(state, {
    resources = #{} :: #{term() => {pid(), reference(), atom()}}
}).

-doc """
Start the audio context with defaults.

It is `start(#{backend => default, ...})` via `audio:initialize/0`.
""".
-spec start() -> {ok, pid()} | {error, audio:error()}.
start() ->
    start(#{
        backend => default,
        sample_rate => 48000,
        channels => 2,
        period_frames => 0,
        device => default
    }).

-doc """
Start the singleton audio context.

A second start is `{error, already_initialized}`. The caller is linked.
""".
-spec start(map()) -> {ok, pid()} | {error, audio:error()}.
start(Opts) when is_map(Opts) ->
    case worker:spawn(link, {name, ?WORKER_NAME}, ?MODULE, [Opts]) of
        {ok, Pid} ->
            {ok, Pid};
        already_spawned ->
            {error, already_initialized};
        {aborted, Reason} when
              Reason =:= device_failed;
              Reason =:= no_device;
              Reason =:= badarg;
              Reason =:= out_of_memory;
              Reason =:= unsupported_channels ->
            {error, Reason};
        {aborted, Reason} ->
            {error, {aborted, Reason}};
        timeout ->
            {error, timeout};
        {error, Reason} ->
            {error, Reason}
    end.

-doc """
Stop the audio context.

It requests the worker to stop and waits until that process has exited.
Remaining native objects are released in `terminate/2` before the wait
returns.
""".
-spec stop() -> ok.
stop() ->
    case whereis(?WORKER_NAME) of
        undefined ->
            ok;
        Pid ->
            Ref = monitor(process, Pid),
            _ = worker:request(?WORKER_NAME, ?STOP_REQUEST),
            receive
                {'DOWN', Ref, process, Pid, _} ->
                    ok
            after 30000 ->
                demonitor(Ref, [flush]),
                ok
            end
    end.

-doc "The context worker pid, or `undefined`.".
-spec pid() -> pid() | undefined.
pid() ->
    whereis(?WORKER_NAME).

-doc "Whether the context worker is running.".
-spec running() -> boolean().
running() ->
    whereis(?WORKER_NAME) =/= undefined.

-doc """
The opened backend name.

It is a public atom mapped from miniaudio, or `unknown`.
""".
-spec backend() ->
    null | wasapi | coreaudio | pulseaudio | alsa |
    jack | dsound | winmm | aaudio | opensl | unknown |
    {error, not_initialized}.
backend() ->
    case audio_nif:engine_info() of
        {ok, {Backend, _Rate, _Channels, _Volume}} ->
            Backend;
        {error, not_initialized} ->
            {error, not_initialized}
    end.

-doc "The engine sample rate.".
-spec sample_rate() -> audio:sample_rate() | {error, not_initialized}.
sample_rate() ->
    case audio_nif:engine_info() of
        {ok, {_Backend, Rate, _Channels, _Volume}} ->
            Rate;
        {error, not_initialized} ->
            {error, not_initialized}
    end.

-doc "The engine channel count.".
-spec channels() -> audio:channels() | {error, not_initialized}.
channels() ->
    case audio_nif:engine_info() of
        {ok, {_Backend, _Rate, Channels, _Volume}} ->
            Channels;
        {error, not_initialized} ->
            {error, not_initialized}
    end.

-doc """
The current playback device.

v1 returns `undefined` while the engine is running. Device details come from
`audio_device:list/1`.
""".
-spec device() -> audio:device_info() | undefined | {error, not_initialized}.
device() ->
    case running() of
        false ->
            {error, not_initialized};
        true ->
            undefined
    end.

-doc """
Set the master volume.

`0.0` is silent, `1.0` is unity, values greater than `1.0` are gain.
Negative, NaN, and Inf are `{error, badarg}`.
""".
-spec set_master_volume(float()) -> ok | {error, audio:error()}.
set_master_volume(Volume) when is_number(Volume) ->
    audio_nif:set_master_volume(float(Volume)).

-doc "The master volume.".
-spec master_volume() -> float() | {error, not_initialized}.
master_volume() ->
    case audio_nif:engine_info() of
        {ok, {_Backend, _Rate, _Channels, Volume}} ->
            Volume;
        {error, not_initialized} ->
            {error, not_initialized}
    end.

-doc """
Live resource ids and their owner processes.

It is for tests, like `graphics_context:inner_resources/0`.
""".
-spec inner_resources() -> #{term() => pid()}.
inner_resources() ->
    case whereis(?WORKER_NAME) of
        undefined ->
            #{};
        _Pid ->
            {reply, Resources} = worker:request(?WORKER_NAME, inner_resources),
            Resources
    end.

-doc false.
-spec register(term(), pid(), atom()) -> ok | {error, audio:error()}.
register(Resource, Owner, Kind) ->
    case whereis(?WORKER_NAME) of
        undefined ->
            {error, not_initialized};
        _Pid ->
            {reply, Reply} = worker:request(
                ?WORKER_NAME,
                {register, Resource, Owner, Kind}
            ),
            Reply
    end.

-doc false.
-spec unregister(term()) -> ok.
unregister(Resource) ->
    case whereis(?WORKER_NAME) of
        undefined ->
            ok;
        _Pid ->
            {reply, _} = worker:request(?WORKER_NAME, {unregister, Resource}),
            ok
    end.

-doc false.
initialize([Opts]) ->
    Backend = maps:get(backend, Opts),
    Rate = maps:get(sample_rate, Opts),
    Channels = maps:get(channels, Opts),
    Period = maps:get(period_frames, Opts),
    Device = maps:get(device, Opts),
    case audio_nif:engine_start(Backend, Rate, Channels, Period, Device) of
        ok ->
            {continue, #state{}};
        {error, Reason} ->
            {abort, Reason}
    end.

-doc false.
handle_request(
    {register, Resource, Owner, Kind},
    _From,
    #state{resources = Resources} = State
) ->
    Monitor = erlang:monitor(
        process,
        Owner,
        [{tag, {?OWNER_DOWN_MESSAGE, Resource}}]
    ),
    NewResources = maps:put(Resource, {Owner, Monitor, Kind}, Resources),
    {reply, ok, State#state{resources = NewResources}};

handle_request(
    {unregister, Resource},
    _From,
    #state{resources = Resources} = State
) ->
    NewState = case maps:take(Resource, Resources) of
        {{_Owner, Monitor, _Kind}, Rest} ->
            erlang:demonitor(Monitor, [flush]),
            State#state{resources = Rest};
        error ->
            State
    end,
    {reply, ok, NewState};

handle_request(inner_resources, _From, #state{resources = Resources} = State) ->
    Reply = maps:map(fun(_Resource, {Owner, _Monitor, _Kind}) ->
        Owner
    end, Resources),
    {reply, Reply, State};

handle_request(?STOP_REQUEST, _From, State) ->
    {stop, requested, ok, State}.

-doc false.
handle_message(
    {{?OWNER_DOWN_MESSAGE, Resource}, _Monitor, process, _Owner, _Reason},
    #state{resources = Resources} = State
) ->
    NewState = case maps:take(Resource, Resources) of
        {{_TrackedOwner, _TrackedMonitor, Kind}, Rest} ->
            audio_nif:owner_down(Kind, Resource),
            State#state{resources = Rest};
        error ->
            State
    end,
    {continue, NewState}.

-doc false.
handle_notification(_Notification, State) ->
    {continue, State}.

-doc false.
handle_task(_Payload, State) ->
    {continue, State}.

-doc false.
handle_timeout(_Payload, State) ->
    {continue, State}.

-doc false.
terminate(_Reason, #state{resources = Resources}) ->
    lists:foreach(fun({Resource, {_Owner, Monitor, Kind}}) ->
        erlang:demonitor(Monitor, [flush]),
        audio_nif:owner_down(Kind, Resource)
    end, order_resources(maps:to_list(Resources))),
    audio_nif:engine_stop(),
    ok.

order_resources(Pairs) ->
    Rank = fun
        (player) -> 1;
        (stream) -> 2;
        (recorder) -> 3;
        (clip) -> 4;
        (_) -> 5
    end,
    lists:sort(
        fun({_, {_, _, Left}}, {_, {_, _, Right}}) ->
            Rank(Left) =< Rank(Right)
        end,
        Pairs
    ).
