using Popuset, SampledSignals

# Set verbosity level and stats printing
Popuset.set_verbose(0)
Popuset.set_print_stats(false)
Popuset.set_mute(false)

ai = AudioInput("stage1")
ao = AudioOutput("stage2")

write_queue = Channel{Any}()
buffer_size = 10ms
t_read = Threads.@spawn begin
    while isopen(ai)
        put!(write_queue, read(ai, buffer_size))
        yield()
    end
end

t_write = Threads.@spawn begin
    while isopen(ao)
        try
            write(ao, take!(write_queue))
            yield()
        catch e
            if isa(e, InvalidStateException)
                continue
            end
        end
    end
end

# try
#     wait(t_read)
# catch e
#     if isa(e, InterruptException)
#         println("Closing gracefully...")
#         close(ai)
#     end
# end

# wait(t_read)
# close(ao)
# wait(t_write)
