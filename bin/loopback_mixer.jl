using Popuset, SampledSignals

# Set verbosity level and stats printing
Popuset.set_verbose(0)
Popuset.set_print_stats(false)
Popuset.set_mute(false)

ai = AudioInput("stage1")
ao = AudioOutput("stage2")

mx = TimelyMixer()
buffer_size = 100ms
t_read = Threads.@spawn begin
    while isopen(ai)
        buff = read(ai, buffer_size)

        # Tag these pieces of audio to be played X ms in the future
        buff = Popuset.delay_tsb(buff, 150ms)

        queue(mx, "default", buff)
        yield()

        @info("queued $(nframes(buff)) samples at timestamp $(buff.timestamp)")
    end
end

t_mix = Threads.@spawn begin
    data = Array{Float32}(undef, round(Int, inseconds(buffer_size)*samplerate(ao)), nchannels(ao))
    while isopen(ao)
        @info("waiting for buffer request", ts=Timestamp())
        take!(ao.buffer_request)

        # We mix for the next available buffer slot in the port audio shim
        #Popuset.synchronize!(ao)
        next_mix_time = Popuset.get_next_mix_timestamp(ao)

        @info("Mixing for", mix_timestamp=next_mix_time, ts=Timestamp())
        data[:,:] .= 0.0
        out_buff = TimestampedSampleBuf(data, next_mix_time)
        mix!(mx, out_buff)
        write(ao, out_buff)
    end
end

# wait(t_read)

# t_write = Threads.@spawn begin
#     while isopen(ao)
#         try
#             @time buff = take!(write_queue)
#             write(ao, buff)
#         catch e
#             if isa(e, InvalidStateException)
#                 continue
#             end
#         end
#     end
# end


# sleep(0.01)
# #wait(t_mix)
# close(ao)
