using Base.Threads, SHA, CRC

mutable struct CacheEntry
    timestamp::UInt64
    packet::Vector{UInt8}
    last_hit::Float64
end

mutable struct PacketCache
    # Packet cache that we can retransmit at will, along with retransmission hysterysis tracker
    cache::Vector{CacheEntry}

    # Concurrency, amirite?
    cache_lock::ReentrantLock

    # The maximum number of packets we'll keep in memory.
    max_packets::Int

    function PacketCache(max_packets = 10000)
        return new(
            CacheEntry[],
            ReentrantLock(),
            max_packets,
        )
    end
end

function hit(pc::PacketCache, timestamp::UInt64, backoff_time::Float64)
    curr_time = time()
    lock(pc.cache_lock) do
        for idx in 1:length(pc.cache)
            if pc.cache[idx].timestamp == timestamp && curr_time - pc.cache[idx].last_hit > backoff_time
                pc.cache[idx].last_hit = curr_time
                return pc.cache[idx].packet
            end
        end
        return nothing
    end
end

function cache!(pc::PacketCache, timestamp::UInt64, packet::Vector{UInt8})
    lock(pc.cache_lock) do
        push!(pc.cache, CacheEntry(timestamp, packet, 0.0f0))
        while length(pc.cache) > pc.max_packets
            popfirst!(pc.cache)
        end
        return nothing
    end
end

function run_packet_cache(pc::PacketCache, sock::UDPSocket; verbose::Bool = false)
    crc32 = crc(CRC_32)
    Threads.@spawn begin
        while true
            try
                # Listen for retransmit requests
                src_addr, data = recvfrom(sock)
                requested_timestamps = reinterpret(UInt64, data)
                for ts in requested_timestamps
                    # Check to see if this timestamp is still within our packet cache and was not asked for less than a few frames ago
                    packet = hit(pc, ts, 0.015)

                    if packet !== nothing
                        if verbose
                            payload = packet[12:end]
                            println("  [0x$(string(ts, base=16))] Retransmitting to $(multicast_group_addr(speaker_group)) (payload len: hash: $(string(crc32(packet),base=16)))")
                        end
                        send(sock, multicast_group_addr(speaker_group), src_addr.port, packet)
                    else
                        println("  [0x$(string(ts, base=16))] Ignoring from $(src_addr.host).$(src_addr.port)")
                    end
                end
            catch e
                if isa(e, InterruptException) || isa(e, EOFError)
                    break
                else
                    rethrow(e)
                end
            end
        end
    end
end