@info("Loading libraries")
using Sockets, Opus, FileIO, LibSndFile, Statistics, Printf

function build_packet!(encoders::Vector{OpusEncoder},
                       audio_data::Vector,
                       packet_time::Float64)
    # First, encode all audio data using our Opus encoders
    enc_packets = [Opus.encode_frame(enc, data) for (enc, data) in zip(encoders, audio_data)]
    
    # Build simple seriliazed packet representation:
    #
    # Packet layout:
    #  - timestamp (uint64 in us)
    #  - num_channels (uint8)
    #  - packet_lengths (num_channels * uint16)
    #  - packets (num_channels .* packet_lengths * uint8)
    io = IOBuffer()
    num_channels = length(encoders)
    write(io, UInt64(packet_time*1e6))
    write(io, UInt8(length(encoders)))
    for idx in 1:num_channels
        write(io, UInt16(length(enc_packets[idx])))
    end
    for idx in 1:num_channels
        write(io, enc_packets[idx])
    end

    # Return serialized packet.
    return take!(io)
end

struct SinGen
    freq::Float64
    bufflen::Int
end

function Base.iterate(p::SinGen, ϕ = 0)
    fs = 48000
    chunk = Float32[sin(2π * p.freq*t/fs + ϕ) for t in 1:p.bufflen]
    ϕ = (ϕ + 2π*p.freq*p.bufflen/fs)%(2π)
    return chunk, ϕ
end

struct FileGen
    stream::LibSndFile.SndFileSource
    bufflen::Int
end

function FileGen(filename::String, bufflen::Int = 480)
    return FileGen(loadstreaming(filename), bufflen)
end

function Base.iterate(f::FileGen, state = nothing)
    data = vec(mean(Float32.(read(f.stream, f.bufflen).data), dims=2))
    if length(data) == 0
        return nothing
    end
    if length(data) < f.bufflen
        data = vcat(data, zeros(Float32, f.bufflen - length(data)))
    end
    return data, state
end

let
    @info("Creating socket")
    udpsock = UDPSocket()
    bind(udpsock, ip"::", 0; reuseaddr=false, ipv6only=true)
    Sockets.setopt(udpsock, enable_broadcast=true)
    
    @info("Creating encoder")
    enc = OpusEncoder(48000, 1)
    t_start = time()
    packets_sent = 0
    multicast_addr = ip"ff12:5040::1337:0"
    multicast_port = 5040
    for data in FileGen(expanduser("~/britbutbetter.flac"))
        packet = build_packet!([enc], [data], time())
        send(udpsock, multicast_addr, multicast_port, packet)
        packets_sent += 1
        println(@sprintf("[%d]: RMS: %.3f", packets_sent, sqrt(mean(data.^2))))
        #sleep(max((t_start + 4*0.01*(packets_sent - 1)) - time(), 0))
        sleep(0.7*480.0/48000)
    end
end

