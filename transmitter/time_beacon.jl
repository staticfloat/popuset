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
    # Call clock_gettime(CLOCK_REALTIME, &tx)
    tx = timespec(0,0)
    s = ccall((:clock_gettime,:librt), Int32, (Int32, Ptr{timespec}), 0, Ref(tx))
    if s < 0
        error("Unable to call clock_gettime()")
    end
    return tx.sec*UInt64(1e9) + tx.nsec
end

let
    @info("Creating socket")
    tx_sock = UDPSocket()
    bind(tx_sock, ip"::", 0; reuseaddr=false, ipv6only=true)
    Sockets.setopt(tx_sock, enable_broadcast=true)

    packets_sent = 0
    multicast_addr = ip"ff12:5040::1337:0"
    multicast_port = 1554
    while true
        try
            t_curr = gettime_ns()
            send(tx_sock, multicast_addr, multicast_port, [t_curr])
            packets_sent += 1
            println(@sprintf("0x%llx,", t_curr))
            sleep(.2)
        catch e
            if isa(e, InterruptException)
                break
            else
                rethrow(e)
            end
        end
    end
    close(tx_sock)
end

