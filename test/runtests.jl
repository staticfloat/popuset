using Test, Popuset, SampledSignals

@testset "Time" begin
    # Basic tests
    t = gettime_ns()
    @test isa(t, UInt64)
    @test t > 0

    # Test different samplerates
    ts = Timestamp(t, 1e9)
    @test ts.samples == t
    ts = Timestamp(t; samplerate=1e6)
    @test ts.samples == round(UInt64, t/1e3)
    @test Timestamp(ts).samples == ts.samples
    @test samplerate(Timestamp(ts)) == samplerate(ts)

    # Inequalities and arithmetic
    ts2 = Timestamp(; samplerate=1e6)
    @test ts2 > ts
    @test Timestamp() > ts
    ts3 = (ts2 + ts)/2
    @test ts3 > ts
    @test ts3 < ts2
    @test ts.samples + 1 == (ts + 1).samples
    @test ts < ts + 1

    # Unitful arithmetic
    @test (ts + 1ns).samples == ts.samples
    @test (ts + 1μs).samples == ts.samples + 1
    @test (ts + 1ms).samples == ts.samples + 1000
end

@testset "TimestampedSampleBuf" begin
    buff = TimestampedSampleBuf(100, 2, Timestamp(UInt64(0); samplerate=10.0))
    @test nframes(buff) == 100
    @test samplerate(buff) == 10.0
    @test start(buff) == Timestamp(UInt64(0), samplerate(buff))
    @test stop(buff) == Timestamp(UInt64(nframes(buff)-1), samplerate(buff))

    # Everything starts as a zero
    @test all(buff .== 0.0)

    # Set the 11th frame as ones, and ensure we can index it by time as well:
    buff[11, :] .= 1.0
    @test all(buff[Timestamp(UInt64(10), samplerate(buff)), :] .== 1.0)

    # Also test timeslicing
    timeslice_start = Timestamp(UInt64(9), samplerate(buff))
    timeslice_stop  = Timestamp(UInt64(11), samplerate(buff))
    timeslice = buff[timeslice_start:timeslice_stop]
    @test size(timeslice, 1) == 3
    @test all(timeslice .== buff[10:12, :])

    # Also test copy constructor
    buff = TimestampedSampleBuf(SampleBuf(Float32, 48000, 480, 2));
    @test samplerate(buff) == 48000.0
    @test nframes(buff) == 480
    @test start(buff) > Timestamp(UInt64(0); samplerate=samplerate(buff))
end

function make_sin_buff(ts)
    data = Float32.(sin.((0:479)*777*2π/samplerate(ts)))
    data = hcat(data, data)
    return TimestampedSampleBuf(data, ts)
end

@testset "TimelyMixer" begin
    start_time = Timestamp(;samplerate=48000)
    centered_buff = make_sin_buff(start_time)

    @testset "Basic queueing" begin
        mx = TimelyMixer()
        queue(mx, "client1", centered_buff)
        queue(mx, "client2", centered_buff)

        # A duplicate buffer gets dropped
        queue(mx, "client1", centered_buff)

        # Test that they have queued as expected
        @test length(keys(mx.stream_buffers)) == 2
        @test all(length(buffs) == 1 for (stream, buffs) in mx.stream_buffers)

        # Buffers in streams append as expected
        queue(mx, "client1", make_sin_buff(start_time + nframes(centered_buff)))

        # Mix the output, test that it has correctly summed our two buffers together
        mix_output = TimestampedSampleBuf(480, 2, start_time)
        mix!(mx, mix_output)
        @test all(mix_output .== centered_buff*2)
        @test isempty(mx.stream_buffers["client2"])
        @test !isempty(mx.stream_buffers["client1"])

        # Mix for the second chunk of time
        mix_output = TimestampedSampleBuf(480, 2, start_time + nframes(centered_buff))
        mix!(mx, mix_output)
        @test all(mix_output .== centered_buff)
        @test all(isempty(buffs) for (stream, buffs) in mx.stream_buffers)
    end

    # Next, mix in a buffer that is shifted in time, ensure that it works out:
    @testset "Shifting" begin
        mx = TimelyMixer()
        # Shift a sin buffer forward 10 samples
        shifted_buff = make_sin_buff(start_time + 10)
        shifted_buff[:, 2] .= 0.0
        queue(mx, "client1", centered_buff)
        queue(mx, "client2", shifted_buff)
        mix_output = TimestampedSampleBuf(480, 2, start_time)
        mix!(mx, mix_output)
        @test all(mix_output[1:10, 1] .== centered_buff[1:10, 1])
        @test all(mix_output[11:end, 1] .== centered_buff[11:end, 1] .+ shifted_buff[1:end-10, 1])
        @test all(mix_output[:, 2] .== centered_buff[:, 2])
    end

    @testset "Window overlap" begin
        # Next, test mixing of a poorly-aligned buffer
        mx = TimelyMixer()
        buff1 = centered_buff
        buff2 = make_sin_buff(start_time + nframes(buff1))
        queue(mx, "client1", buff1)
        queue(mx, "client1", buff2)

        mix_output = TimestampedSampleBuf(480, 2, start_time + div(nframes(buff1), 4))
        mix!(mx, mix_output)
        @test mix_output[1:3*div(end,4), :] ≈ buff1[div(end,4)+1:end, :]
        @test mix_output[3*div(end,4)+1:end, :] ≈ buff2[1:div(end,4), :]
    end

    @testset "Porous Mixing" begin
        # Test mixing with a hole
        mx = TimelyMixer()
        buff1 = centered_buff
        buff2 = make_sin_buff(start_time + nframes(buff1) + 10)

        queue(mx, "client1", buff1)
        queue(mx, "client1", buff2)
        mix_output = TimestampedSampleBuf(480, 2, start_time + div(nframes(buff1), 4))
        @test_logs (:warn, r"porous stream!") match_mode=:any begin
            mix!(mx, mix_output)
        end
        @test mix_output[1:3*div(end,4), :] ≈ buff1[div(end,4)+1:end, :]
        @test all(mix_output[3*div(end,4)+1:3*div(end,4)+11, :] .== 0.0)
        @test mix_output[3*div(end,4)+11:end, :] ≈ buff2[1:div(end,4) - 10, :]
    end
end
