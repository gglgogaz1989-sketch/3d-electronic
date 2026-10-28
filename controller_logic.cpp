// C++ programmable controller: example logic.
// If D1 is high, D0 will be high. D2 is blink signal.
extern "C" __declspec(dllexport)
void controller_tick(const int* input, int* output, double seconds) {
    output[0] = input[1] ? 1 : 0;
    output[1] = 0;                            // can be used as LOW
    output[2] = int(seconds) % 2 == 0;
    output[3] = input[3] && input[1];
}
