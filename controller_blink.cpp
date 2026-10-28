// Example firmware: compile from inside Electric3D with MinGW-w64 g++
// or MSVC cl.exe installed and available in PATH.
// Pins D0..D3 become digital outputs when the board powers VCC/GND.
extern "C" __declspec(dllexport)
void controller_tick(const int* input, int* output, double seconds) {
    output[0] = (int(seconds * 2) % 2) == 0;  // LED blink 2 Hz state changes
    output[1] = input[1];                    // copy D1 as output
    output[2] = 0;
    output[3] = 0;
}
