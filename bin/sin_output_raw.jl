using Popuset, SampledSignals

# Set verbosity level and stats printing
Popuset.set_verbose(0)
Popuset.set_print_stats(false)
Popuset.set_mute(false)

ao = AudioOutput("stage2")
# Get 10ms buffer
N = round(Int64, samplerate(ao)/100)
sin_buff = 0.2f0 .* Float32.(sin.((0:(N-1))*1000*2π/samplerate(ao)))
sin_buff = hcat(sin_buff, sin_buff)

t_write = Threads.@spawn begin
    while isopen(ao)
        try
            write(ao, sin_buff)
            yield()
        catch e
            if isa(e, InvalidStateException)
                continue
            end
        end
    end
end
