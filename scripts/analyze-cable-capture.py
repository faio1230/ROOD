"""Check that the SRT loopback tones reached the two sides of VB-Cable.

Input is a stereo 48 kHz PCM16 WAV recorded from CABLE Output while
test-srt-loopback.ps1 plays to CABLE Input with its default routes.
"""

import argparse
import array
import math
import sys
import wave


def tone_amplitude(samples, frequency, rate):
    coefficient = 2.0 * math.cos(2.0 * math.pi * frequency / rate)
    previous = 0.0
    older = 0.0
    for sample in samples:
        value = sample / 32768.0 + coefficient * previous - older
        older, previous = previous, value
    power = previous * previous + older * older - coefficient * previous * older
    return 2.0 * math.sqrt(max(0.0, power)) / len(samples)


def rms(samples):
    return math.sqrt(sum((sample / 32768.0) ** 2 for sample in samples) / len(samples))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", help="stereo 48 kHz PCM16 recording of CABLE Output")
    args = parser.parse_args()

    first_windows = []
    second_windows = []
    with wave.open(args.wav, "rb") as recording:
        if (recording.getnchannels(), recording.getframerate(),
                recording.getsampwidth(), recording.getcomptype()) != (2, 48000, 2, "NONE"):
            parser.error("expected stereo 48 kHz uncompressed PCM16 WAV")
        for second in range(recording.getnframes() // 48000):
            samples = array.array("h")
            samples.frombytes(recording.readframes(48000))
            if sys.byteorder != "little":
                samples.byteswap()
            left = samples[0::2]
            right = samples[1::2]
            l330 = tone_amplitude(left, 330, 48000)
            r330 = tone_amplitude(right, 330, 48000)
            l440 = tone_amplitude(left, 440, 48000)
            r440 = tone_amplitude(right, 440, 48000)
            l880 = tone_amplitude(left, 880, 48000)
            r880 = tone_amplitude(right, 880, 48000)
            if (l440 >= 0.03 and r330 >= 0.04 and
                    l330 < r330 * 0.05 and r440 < l440 * 0.05):
                first_windows.append(second)
            if (l880 >= 0.04 and r880 < l880 * 0.05 and rms(right) < 0.01):
                second_windows.append(second)

    print(f"First connection: 440 Hz left + 330 Hz right in seconds {first_windows}")
    print(f"Second connection: 880 Hz left, quiet right in seconds {second_windows}")
    if len(first_windows) < 2 or len(second_windows) < 2 or max(first_windows) >= min(second_windows):
        print("Cable capture did not prove both routed connections", file=sys.stderr)
        return 1
    print("Cable capture passed: output channels and reconnect signal verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
