# ascii-thunderstorm

A thunderstorm drawn in letters, for your terminal. Written in C++ (with a Python
version of the same scene) together with Claude, an AI assistant.

What you see, over a 24-second storm that loops without a seam:

- **Clouds in perspective** - a cloud deck overhead that runs off to a hazy horizon,
  large and dark above you, small and pale in the distance.
- **Drizzle** - a few faint, short streaks, from about 1 to 8 mm/h.
- **Distant rain storms** - faint curtains of rain hanging under the far clouds,
  drifting sideways.
- **Lightning at many distances** - tall branching bolts up close, small dim ones
  near the horizon, and for the farthest only a glow inside the clouds.
- **Thunder delay** - the readout shows how far the latest flash was and counts down
  to its thunder at the speed of sound (343 m/s). Beyond 12 km it says "too far to hear".
  There is no actual sound.

## Build and run (Linux, g++ with C++17)

    g++ -O2 -std=c++17 -o thunderstorm thunderstorm.cpp
    ./thunderstorm

Options:

    ./thunderstorm --loop         keep going; the storm repeats with no seam
    ./thunderstorm --still 2.6    print the frame at 2.6 seconds as plain text
    ./thunderstorm --no-color     letters only
    ./thunderstorm --fps 60       frames per second (default 30)
    ./thunderstorm --cols 120 --rows 50

Press `q` to quit. Colour is 24-bit: most desktop terminals and SSH clients support
it, the plain Linux text console does not (use `--no-color`). It needs only the C++
standard library and normal Linux headers, and runs comfortably on a Raspberry Pi 5.

`thunderstorm.py` is the same scene as a numpy reference: `setup(cols, rows)` once,
then `frame(t)` returns brightness, colour and the three readout lines.

## Credit

The style of this scene, and a piece of its code, come from the **python.animate**
YouTube channel: https://www.youtube.com/@python.animate

That channel's ASCII animations (campfire, Moon phases, fireworks, black hole and
more) each show their source on screen. This storm follows the same shape - `setup`
once, then `frame(t)` as a pure function of time - and uses the channel's table of
letters grouped by how much ink they carry. The storm itself (clouds, rain,
lightning, thunder timing, the player) is new, but it would not exist without those
videos. This project is unofficial and not affiliated with or endorsed by the channel.

## Licence

MIT, copyright briefnotion - see `LICENSE`. This covers the code written for this
project. The table of letters grouped by ink (`LEVELS`) comes from the python.animate
channel and is not covered by this licence; see Credit above.
