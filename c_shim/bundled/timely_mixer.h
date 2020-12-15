#include <unordered_map>
#include <list>
#include <inttypes.h>

template <typename T>
typedef struct {
    T * data;
    uint32_t nframes;
    uint8_t nchannels;
    uint64_t presentation_time;
} TimestampedBuffer<T>;

template <typename T>
class TimelyMixer<T> {
    public:
        std::unordered_map<uint64_t, std::list<TimestampedBuffer<T> > > buffers;
        uint64_t global_clock_offset;
        TimelyMixer() { 
            this->global_clock_offset = 0;
        }

        uint64_t seconds_to_samples(double timestamp, double samplerate, uint64_t offset = this->global_clock_offset) {
            return (uint64_t)llround(timestamp*samplerate) + offset;
        }

        double samples_to_seconds(uint64_t samples, double samplerate, uint64_t offset = this->global_clock_offset) {
            return (samples - offset)/samplerate;
        }

        /*
         *    queue(key, data, nframes, samplerate, ptime_in_samples)
         * 
         * Queues a new buffer from the given client identified by `key` into that client's buffer list.
         */
        void queue(uint64_t key, T * data, uint32_t nframes, uint8_t nchannels, uint64_t presentation_time) {
            // Initialize new buffer list for the given client, if we've never seen it before.
            if (this->buffers.find(key) == this->buffers.end()) {
                this->buffers[key] = std::list<TimestampedBuffer<T> >();
            }
            auto buffs = this->buffers[key];

            for (auto ritty = buffs.rbegin(); ritty != buffs.rend(); ++ritty) {
                // The first buffer that is _before_ the one we're trying to insert gives us the insertion location.
                if (ritty->presentation_time < presentation_time) {
                    buffs.insert(ritty.base(), TimestampedBuffer<T>(
                        data,
                        nframes,
                        nchannels,
                        presentation_time,
                    ));
                }
            }
        }

        /*
         *     prune(horizon)
         * 
         * Prune all buffers that end before the given time horizon, meaning they will never be used again.
         */
        void prune(uint64_t horizon) {
            for (auto iter = this->buffers.begin(); iter != this->buffers.end(); ++iter) {
                auto buffs = iter->second;
                while (!isempty(buffs)) {
                    auto front_buff = buffs.front();
                    // Break out if front_buff is actually still useful.
                    if (front_buff.presentation_time + front_buff.nframes > horizon) {
                        break;
                    }

                    // Otherwise, pop it from `popfront()`
                    buffs.popfront();

                    // Free the buffer memory
                    delete front_buff.data;
                    delete front_buff;
                }
            }
        }
};

// C translators for Julia
//timelymixer_queue(uint64_t key, )