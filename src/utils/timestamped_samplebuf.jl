using SampledSignals
import SampledSignals: samplerate
export TimestampedSampleBuf, start, stop, nframes, samplerate


function default_timestamp(num_samples::Int, samplerate::Float64)
    # We automatically adjust the timestamp of this samplebuf backwards
    # by the length of the buffer itself:
    return Timestamp(;samplerate=samplerate) - num_samples
end


struct TimestampedSampleBuf{S} <: AbstractSampleBuf{Float32, 2}
    data::Array{Float32, 2}

    # The time at which this sample buffer should be presented, in nanoseconds
    timestamp::Timestamp{S}

    function TimestampedSampleBuf(data::Array{Float32, 2}, samplerate::Real)
        ts = default_timestamp(size(data,1), Float64(samplerate))
        return new{samplerate}(data, ts)
    end
    function TimestampedSampleBuf(data::Array{Float32, 2}, ts::Timestamp{S}) where {S}
        return new{S}(data, ts)
    end
end

# Convenience constructors for constructing zero-buffers
function TimestampedSampleBuf(frames::Int, channels::Int, samplerate::Real)
    return TimestampedSampleBuf(zeros(Float32, frames, channels), samplerate)
end
function TimestampedSampleBuf(frames::Int, channels::Int, ts::Timestamp)
    return TimestampedSampleBuf(zeros(Float32, frames, channels), ts)
end
# Convenience copy constructor from a non-timestamped SampleBuf
function TimestampedSampleBuf(buff::SampleBuf; kwargs...)
    return TimestampedSampleBuf(buff.data, buff.samplerate; kwargs...)
end

samplerate(tsb::TimestampedSampleBuf{S}) where S = S
start(tsb::TimestampedSampleBuf) = tsb.timestamp
stop(tsb::TimestampedSampleBuf) = start(tsb) + nframes(tsb) - 1
lastindex(tsb::TimestampedSampleBuf) = stop(tsb)
contains(tsb::TimestampedSampleBuf, ts::Timestamp) = start(tsb) <= ts <= stop(tsb)

## Indexing helpers
# Technically, I could do conversion to support different samplerates, but we'd have to wrestle
# with interpolation and that's roughly 1000x too much work for me right now.
function index(tsb::TimestampedSampleBuf{S}, ts::Timestamp{S}) where {S}
    return ts.samples - tsb.timestamp.samples + 1
end
function index(tsb::TimestampedSampleBuf{S}, r::TimestampRange{S}) where {S}
    return index(tsb, Timestamp(r.start, S)):index(tsb, Timestamp(r.stop, S))
end
function index(tsb::TimestampedSampleBuf, c::Colon)
    return 1:nframes(tsb)
end

const TimestampIndex = Union{Timestamp,TimestampRange,Colon}
const ChannelIndex = Union{Int,AbstractRange{Int},Colon}

Base.getindex(tsb::TimestampedSampleBuf, I::TimestampIndex, C::ChannelIndex = Colon()) = getindex(tsb.data, index(tsb, I), C)
Base.setindex!(tsb::TimestampedSampleBuf, val, I::TimestampIndex, C::ChannelIndex = Colon()) = setindex!(tsb.data, val, index(tsb, I), C)
Base.view(tsb::TimestampedSampleBuf, I::TimestampIndex, C::ChannelIndex = Colon()) = view(tsb.data, index(tsb, I), C)


# Display helpers
SampledSignals.typename(::TimestampedSampleBuf) = "TimestampedSampleBuf"
SampledSignals.unitname(::TimestampedSampleBuf) = "s"
SampledSignals.srname(::TimestampedSampleBuf) = "Hz"

# Slightly-adapted version of SampledSignals.show()
function Base.show(io::IO, ::MIME"text/plain", buf::TimestampedSampleBuf)
    println(io, "$(nframes(buf))-frame, $(nchannels(buf))-channel $(SampledSignals.typename(buf)) ($(buf.timestamp))")
    len = nframes(buf) / samplerate(buf)
    ustring = SampledSignals.unitname(buf)
    srstring = SampledSignals.srname(buf)
    print(io, "$(len)$(ustring) sampled at $(samplerate(buf))$(srstring)")
    nframes(buf) > 0 && SampledSignals.showchannels(io, buf)
end
