
fid = fopen('C:\Users\Gebruiker\source\repos\Connexounds_DSP_WASAN_Challenge_application1\Connexounds_DSP_WASAN_Challenge_application1\MicCapture.txt', 'r');
[output, count] = fscanf(fid, '%f');
output = decimate(output,2);


plot(output);

title('Data from microphone written to file');

soundsc(output, 48000);