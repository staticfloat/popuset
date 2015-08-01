#include "audio.h"
#include "net.h"
#include "util.h"
#include <string.h>
#include <unordered_set>
#include <fcntl.h>


// Right now, only covers mono -> multichannel and multichannel -> mono
void mixdown_channels( float * in_data, float * out_data, unsigned int num_samples, unsigned int in_channels, unsigned int out_channels ) {
    if( in_channels == out_channels )
        return;

    switch( in_channels ) {
        case 1:
            // Just copy input channel to all output channels
            for( int i=0; i<num_samples; ++i ) {
                for( int k=0; k<out_channels; ++k )
                    out_data[i*out_channels + k] = in_data[i];
            }
        default:
            // If output is mono, just mix all input channels together
            if( out_channels == 1 ) {
                for( int i=0; i<num_samples; ++i ) {
                    out_data[i] = 0.0f;
                    for( int k=0; k<in_channels; ++k )
                        out_data[i] += in_data[i*in_channels + k];
                    out_data[i] /= in_channels;
                }
            }

            // If output is not mono, I don't know what to do!
            printf("ERROR: Cannot mixdown %d -> %d!\n", in_channels, out_channels);
            break;
    }
}


void * audio_thread(void * device_ptr) {
    // Our client identity list, mapping to output sockets.  These sockets go:
    // Broker [PUB] -> Audio thread [SUB]
    std::map<std::string, void *> clientSocks;
    std::map<void *, int> clientOffsets;

    // Scratch space for encoded data
    unsigned char * encoded_data = new unsigned char[MAX_DATA_PACKET_LEN];

    // I think it's pretty probable that we'll need at least 10ms for stereo scratch space; let's see if I'm right!
    float * temp_buff = new float[2*10*SAMPLE_RATE/1000];
    unsigned int temp_buff_len = 2*10*SAMPLE_RATE/1000;
    float * mix_buff = new float[device.num_samples*10*SAMPLE_RATE/1000];
    unsigned int mix_buff_len = device.num_samples*10*SAMPLE_RATE/1000;

    // Grab our device from the device_ptr passed in to this thread
    audio_device & device = (audio_device *)device_ptr;

    // Build up a pollitem_t group from our sockets
    zmq_pollitem_t items[66];
    items[0].socket = device.cmd_sock;
    items[1].socket = device.raw_audio_out;
    
    // We only deal in ZMQ_POLLIN events, so set those up first
    for( int i=0; i<sizeof(items)/sizeof(zmq_pollitem_t); ++i )
        items[i].events = ZMQ_POLLIN;

    // Let's listen for ZMQ events, and mix some wicked sick beats
    bool keepRunning = true;
    while( keepRunning ) {
        // Wait for an event
        int rc = zmq_poll(items, 2 + clientSocks.size(), -1);

        // Did we get an event?!
        if( rc > 0 ) {
            // Are we receiving a command?
            if( items[0].revents & ZMQ_POLLIN ) {
                audio_device_command cmd;
                readCommand(device.cmd_sock, &cmd);

                switch( cmd->type ) {
                    case CMD_CLIENTLIST:
                        std::unordered_set<std::string> clients;

                        // The data field of cmd now holds a NULL-separated list of client identities,
                        // ending with an entry of zero length (an extra NULL at the end of the last id)
                        unsigned int idx = 0;
                        while( cmd->data[idx] != 0 ) {
                            unsigned int identity_len = strlen(cmd->data + idx);

                            const char * identity = cmd->data + idx;
                            // Add this string to our client set:
                            clients.insert(identity);

                            // If such a client does not already exist in clientSocks, create one!
                            if( clientSocks.find(identity) == clientSocks.empty() ) {
                                // Create a socket to listen for data coming from this client:
                                void * sock = zmq_socket(zmq_ctx, ZMQ_SUB);
                                zmq_setsockopt(sock, ZMQ_SUBSCRIBE, identity, identity_len);
                                zmq_connect(sock, "inproc://broker.output");
                                clientSocks[identity] = sock;
                                clientOffsets[sock] = 0;
                            }
                            idx += identity_len + 1;
                        }

                        // Now go through all the clients we already have and ensure they're still on the list
                        for( auto& kv : clientSocks ) {
                            if( clients.count(kv->first) == 0 ) {
                                // Let's cleanup this client!
                                clientOffsets.erase(kv->second);
                                zmq_close(kv->second);
                                clientSocks.erase(kv);
                            }
                        }

                        // Finally, rebuild items:
                        int i = 2;
                        for( auto& kv : clientSocks )
                            items[i].socket = kv->second;
                        break;
                    case CMD_SHUTDOWN:
                        // The ultimate surrender
                        keepRunning = false;
                        break;
                    case CMD_INVALID:
                    default:
                        break;
                }
            }

            // Did we just get audio from the device?
            if( items[1].revents & ZMQ_POLLIN ) {
                // Read it in
                zmq_msg_t msg;
                zmq_msg_init(&msg);
                zmq_recvmsg(device.raw_audio_out, &msg, 0);
                int dec_len = zmq_msg_size(&msg);
                int num_samples = dec_len/(sizeof(float)*device.num_channels);

                // Encode it:
                int enc_len = opus_encode_float(encoder, zmq_msg_data(&msg), num_samples, encoded_data, MAX_DATA_PACKET_LEN );

                // Send the decoded length
                dec_len = htonl(dec_len);
                zmq_send(device.input_sock, &dec_len, sizeof(int), ZMQ_SENDMORE);

                // Send number of channels
                int num_channels = htonl(device.num_channels);
                zmq_send(device.input_sock, &num_channels, sizeof(int), ZMQ_SENDMORE);

                // Next, send the encoded audio!
                zmq_send(device.input_sock, encoded_data, enc_len, 0);

                // Small amount of cleanup
                zmq_msg_close(msg);
            }

            // Check to see if the client offsets need to be updated
            unsigned int amount_read = device.out_buff->getDelta();
            if( amount_read > 0 ) {
                // Update client offsets by subtracting the amount the audio device itself has read, minimum zero
                for( auto kv : clientOffsets )
                    kv->second = max(0, kv->second - (int)amount_read);
            }

            // Did we just get audio from a client?
            for( int i=0; i<clientSocks.size(); ++i ) {
                if( items[i + 2].revents & ZMQ_POLLIN ) {
                    // Read in the audio from this client; first decoded length
                    int dec_len, num_channels;
                    zmq_recv(items[i+2].socket, &dec_len, sizeof(int), 0);
                    dec_len = ntohl(dec_len);
                    int num_samples = dec_len/(sizeof(float)*num_channels);

                    // Then number of channels
                    zmq_recv(items[i+2].socket, &num_channels, sizeof(int), 0);
                    num_channels = ntohl(num_channels);

                    // Finally, the audio itself
                    zmq_recv(items[i+2].socket, encoded_data, MAX_DATA_PACKET_LEN, 0);

                    // Decode the data, expanding temp_buff if we need to:
                    if( temp_buff_len < num_samples*num_channels ) {
                        delete[] temp_buff;
                        temp_buff_len = num_samples*num_channels;
                        temp_buff = new float[temp_buff_len];
                    }

                    // Decode it into our (possibly newly-widened) temp_buff
                    int actually_dec_len = opus_decode_float(device.decoder, encoded_data, num_samples, temp_buff, temp_buff_len, 0);
                    if( actually_dec_len != dec_len ) {
                        printf("ERROR: actually_dec_len (%d) != dec_len (%d)\n", actually_dec_len, dec_len);
                        break;
                    }

                    // Mix the audio down, expanding mix_buff if we need to:
                    if( mix_buff_len < num_samples*device.num_channels ) {
                        delete[] mix_buff;
                        mix_buff_len = num_samples*device.num_channels;
                        mix_buff = new float[mix_buff_len];
                    }

                    // Mix the audio down for output onto our device
                    mixdown_audio(temp_buff, mix_buff, num_samples, num_channels, device.num_channels);

                    // Write it into our circular buffer! First, lookup the offset this particular client's got:
                    int offset = clientOffsets[items[i+2].socket];
                    device.out_buff->write(num_samples*device.num_channels, offset, mix_buff);

                    // Update the offset
                    clientOffsets[items[i+1].socket] = offset + num_samples*device.num_channels;
                }
            }
        }
    }

    // CLEANUP TIME! Let's blow this popsicle stand!
    while( !clientSocks.empty() ) {
        auto kv = clientSocks.begin();
        zmq_close(kv->second);
        clientSocks.erase(kv);
    }

    delete[] encoded_data;
}



AudioEngine::AudioEngine() {
    // Attempt to initialize PortAudip
    if (Pa_Initialize() != paNoError) {
        fprintf(stderr, "Error: Could not initialize PortAudio.\n");
        throw "Error: Could not initialize PortAudio";
    }

    // Loop through audio devices, initializing encoders/decoders and opening Port devices
    for( auto device : opts.devices ) {
        if( !this->initPortAudio(device) ) {
            throw "Error: Could not initialize PortAudio";
        }
        if( !this->initOpus(device) ) {
            throw "Error: Could not initialize Opus";
        }
        if( !this->initSocks(device) ) {
            throw "Error: Could not initialize sockets";
        }
        // Give ourselves a maximum of 100ms buffer time
        device.out_buff = new RingBuffer(device.num_channels*SAMPLE_RATE/10);
    }
    
    
}

AudioEngine::~AudioEngine() {
    // Send CMD_SHUTDOWN to everybody

    // Join all threads

    // Cleanup audio devices
    for( auto device : opts.devices ) {
        // Stop the stream
        Pa_CloseStream(device.stream);

        // Cleanup encoder/decoder
        if( device.decoder != NULL )
            opus_decoder_destroy(device.decoder);
        if( device.encoder != NULL )
            opus_encoder_destroy(device.encoder);

        // Close sockets
        zmq_close(device.cmd_sock);
        zmq_close(device.input_sock);
        zmq_close(device.output_sock);

        // Cleanup top-tier stuff!
        delete[] device.name;
    }

    // No more Port Audio for us.  :(
    Pa_Terminate();
}

// Initialize Opus {de,en}coders for the given device
bool AudioEngine::initOpus( audio_device & device ) {
    int err;
    if( device.direction != INPUT ) {
        device.decoder = opus_decoder_create(SAMPLE_RATE, device.num_channels, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus decoder with %d channels for %s.\n", device.num_channels, device.name);
            return false;
        }
    }
    if( device.direction != OUTPUT ) {
        device.encoder = opus_encoder_create(SAMPLE_RATE, device.num_channels, OPUS_APPLICATION_AUDIO, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus encoder with %d channels for %s.\n", device.num_channels, device.name);
            return false;
        }
    }
    return true;
}

bool AudioEngine::initSocks( audio_device & device ) {
    // Create command channel listener
    device.cmd_sock = create_sock(ZMQ_DEALER, 10);
    if( device.cmd_sock == NULL ) {
        fprintf(stderr, "Could not create command socket for device %s", device.name);
        return false;
    }
    // Set identity and connect command channel
    zmq_set_sockopt(device.cmd_sock, ZMQ_IDENTITY, &&device, sizeof(audio_device *));
    zmq_connect(device.cmd_sock, "inproc://broker.cmd");


    // Create channel to send recorded audio data out to broker on
    device.input_sock = create_sock(ZMQ_DEALER);
    if( device.input_sock == NULL ) {
        fprintf(stderr, "Could not create input socket for device %s", device.name);
        return false;
    }
    // Do the same for the input channel!
    zmq_set_sockopt(device.input_sock, ZMQ_IDENTITY, &&device, sizeof(audio_device *));
    zmq_connect(device.input_sock, "inproc://broker.input");

    // Create channel to receive raw audio from audio thread.
    device.raw_audio_in = create_sock(ZMQ_PUSH);
    if( device.raw_audio_in == NULL ) {
        fprintf(stderr, "Could not create raw_audio_in socket for device %s", device.name);
        return false;
    }
    char addr[20];
    sprintf(&addr[0], "inproc://dev%2d_raw", device.id);
    zmq_bind(device.raw_audio_in, addr);

    device.raw_audio_out = create_sock(ZMQ_PULL);
    if( device.raw_audio_out == NULL ) {
        fprintf(stderr, "Could not create raw_audio_out socket for device %s", device.name);
        return false;
    }
    zmq_connect(device.raw_audio_out, addr);
}


/*
Callback for port that also communicates with broker thread.

Every time this callback gets called (on a per-device basis) we need to:

  - Check for new commands via the command socket.  This can do things like update the different
    clients we need to mix together before outputting the audio into the outputBuffer.

  - Send our inputBuffer back to the broker thread

  - Iterate over our list of clients, pulling in as much data as we need to satisfy framesPerBuffer

  - Mix that audio together, output it to outputBuffer
*/
static int pa_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData ) {
    // First, disable unused variable warnings 
    (void) statusFlags;
    (void) timeInfo;

    // Grab our audio_device, which has important things in it
    audio_device * device = (audio_device *)userData;

    

    // If we've got input data, send it out!
    if( inputBuffer != NULL )
        zmq_write(device.raw_audio_in, inputBuffer, framesPerBuffer*device.num_channels*sizeof(float), ZMQ_DONTWAIT);

    // If we're expecting output data, let's not disappoint!
    if( outputBuffer != NULL )
        device->out_buff->read(framesPerBuffer*device.num_channels, (float *)outputBuffer);

    // The show must go on
    return paContinue;
}

// Initialize Port streams for the given device
bool AudioEngine::initPort( audio_device & device ) {
    PaStreamParameters parameters;
    parameters.device = device.id;
    parameters.channelCount = device.num_channels;
    parameters.sampleFormat = paFloat32;
    parameters.suggestedLatency = Pa_GetDeviceInfo( parameters.device )->defaultLowInputLatency;
    parameters.hostApiSpecificStreamInfo = NULL;

    // Are we doing input, output, or both?
    PaStreamParameters *inparams = NULL, *outparams = NULL;
    if( device.direction != OUTPUT )
        inparams = &parameters;
    if( device.direction != INPUT )
        outparams = &parameters;

    // Actually try to open the stream
    PaError err;
    err = Pa_OpenStream( &device.stream, inparams, outparams, SAMPLE_RATE, AUDIO_BUFF_LEN, 0, &pa_callback, (void *)&device );

    if( err != paNoError ) {
        fprintf(stderr, "Could not open stream %d - %s\n", device.id, device.name);
        return false;
    }

    // Start the stream, spawning off a thread to run the callbacks from.
    err = Pa_StartStream( device.stream );
    if( err != paNoError ) {
        fprintf(stderr, "Could not start stream %d - %s\n", device.id, device.name);
        return false;
    }

    return true;
}




// Return the device ID matching this name, or -1 if not found (case-insensitive)
int getDeviceId( const char * name ) {
    int numDevices = Pa_GetDeviceCount();
    if( numDevices < 0 ) {
        fprintf( stderr, "ERROR: Pa_GetDeviceCount() returned 0x%x\n", numDevices );
        return -1;
    }

    const PaDeviceInfo * di;
    const PaDeviceInfo * di_choice;
    int choice = -1;
    for( int i=0; i<numDevices; i++ ) {
        di = Pa_GetDeviceInfo(i);
        if( strcasestr(di->name, name) != NULL ) {
            if( choice == -1 ) {
                choice = i;
                di_choice = di;
            } else {
                fprintf( stderr, "Ambiguous device name \"%s\"; choosing deivce \"%s\" (%d)\n", name, choice, di_choice->name);
                break;
            }
        }
    }

    if( choice == -1 )
        fprintf( stderr, "ERROR: Could not find device \"%s\"\n", name);
    return choice;
}


// Only return true if all of buffer is 0.0f
bool is_silence( float * buffer, unsigned int len ) {
    for( int i=0; i<len; ++i ) {
        if( buffer[i] != 0.0f )
            return false;
    }
    return true;
}
