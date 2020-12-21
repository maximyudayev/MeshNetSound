# Massively Scalable WASAN Aggregator
Teleconferencing Windows audio utility for sound quality enhancement and audio effect generation via a WASAN
for the Master of Electronics and ICT course of R&D at KU Leuven 2020-2021, case study of ConneXounds.

The utility performs aggregation of an arbitrary number of devices, each of arbitrary number of channels, and sampled each at an 
arbitrary sample rate, both hardware, software, and networked. In case of the first two, as long as they are enumerable by the OS, 
the third, as long as WiFi-Direct connection to it can be established.

The aggregator uses 2 threads for capture:
1. WASAPI device capture thread - receives packets from WASAPI in a polling or in event-driven fashion for each device
1. UDP server thread - binds and continuously monitors port 42069 for incoming packets from connected WASAN nodes

It efficiently sample rate converts all of the streams to the user chosen DSP sample rate (i.e 48000Hz), using the 
[Flexible Sample Rate Conversion algorithm](https://ccrma.stanford.edu/~jos/resample/) by Julius O. Smith, and places each stream
into the equisampled ring buffer.

The data is then available for consumption by the polyphonic DSP threads, which each apply desired
audio effects on each, one, or multiple ring buffer channels concurrently. 

After processing each batch of frames, the DSP block optionally mixes, scales, or combines channels into completely new
and then places the resulting data into the output ring buffer.

Once data is available, render thread routes each of the output ring buffer channels into playback devices according to the
channel selection, a.k.a channel mask, chosen by the user. The render thread pushes data into the playback device through the final
SRC block which works identically to the capture process above, operating in-place without copying and places filtered result directly
into the memory provided by the kernel for direct playback.

Storing original equisampled filtered data in the output ring buffer guarantees instant channel multiplexing and ability to route
same data into simultaneously multiple distinct devices.

## Feature List
- [X] **Inter-node audio transfer** - Rx/Tx of streams between WASAN nodes over WiFi-Direct
- [X] **Recording** - simultaneous recording of stream(s) according to user's request
- [ ] **Automated time alignment** - periodic TDE
- [ ] **Noise cancellation** - sound enhancement based on WASAN cumulitive captured data
- [ ] **Dynamic configuration** - GUI support to dynamically change tool's settings (buffer size, sample rate, output MUX, mixing, etc.)
- [ ] **Virtual audio device interface** - native connection to JUCE as an OS recognizable capture device
- [ ] **Virtual audio device interface** - connects to teleconference applications as a regular microphone/speaker
- [ ] **Daemon** - works as a background process
- [ ] **Internode synchronization** - "pulse" quanta for precisely timed playback across WASAN devices
- [ ] **Beamforming** - collaborative spatial targeting
- [ ] **DOA/DOD** - identification of the room layout
- [ ] **Room transfer function estimation**
- [ ] **Room reverberance simulation** - perception of presence in the space where audio is recorded (concert stadium, church, lecture hall, booth, etc.)
- [ ] **Automated setup** - negotiate optimal communication approach, parameter settings, room acoustic evaluation, TDE, etc.
- [ ] **Push-to-talk** - automatically muting participant when they are not speaking to reduce noise and confusion of participants.

System architecture vision.
![Extended functionality](https://github.com/stijn-reniers/Windows_audio_aggregator/blob/aggregator/images/Presentation%20-%20Current%20Aggregator%20Functionality.png)

## Dataflow
Picture below shows how data flows internally through the system, starting at the capture side and ending at the render side.
![Data flow description](https://github.com/stijn-reniers/Windows_audio_aggregator/blob/aggregator/images/Data%20Flow%20Description.png)

The system does all the processing in-place, without unnecessary, time-consuming, and memory occupying copy-and-move operations:
1. WASAPI or UDP server provid a pointer to a buffer where kernel wrote audio data
1. Tool performs SRC, dereferencing frames from that buffer, placing multiply-accumulate results directly into input ring buffer
1. Polyphonic DSP in the similar fashion filter and applies effects, placing results directly into output ring buffer
1. Render thread uses user-chosen channel mask and routes each channel to the associated, chosen, playback device