struct FileGen
    stream::LibSndFile.SndFileSource
    bufflen::Int
end

function FileGen(filename::String, bufflen::Int = 480)
    return FileGen(loadstreaming(filename), bufflen)
end

function Base.iterate(fg::FileGen, state = nothing)
    data = Float32.(read(fg.stream, fg.bufflen).data)
    if size(data, 1) == 0
        return nothing
    end

    if size(data, 1) < fg.bufflen
        data = vcat(data, zeros(Float32, fg.bufflen - size(data,1), size(data,2)))
    end

    # Convert data from a matrix to a vector of vectors
    return [data[:,idx] for idx in 1:size(data,2)], state
end

function Base.length(fg::FileGen)
    return ceil(Int, fg.stream.sfinfo)
end

num_channels(fg::FileGen) = fg.stream.sfinfo.channels
num_buffers(fg::FileGen) = ceil(Int, fg.stream.sfinfo.frames/fg.bufflen)
samplerate(fg::FileGen) = fg.stream.sfinfo.samplerate



function encode_packet!(encoders::Vector{OpusEncoder},
                        audio_data::Vector{Vector{Float32}},
                        packet_time::UInt64)
    # First, encode all audio data using our Opus encoders
    enc_packets = [Opus.encode_packet(enc, data) for (enc, data) in zip(encoders, audio_data)]
    
    # Build simple seriliazed packet representation:
    #
    # Packet layout:
    #  - presentation_timestamp (uint64 in us)
    #  - channels_included (uint16)
    #  - channel_idx offset (uint16)
    #  - packet_lengths (channels_included * uint16)
    #  - packets (channels_included .* packet_lengths * uint8)
    io = IOBuffer()
    channels_included = length(encoders)
    write(io, packet_time)
    write(io, UInt16(channels_included))
    write(io, UInt16(0))
    for idx in 1:channels_included
        write(io, UInt16(length(enc_packets[idx])))
    end
    for idx in 1:channels_included
        write(io, enc_packets[idx])
    end

    # Return serialized packet.
    return take!(io)
end

function send_file(audio_sock, filename, pc; latency_ms=40, bitrate=128000)
    # Open file
    fg = FileGen(filename)

    if samplerate(fg) != 48000
        @warn("Incorrect samplerate, resampling not yet supported!")
    end

    encs = [OpusEncoder(48000, 1; packetloss_pct=5, bitrate=bitrate) for idx in 1:num_channels(fg)]
    packet_time = 480.0/48000
    packet_time_ns = UInt64(packet_time*1e9)
    times_slept = 0

    tx_start = time()
    tx_start_ns = UInt64(round(tx_start*1e9, digits=-5))

    # Always start with a packet of silence, to prime the decoders.
    packet = encode_packet!(encs, [zeros(Float32, 480) for idx in 1:num_channels(fg)], tx_start_ns + UInt64(latency_ms*1e6) - packet_time_ns)
    send(audio_sock, multicast_group_addr(speaker_group), audio_port, packet)

    num_packets_sent = 0
    tx_errs = Float64[]
    for data in FileGen(filename)
        # Prepare the next packet for transmission
        presentation_time = tx_start_ns + num_packets_sent*packet_time_ns + UInt64(latency_ms*1e6)
        packet = encode_packet!(encs, data, presentation_time)
        cache!(pc, presentation_time, packet)
        
        # Sleep until a little bit before the next packet's transmission time
        curr_time = time()
        next_packet_transmission_time = tx_start + (num_packets_sent - 1)*packet_time
        if curr_time < next_packet_transmission_time - 0.001
            sleep(0.9 * (next_packet_transmission_time - curr_time))
        end
        send(audio_sock, multicast_group_addr(speaker_group), audio_port, packet)

        # Track transmission time error
        tx_err = abs(time() - next_packet_transmission_time)*1000
        push!(tx_errs, tx_err)
        while length(tx_errs) > 100
            popfirst!(tx_errs)
        end

        num_packets_sent += 1
        if num_packets_sent % 100 == 0
            avg_rms = mean([sqrt(mean(d.^2)) for d in data])
            println(@sprintf("[%.1f%%]: RMS: %.3f (%.1f packets per second), |tx_errs|: (mu: %.1fms, min: %.1fms, max: %.1fms)", num_packets_sent*100.0/num_buffers(fg), avg_rms, num_packets_sent/(curr_time - tx_start), mean(tx_errs), minimum(tx_errs), maximum(tx_errs)))
        end
    end
end
