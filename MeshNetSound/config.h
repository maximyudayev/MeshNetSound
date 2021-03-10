#pragma once
//-------- Helper functions
#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

//-------- Misc Macros
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

//-------- Endpoint Macros
// In the future must be ideally made into dynamic controls or CLI arguments
#ifndef WAV_FILE_OPEN_ATTEMPTS
    #define WAV_FILE_OPEN_ATTEMPTS 5
#endif

#ifndef ENDPOINT_BUFFER_PERIOD_MILLISEC
    #define ENDPOINT_BUFFER_PERIOD_MILLISEC 500     // buffer period of each device
#endif

#ifndef ENDPOINT_TIMEOUT_MILLISEC
    #define ENDPOINT_TIMEOUT_MILLISEC 2000          // timeout when WASAPI does not provide new packets
#endif

//-------- Aggregator Macros
#define AGGREGATOR_RENDER 0
#define AGGREGATOR_CAPTURE 1
#define AGGREGATOR_CIN_IP_LEN 16                    // standard in stream buffer length for IP address
#define AGGREGATOR_CIN_DEVICEID_LEN 11              // standard in stream buffer length for device ID

#ifndef AGGREGATOR_SAMPLE_FREQ
    #define AGGREGATOR_SAMPLE_FREQ 44100            // sampling frequency of circular buffer for DSP consumption
#endif

#ifndef AGGREGATOR_CIRCULAR_BUFFER_SIZE
    #define AGGREGATOR_CIRCULAR_BUFFER_SIZE 44100   // number of endpoint buffer sized packets to fit
#endif

#ifndef AGGREGATOR_OP_ATTEMPTS
    #define AGGREGATOR_OP_ATTEMPTS 5
#endif

#ifndef AUDIOEFFECT_OUTPUT_BUFFER_SIZE
    #define AUDIOEFFECT_OUTPUT_BUFFER_SIZE 2048
#endif
//-------- Resampler Macros
#define RESAMPLER_IZERO_EPSILON 1E-21               // Max error acceptable in Izero 
#define RESAMPLER_ROLLOFF_FREQ 0.9                  //  
#define RESAMPLER_BETA 4.538                        //
#define RESAMPLER_L_TWOS_EXP 12			            // (2^(1+Nc/2)) according to author's conference papaer: 
                                                    // Nc - bits to represent filter coefficients to insure
                                                    // interpolation error to be twice smaller than the 
                                                    // table-entry quantization error. We use floating point hence it is not
                                                    // straight-forwardly applicable as precision changes as number is increased.
#define RESAMPLER_N_QUALITY_L 13		            // Audio quality parameter
#define RESAMPLER_N_QUALITY_H 35		            // Some freaky alien quality

//-------- Debug Macros
#define DEBUG

#define GREEN(text) "\033[32m" text "\033[0m"

#define MSG "[MSG]: "
#define SUC "\033[32m[SUC]: "
#define WRN "\033[33m[WRN]: "
#define ERR "\033[31m[ERR]: "
#define END "\033[0m"
#define TAB "       "

//-------- Temporary Macros for WiFi-related stuff that must be replaced with proper code/negotiation
#define TEMP_AGGREGATOR_SAMPLE_PER_SEC 44100
#define TEMP_AGGREGATOR_CHANNELS 2
#define TEMP_AGGREGATOR_BLOCK_ALIGN 8
#define TEMP_AGGREGATOR_BIT_PER_SAMPLE 32
#define TEMP_AGGREGATOR_CB_SIZE 22
#define TEMP_AGGREGATOR_FORMAT_TAG WAVE_FORMAT_EXTENSIBLE
#define TEMP_AGGREGATOR_AVG_BYTE_PER_SEC 352800
#define TEMP_AGGREGATOR_SUBFORMAT KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
#define TEMP_AGGREGATOR_CHANNEL_MASK (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
#define TEMP_AGGREGATOR_VALID_BIT_PER_SAMPLE 32
#define TEMP_AGGREGATOR_ENDPOINT_BUF_SIZE 448
#define TEMP_UDP_BUFFER_SIZE 4096

//-------- UDP Macros
#define UDP_RCV_PORT 42069

//-------- Error Macros
#define ERR_OK 0
#define ERR_UDP_SOCKET_CREATE 1

//-------- Type Definitions
typedef double DOUBLE;