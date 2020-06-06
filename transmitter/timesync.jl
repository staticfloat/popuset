using Sockets, Statistics, Printf, Base.Threads

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
    # Call clock_gettime() with realtime
    tx = timespec(0,0)
    CLOCK_REALTIME = 0
    s = ccall((:clock_gettime,:librt), Int32, (Int32, Ptr{timespec}), CLOCK_REALTIME, Ref(tx))
    if s < 0
        error("Unable to call clock_gettime(): $(s)")
    end
    return UInt64(tx.sec*UInt64(1e9) + tx.nsec)
end

function run_timesync(time_sock::UDPSocket; verbose::Bool = false)
    Threads.@spawn begin
        while true
            try
                # Receive a ping from a client
                src_addr, data = recvfrom(time_sock)

                # As quickly as possible, record reception time
                t_rx = gettime_ns()
                t_tx_speaker = reinterpret(UInt64, data)[1]

                # Send back to the requester t_rx and the received UInt64, so that they can timesync properly.
                send(time_sock, src_addr.host, src_addr.port, UInt64[UInt64(t_rx), UInt64(t_tx_speaker)])

                # Report what we did, if we're verbose
                if verbose
                    println("[0x$(string(t_rx, base=16))] Sent a pong to [$(src_addr.host)]:$(src_addr.port)")
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