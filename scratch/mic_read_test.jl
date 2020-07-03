using PortAudio, SampledSignals

input = PortAudioStream("Loopback Audio", 2, 0; eltype=Float32, samplerate=48000, latency=0.01)
output = PortAudioStream("MacBook Pro Speakers", 0, 2; eltype=Float32, samplerate=48000, latency=0.01)

buffers = Channel{SampleBuf{Float32,2}}(2)
t_read = @async begin
    while isopen(input)
        put!(buffers, read(input, 0.01s))
    end
end

t_write = @async begin
    while isopen(output)
        write(output, take!(buffers))
    end
end
