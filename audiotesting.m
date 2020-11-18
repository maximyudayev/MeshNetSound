%% Read, visualize & playback microphone stream

fid = fopen('C:\Users\Gebruiker\source\repos\Connexounds_DSP_WASAN_Challenge_application1\Connexounds_DSP_WASAN_Challenge_application1\Mic capture.txt', 'r');
[output, count] = fscanf(fid, '%f');
output = output(2:2:end); % from 2-to-1 channel
plot(output);

title('Data from microphone written to file');
soundsc(output, 48000);

%% Read, visualize & playback Bluetooth stream

fid = fopen('C:\Users\Gebruiker\source\repos\Connexounds_DSP_WASAN_Challenge_application1\Connexounds_DSP_WASAN_Challenge_application1\Bluetooth capture.txt', 'r');
[output, count] = fscanf(fid, '%f');
plot(output);

title('Data from BT headset written to file');
soundsc(output, 16000);