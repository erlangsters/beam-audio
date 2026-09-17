cmake_minimum_required(VERSION 3.11)
project(beam_audio LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Vendored from https://github.com/mackron/miniaudio
# tag 0.11.25 (3 Mar 2026)
# commit 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
# Do not use Debian libminiaudio-dev. Do not copy raylib/src/external/miniaudio.h.

# We let Rebar3 inform CMake where to find the Erlang/OTP location. Therefore,
# it requires the ERLANG_ERTS_DIR and ERL_INTERFACE_DIR values specified with
# the -D flags.
if(NOT DEFINED ERLANG_ERTS_DIR)
    message(FATAL_ERROR "ERLANG_ERTS_DIR must be defined (pass -DERLANG_ERTS_DIR=...)")
endif()
if(NOT DEFINED ERL_INTERFACE_DIR)
    message(FATAL_ERROR "ERL_INTERFACE_DIR must be defined (pass -DERL_INTERFACE_DIR=...)")
endif()

set(ERTS_INCLUDE_DIR "${ERLANG_ERTS_DIR}/include")
set(ERL_INTERFACE_INCLUDE_DIR "${ERL_INTERFACE_DIR}/include")

add_library(beam-audio SHARED
    c_src/miniaudio_impl.c
    c_src/audio_nif.c
)
set_target_properties(beam-audio PROPERTIES
    OUTPUT_NAME "beam-audio"
    PREFIX ""
)

# The Rebar3 build system (as configured in rebar.config) does not make use
# of the "install" step. Instead, it produces the NIF library in the priv/
# folder directly during the "build" step.
set_target_properties(beam-audio PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/priv"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/priv"
)

# On macOS, we need to add special linker flags to avoid issues with symbol
# resolution. miniaudio is built with MA_NO_RUNTIME_LINKING (see miniaudio_impl.c).
if(APPLE)
    set_target_properties(beam-audio PROPERTIES
        LINK_FLAGS "-flat_namespace -undefined suppress"
        SUFFIX ".so"
    )
endif()

target_include_directories(beam-audio PRIVATE
    ${ERTS_INCLUDE_DIR}
    ${ERL_INTERFACE_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/c_src
    ${CMAKE_CURRENT_SOURCE_DIR}/c_src/miniaudio
)

# This NIF uses erl_nif.h only. Do not link ei.

if(APPLE)
    target_link_libraries(beam-audio PRIVATE
        m
        pthread
        dl
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
    )
elseif(UNIX)
    target_link_libraries(beam-audio PRIVATE m pthread dl)
endif()
# Windows: miniaudio defaults to runtime linking with no extra libraries.
# ole32 is optional belt-and-suspenders and is not linked here.
