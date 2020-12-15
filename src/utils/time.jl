using Dates, TimeZones, Unitful
import SampledSignals: samplerate

export Timestamp, TimestampRange, gettime_ns

"""
    Timestamp{S}

This represents a timestamp that is rounded to the nearest sample since the unix epoch,
granularized by the given samplerate.
"""
struct Timestamp{S}
    samples::UInt64
    Timestamp(num_samples::UInt64, samplerate::Real) = new{Float64(samplerate)}(num_samples)
end

# Convenience constructor to convert the current time in nanoseconds to samples
# since the unix epoch.
function Timestamp(num_ns::UInt64 = gettime_ns(); samplerate::Real = 48000.0)
    return Timestamp(round(UInt64, num_ns*(samplerate/1.0e9)), samplerate)
end

# Copy constructor
function Timestamp(ts::Timestamp{S}) where {S}
    return Timestamp(ts.samples, S)
end

# Convert Unitful time definitions to nanoseconds, samples, etc...
innanosecs(x::Unitful.Time) = ustrip(uconvert(ns, x))
insamples(x::Unitful.Time, samplerate::Real) = round(UInt64, innanosecs(x)*samplerate/1e9)
samplerate(x::Timestamp{S}) where {S} = S

# Basic temporal arithmetic.  First, timestamps with other timestamps of the same samplerate
Base.:(+)(x::Timestamp{S}, y::Timestamp{S}) where {S} = Timestamp(x.samples + y.samples, S)
Base.:(-)(x::Timestamp{S}, y::Timestamp{S}) where {S} = Timestamp(x.samples - y.samples, S)

# Next, operations on timestamps with # of samples:
Base.:(+)(x::Timestamp{S}, y::Real) where {S} = Timestamp(x.samples + y, S)
Base.:(-)(x::Timestamp{S}, y::Real) where {S} = Timestamp(x.samples - y, S)
Base.:(+)(x::Real, y::Timestamp{S}) where {S} = Timestamp(x + y.samples, S)
Base.:(-)(x::Real, y::Timestamp{S}) where {S} = Timestamp(x - y.samples, S)

# Next, operations on timestamps with Unitful time values:
Base.:(+)(x::Timestamp{S}, y::Unitful.Time) where {S} = Timestamp(x.samples + insamples(y, S), S)
Base.:(-)(x::Timestamp{S}, y::Unitful.Time) where {S} = Timestamp(x.samples - insamples(y, S), S)

# You might say that multiplication/division is a weird thing to do to a timestamp.
# I would agree, except that it's quite useful to be able to be able to do things
# like find the midpoint between two timestamps, e.g. `(ts1 + ts2)/2`.
Base.:(/)(x::Timestamp{S}, y::Real) where {S} = Timestamp(round(UInt64, x.samples/y), S)
Base.:(*)(x::Timestamp{S}, y::Real) where {S} = Timestamp((x.samples*y)%UInt64, S)

# We can order timestamps, even ones that are recorded in different time bases!
Base.isless(x::Timestamp{S}, y::Timestamp{S}) where {S} = Base.isless(x.samples, y.samples)
Base.isless(x::Timestamp{S1}, y::Timestamp{S2}) where {S1, S2} = Base.isless(x.samples/S1, y.samples/S2)

# Helper for display
function TimeZones.ZonedDateTime(x::Timestamp{S}) where {S}
    rata = Dates.UNIXEPOCH + round(Int64, x.samples*1e3/S)
    return ZonedDateTime(DateTime(Dates.UTM(rata)), localzone(); from_utc=true)
end
Base.show(io::IO, x::Timestamp) = print(io,ZonedDateTime(x))


"""
    TimestampRange{S}

Given two timestamps that are using the same samplerate, we can define a range of times.
This allows us to do things like index into `TimestampedSampleBuf` objects with timestamps.
"""
struct TimestampRange{S} <: AbstractUnitRange{UInt64}
    start::UInt64
    stop::UInt64
end
(::Colon)(x::Timestamp{S}, y::Timestamp{S}) where {S} = TimestampRange{S}(x.samples, y.samples)



if Base.Sys.WORD_SIZE==32
    time_t = Clong
elseif Base.Sys.WORD_SIZE==64
    time_t = Clonglong
end

mutable struct timespec
    sec::time_t
    nsec::Clong
end

function gettime_ns()
    @static if Sys.islinux()
        # Call clock_gettime() with CLOCK_REALTIME
        tx = timespec(0,0)
        CLOCK_REALTIME = 0
        s = ccall((:clock_gettime,:librt), Int32, (Int32, Ptr{timespec}), CLOCK_REALTIME, Ref(tx))
        if s < 0
            error("Unable to call clock_gettime(): $(s)")
        end
        return UInt64(tx.sec*1e9 + tx.nsec)
    end

    @static if Sys.isapple()
        # Call clock_gettime_nsec_np() with CLOCK_REALTIME
        CLOCK_REALTIME = 0
        return ccall(:clock_gettime_nsec_np, UInt64, (Cint,), CLOCK_REALTIME)
    end

    # Fallback implementation
    return UInt64(time()*1e9)
end
