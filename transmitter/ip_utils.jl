# This function will be included in Julia 1.5+ (https://github.com/JuliaLang/julia/pull/35521)
function join_multicast_group(sock::UDPSocket, addr)
    UV_JOIN_GROUP = Cint(1)
    r = ccall(:uv_udp_set_membership, Cint, (Ptr{Cvoid}, Cstring, Cstring, Cint), sock.handle, string(addr), C_NULL, UV_JOIN_GROUP)
    if r != 0
        throw(Base.IOError("uv_udp_set_membership", r))
    end
    return
end

multicast_group_addr(idx::Int) = parse(IPAddr, "ff12:5041::1337:$(string(idx, base=16))")

function open_multicast_socket(port)
    # Create socket, bind it to any ipv6 interface on this port
    sock = UDPSocket()
    bind(sock, ip"::", port; reuseaddr=false, ipv6only=true)

    # Enable broadcast and join the relevant speaker ipv6 multicast group
    Sockets.setopt(sock, enable_broadcast=true, multicast_loop=false)
    join_multicast_group(sock, multicast_group_addr(speaker_group))

    return sock
end