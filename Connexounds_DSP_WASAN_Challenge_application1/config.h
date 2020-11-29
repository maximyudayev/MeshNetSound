#pragma once

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

// In the future must be ideally made into dynamic controls or a CLI arguments
#ifndef WAV_FILE_OPEN_ATTEMPTS
    #define WAV_FILE_OPEN_ATTEMPTS 5
#endif

#ifndef ENDPOINT_BUFFER_PERIOD_MILLISEC
    #define ENDPOINT_BUFFER_PERIOD_MILLISEC 500     // buffer period of each device
#endif

#ifndef ENDPOINT_TIMEOUT_MILLISEC
    #define ENDPOINT_TIMEOUT_MILLISEC 2000          // timeout when WASAPI does not provide new packets
#endif

#ifndef AGGREGATOR_SAMPLE_FREQ
    #define AGGREGATOR_SAMPLE_FREQ 44100            // sampling frequency of circular buffer for DSP consumption
#endif

#ifndef AGGREGATOR_CIRCULAR_BUFFER_SIZE
    #define AGGREGATOR_CIRCULAR_BUFFER_SIZE 44100   // number of endpoint buffer sized packets to fit
#endif

#ifndef AGGREGATOR_OP_ATTEMPTS
    #define AGGREGATOR_OP_ATTEMPTS 5
#endif

#define DEBUG                                   // flag aiding in automatic exit after 10s

#define MSG "[MSG]: "
#define WARN "[WARN]: "
#define ERR "[ERR]: "

// libresample
/* Accuracy */
#define Npc 4096