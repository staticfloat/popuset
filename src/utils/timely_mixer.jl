import SampledSignals: mix!
export TimelyMixer, queue, mix!


mutable struct TimelyMixer
    stream_buffers::Dict{Any, Vector{TimestampedSampleBuf}}
    queue_lock::Threads.ReentrantLock

    TimelyMixer() = new(
        Dict{Any,Vector{TimestampedSampleBuf}}(),
        Threads.ReentrantLock(),
    )
end

function queue(mx::TimelyMixer, stream, buff::TimestampedSampleBuf)
    lock(mx.queue_lock) do
        if !haskey(mx.stream_buffers, stream)
            mx.stream_buffers[stream] = TimestampedSampleBuf[]
        end

        # Insert in-order.
        buffs = mx.stream_buffers[stream]

        # Fast, happy path; our buffer should just be pushed on the end.
        if isempty(buffs) || buffs[end].timestamp < buff.timestamp
            #@info("Queued for presentation", timestamp=buff.timestamp)
            push!(buffs, buff)
            return
        end

        # Otherwise, iterate over the buffer (backwards), checking for
        # duplicate queueing as well as looking for the right place to
        # queue this bad boy
        for idx in length(buffs):-1:1
            # If we already have this buffer, drop it
            if buffs[idx].timestamp == buff.timestamp
                return
            end

            # Otherwise, queue it as soon as we find the appropriate location
            if buffs[idx].timestamp < buff.timestamp
                insert!(buffs, idx + 1, buff)
                return
            end
        end

        # Weird, but sure, we can queue it at the very beginning
        insert!(buffs, 1, buff)
        return
    end
end

function prune!(mx::TimelyMixer, horizon::Timestamp)
    lock(mx.queue_lock) do
        for (stream, buffs) in mx.stream_buffers
            while !isempty(buffs) && stop(buffs[1]) <= horizon
                popfirst!(buffs)
            end
        end
    end
end

function find_next_buff(buffers::Vector{TimestampedSampleBuf}, ts_start::Timestamp, ts_stop::Timestamp)
    for idx in 1:length(buffers)
        if start(buffers[idx]) <= ts_stop && stop(buffers[idx]) >= ts_start
            return buffers[idx]
        end
    end
    return nothing
end

function mix!(mx::TimelyMixer, out_buff::TimestampedSampleBuf; auto_prune::Bool = true)
    # The presentation time we're mixing for is encoded in out_buff's timestamp.
    # First, we drop any buffers that are now considered out-of-date:
    if auto_prune
        prune!(mx, start(out_buff) - 1)
    end

    lock(mx.queue_lock) do
        for (stream, buffs) in mx.stream_buffers
            # Just skip this stream if it's empty.  Silly wabbit.
            if isempty(buffs)
                continue
            end

            # For each stream, grab the next nframes(out_buff) frames:
            write_time = start(out_buff)
            buffers_used = 0
            while write_time <= stop(out_buff)
                # Find the next buffer that starts at or after our write offset within this out buffer
                next_buff = find_next_buff(buffs, write_time, stop(out_buff))
                if next_buff === nothing
                    # Stop if we've run out of stuff to write.
                    @warn("underrun!", stream, write_time, stop(out_buff))
                    break
                end

                # Determine the start/stop times of the slice we're extracting from this buffer
                if start(next_buff) > write_time
                    @warn("porous stream!", stream, write_time, start(next_buff))
                end
                slice_start = max(write_time, start(next_buff))
                slice_stop = min(stop(out_buff), stop(next_buff))

                # Mix into `out_buff`
                #@info("mixing", stream, out_buff, next_buff, start(out_buff), stop(out_buff), start(next_buff), stop(next_buff), slice_start, slice_stop)
                out_buff[slice_start:slice_stop] .+= next_buff[slice_start:slice_stop]
                buffers_used += 1

                # Increment write_time to the next sample
                write_time = slice_stop + 1
            end
            if write_time.samples != stop(out_buff).samples + 1
                @warn("Didn't fill up buffer!", deficit=stop(out_buff).samples + 1 - write_time.samples)
            end
            #@info("Mixed using $(buffers_used) buffers", out_buff)
        end
    end

    # At the end of all things, prune even more exhausted buffers
    if auto_prune
        prune!(mx, stop(out_buff))
    end
end
