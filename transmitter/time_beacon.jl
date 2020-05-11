#!/usr/bin/env julia
@info("Loading libraries")
using Sockets, Statistics, Printf

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
    # Call clock_gettime() with realtime or monotonic or something
    tx = timespec(0,0)
    CLOCK_REALTIME = 0
    CLOCK_MONOTONIC = 1
    s = ccall((:clock_gettime,:librt), Int32, (Int32, Ptr{timespec}), CLOCK_REALTIME, Ref(tx))
    if s < 0
        error("Unable to call clock_gettime()")
    end
    return UInt64(tx.sec*UInt64(1e9) + tx.nsec)
end

function join_group(sock::UDPSocket, addr)
    UV_JOIN_GROUP = Cint(1)
    r = ccall(:uv_udp_set_membership, Cint, (Ptr{Cvoid}, Cstring, Cstring, Cint), sock.handle, string(addr), C_NULL, UV_JOIN_GROUP)
    if r != 0
        throw(Base.IOError("uv_udp_set_membership", r))
    end
    return
end

let
    @info("Creating socket")
    packets_sent = 0
    multicast_addr = ip"ff12:5041::1337:0"
    time_port = 1554
    
    sock = UDPSocket()
    bind(sock, ip"::", time_port; reuseaddr=false, ipv6only=true)
    join_group(sock, multicast_addr)

    @info("Listening")
    while true
        try
            src_addr, data = recvfrom(sock)
            t_rx = gettime_ns()
            t_tx_speaker = reinterpret(UInt64, data)[1]
            send(sock, src_addr.host, src_addr.port, UInt64[UInt64(t_rx), UInt64(t_tx_speaker)])
            println("[0x$(string(t_rx, base=16))] Sent a pong to [$(src_addr.host)]:$(src_addr.port)")
        catch e
            if isa(e, InterruptException)
                break
            else
                rethrow(e)
            end
        end
    end
    close(sock)
end

