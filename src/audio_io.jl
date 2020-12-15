using PortAudio, Unitful
import Base: read, write, isopen, close
import SampledSignals: samplerate, nchannels
export AudioInput, AudioOutput
using popuset_shim_jll, Libdl

mutable struct AudioInput{S}
    stream::PortAudioStream{Float32}
    next_timestamp::Timestamp
    slush_buff::Array{Float32}

    function AudioInput(name; channels::Int = 2, samplerate::Float64 = 48000.0, latency::Float64 = 0.01)
        ai = new{samplerate}(
            PortAudioStream(name, channels, 0; eltype=Float32, samplerate=samplerate, latency=latency),
            Timestamp(gettime_ns(); samplerate),
            zeros(Float32, channels, round(Int64, samplerate/10)),
        )
        # finalizer(ai) do
        #     close(ai.stream)
        # end
        return ai
    end
end

isopen(ai::AudioInput) = isopen(ai.stream)
close(ai::AudioInput) = close(ai.stream)
samplerate(ai::AudioInput{S}) where {S} = S
nchannels(ai::AudioInput) = nchannels(ai.stream.source)

function synchronize!(ai::AudioInput{S}, time_ns::UInt64 = gettime_ns()) where {S}
    ai.next_timestamp = Timestamp(time_ns; samplerate=S)
end

function read(ai::AudioInput{S}, num_frames::Int; warn_xruns::Bool = false) where {S}
    if num_frames > size(ai.slush_buff, 2)
        error("I'm just straight up not going to handle this right now")
    end

    # Auto-detect overflows, and reset our stream timestamp if that is the case.
    err = PortAudio.Pa_ReadStream(ai.stream.stream, ai.slush_buff, num_frames, warn_xruns)
    if err == PortAudio.PA_INPUT_OVERFLOWED
        @warn("Discontinuity detected: force-synchronizing AudioInput")
        synchronize!(ai)
    end

    # De-interleave samples, tag it with the currently-used timestamp
    buff = TimestampedSampleBuf(collect(transpose(ai.slush_buff[:, 1:num_frames])), ai.next_timestamp)

    # Save the next timestamp to be used
    ai.next_timestamp += num_frames

    return buff
end
function read(ai::AudioInput{S}, t::Unitful.Time, args...; kwargs...) where {S}
    num_frames = round(Int64, ustrip(uconvert(Unitful.s,t))*S)
    return read(ai, num_frames, args...; kwargs...)
end



mutable struct CallbackStreamInfo
    samplerate::Float64
    channels::UInt8
    notify_callback::Ptr{Cvoid}
end

mutable struct AudioOutput{S}
    stream::PortAudioStream{Float32}
    stream_info::CallbackStreamInfo
    sync_offset::Int64
    buffer_request_cond::Base.AsyncCondition
    buffer_request::Channel{Bool}
    buffer_request_task::Task

    function AudioOutput(name; channels::Int = 2, samplerate::Float64 = 48000.0, latency::Float64 = 0.01)
        callback = dlsym(popuset_shim_jll.popuset_shim_handle, :pa_shim_processcb)
        buffer_request_cond = Base.AsyncCondition()
        stream_info = CallbackStreamInfo(samplerate, channels, @cfunction(() -> ccall(:uv_async_send, buffer_request_cond.handle), Cint, ()))
        stream = PortAudioStream(name, 0, channels; eltype=Float32, samplerate=samplerate, latency=latency, callback=callback, userdata=stream_info)
        buffer_request = Channel{Bool}(2)
        t_event_translator = Threads.@spawn begin
            while isopen(stream)
                wait(buffer_request_cond)
                @info(" -> notify")
            end
        end
        ao = new{samplerate}(stream, stream_info, 0, buffer_request_cond, buffer_request, t_event_translator)
        synchronize!(ao)

        # finalizer(ao) do
        #     close(ao.stream)
        # end
        return ao
    end
end

function synchronize!(ao::AudioOutput, time::UInt64=gettime_ns())
    # Get instantaneous time from current audio output, and convert to ns
    stream_time = PortAudio.Pa_GetStreamTime(ao.stream.stream)
    ao.sync_offset = 0
    ao.sync_offset = time - device_to_host(ao, stream_time);
    return nothing
end

function device_to_host(ao::AudioOutput, device_time::Float64)
    # Convert device_time to ns, then add the sync offset
    return round(UInt64, device_time*1e9) + ao.sync_offset
end

function get_next_mix_timestamp(ao::AudioOutput{S}) where {S}
    device_time = ccall((:pa_shim_get_mix_horizon, popuset_shim), Float64, (Ref{CallbackStreamInfo},), ao.stream_info)
    return Timestamp(device_to_host(ao, device_time); samplerate=S)
end

isopen(ao::AudioOutput) = isopen(ao.stream)
close(ao::AudioOutput) = close(ao.stream)
samplerate(ao::AudioOutput{S}) where {S} = S
nchannels(ao::AudioOutput) = nchannels(ao.stream.sink)
function write(ao::AudioOutput, data::Array{Float32,2})
    ccall((:pa_shim_queue, popuset_shim), Cvoid, (Ptr{Cfloat}, UInt32, UInt8, Cdouble), collect(transpose(data)), UInt32(size(data,1)), UInt8(size(data,2)), samplerate(ao))
end
write(ao::AudioOutput, buff::TimestampedSampleBuf) = write(ao, buff.data)


# shim utils
set_verbose(verbose_level::Int) = ccall((:pa_shim_set_verbosity, popuset_shim), Cvoid, (Cint,), Cint(verbose_level))
set_mute(mute::Bool) = ccall((:pa_shim_set_mute, popuset_shim), Cvoid, (Cint,), Cint(mute))
set_print_stats(v::Bool) = ccall((:pa_shim_set_print_stats, popuset_shim), Cvoid, (Cint,), Cint(v))

struct pashim_stats
    buffer_underruns::UInt64
    total_consumed::UInt64
    in_underrun::UInt8
    buffers_queued::UInt32
end

function get_stats()
    return unsafe_load(ccall((:pa_shim_get_stats, popuset_shim), Ptr{pashim_stats}, ()))
end
