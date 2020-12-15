function delay_tsb(x::TimestampedSampleBuf, delay_amnt)
    return TimestampedSampleBuf(x.data, x.timestamp + delay_amnt)
end
