%%
%% Copyright (c) 2026, Byteplug LLC.
%%
%% This source file is part of a project made by the Erlangsters community and
%% is released under the MIT license. Please refer to the LICENSE.md file that
%% can be found at the root of the project repository.
%%
%% Written by Jonathan De Wachter <jonathan.dewachter@byteplug.io>
%%
-module(audio_device).
-moduledoc """
Playback and capture device enumeration.

It requires a running context so the list matches the selected backend.
There is no enumerate-before-open API. Device ids are encoded copies and
remain usable on a later `initialize/1` of the same backend.

```erlang
ok = audio:initialize([{backend, null}]),
{ok, [#{name := <<"NULL Playback Device">>, kind := playback}]} =
    audio_device:list(playback).
```

Switching device is a full terminate then re-initialize.
""".

-export_type([id/0, info/0]).
-export([list/1]).

-doc """
An encoded `ma_device_id` copy.

It is a binary, not a NIF resource. Garbage is `{error, badarg}` on
initialize. Vanished hardware is `{error, no_device}`.
""".
-opaque id() :: binary().

-doc "A device list entry.".
-type info() :: #{
    id := id(),
    name := binary(),
    kind := audio:device_kind(),
    default := boolean()
}.

-doc """
List playback or capture devices.

The null backend always reports `<<"NULL Playback Device">>` and
`<<"NULL Capture Device">>`, both default.
""".
-spec list(audio:device_kind()) -> {ok, [info()]} | {error, audio:error()}.
list(Kind) when Kind =:= playback; Kind =:= capture ->
    audio_nif:device_list(Kind).
