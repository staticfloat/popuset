@info("Loading libraries")
using Sockets, Opus, FileIO, LibSndFile, Statistics, Printf, Revise

function build_packet!(encoders::Vector{OpusEncoder},
                       audio_data::Vector,
                       packet_time::UInt64)
    # First, encode all audio data using our Opus encoders
    enc_packets = [Opus.encode_packet(enc, data) for (enc, data) in zip(encoders, audio_data)]
    
    # Build simple seriliazed packet representation:
    #
    # Packet layout:
    #  - timestamp (uint64 in us)
    #  - num_channels (uint8)
    #  - packet_lengths (num_channels * uint16)
    #  - packets (num_channels .* packet_lengths * uint8)
    io = IOBuffer()
    num_channels = length(encoders)
    write(io, packet_time)
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

function send_file(filename; latency_ms=40, bitrate=128000)
    @info("Creating socket")
    udpsock = UDPSocket()
    bind(udpsock, ip"::", 0; reuseaddr=false, ipv6only=true)
    Sockets.setopt(udpsock, enable_broadcast=true)
    
    @info("Creating encoder")
    enc = OpusEncoder(48000, 1; packetloss_pct=1, bitrate=bitrate)
    packets_sent = 0
    multicast_addr = ip"ff12:5041::1337:0"
    multicast_port = 5040
    tx_start = time()
    tx_start_ns = UInt64(round(time()*1e9, digits=-5))
    packet_time = 480.0/48000
    packet_time_ns = UInt64(packet_time*1e9)
    times_slept = 0
    for data in FileGen(filename)
        while time() < (tx_start + (packets_sent - 1)*packet_time)
            sleep(0.001)
            times_slept += 1
        end

        presentation_time = tx_start_ns + packets_sent*packet_time_ns + UInt64(latency_ms*1e6)
        packet = build_packet!([enc], [data], presentation_time)
        send(udpsock, multicast_addr, multicast_port, packet)
        packets_sent += 1
        if packets_sent % 117 == 1
            println(@sprintf("[%d slept %.1f/packet]: RMS: %.3f (%.1f packets per second)", packets_sent, times_slept/packets_sent, sqrt(mean(data.^2)), packets_sent/(time() - tx_start)))
        end
    end
end

