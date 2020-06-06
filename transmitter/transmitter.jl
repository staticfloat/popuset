@info("Loading libraries")
using Sockets, Opus, FileIO, LibSndFile, Statistics, Printf
POPUSET_DIR = dirname(@__DIR__)

include("config.jl")
include("packetcache.jl")
include("timesync.jl")
include("ip_utils.jl")
include("audiofile.jl")

# Start up the packet cache and timesync
timesync_sock = open_multicast_socket(timesync_port)
task_timesync = run_timesync(timesync_sock; verbose=true)

pc = PacketCache()
audio_sock = open_multicast_socket(audio_port)
task_packet_cache = run_packet_cache(pc, audio_sock; verbose=true)

# Warm everything up by sending some silence
send_file(audio_sock, joinpath(POPUSET_DIR, "data", "silence.flac"), pc)
