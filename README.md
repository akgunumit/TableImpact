# TableImpact

**Turn your MacBook into a game controller.** Tap the desk to shoot. Tilt the screen to move. Terminal games powered by the hidden sensors inside every Apple Silicon MacBook.

> No Bluetooth. No external hardware. Just your laptop, a desk, and your hands.

[![macOS](https://img.shields.io/badge/macOS-Apple%20Silicon-black?logo=apple)](https://github.com/akgunumit/TableImpact)
[![C](https://img.shields.io/badge/lang-C99-blue)](https://github.com/akgunumit/TableImpact)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

More projects by UMIT at **[umit.cc](https://umit.cc)**

---

## How It Works

TableImpact reads two sensors built into Apple Silicon MacBooks (M2 and later):

| Sensor | What it does | How you use it |
|--------|-------------|----------------|
| **Accelerometer (SPU)** | Detects physical impacts on the desk surface | Tap to shoot, jump, or flap |
| **Lid Angle Sensor** | Reads the screen tilt angle (0-360 degrees) | Tilt to steer, aim, or select |

The accelerometer is accessed via the Apple SPU HID device, and the lid sensor is polled through IOHIDManager - both using raw IOKit interfaces, no private frameworks.

### Pro Tip: External Display Mode

For the best experience, **connect your MacBook to an external monitor** and use the MacBook itself as a physical joystick. Watch the game on the big screen while tilting the laptop lid and tapping the desk to play. It feels like an arcade controller.

---

## Quick Start

```bash
git clone https://github.com/akgunumit/TableImpact.git
cd TableImpact
make
sudo ./chooser
```

`sudo` is required for accelerometer access. Without it, games run in **auto-pilot demo mode**.

---

## Game Chooser

The launcher menu lets you browse and launch all games without restarting:

- **Tilt screen** to highlight a game
- **Tap desk** to launch it
- **Tilt lid to ~20 degrees** (nearly closed) during gameplay to return to the chooser

```bash
sudo ./chooser
```

---

## Games

### Space Impact

Nokia 3310-style side-scrolling shooter.

- **Tilt screen** to move ship up/down
- **Tap desk** to fire - harder taps = bigger weapons (bullets, bombs, mega bombs)
- **Rapid 4-tap combo** fires a screen-clearing laser beam
- Collect `[L]` pickups for laser ammo
- Enemy types scale with score: basic, fast, and tank

### Space Invaders

Classic Space Invaders with physical controls.

- **Tilt screen** to move cannon left/right
- **Tap desk** to fire upward
- 5 rows of animated aliens march and descend
- 4 destructible shields protect you
- Aliens speed up as you destroy them - clear all to advance

### Dino Runner

Chrome dinosaur game, but on your desk.

- **Tap desk** to jump over obstacles
- Small cacti, large cacti, and pterodactyls
- Speed increases as you survive longer
- How far can you run?

### Flappy Bird

Classic flappy bird in your terminal.

- **Tap desk** to flap
- Navigate through pipes - pipes get narrower as your score climbs
- Gravity pulls the bird down; time your taps to stay airborne

---

## Build & Run

### Requirements

- macOS on **Apple Silicon** (M2 or later)
- Root/sudo access (for SPU accelerometer)
- Xcode Command Line Tools (`xcode-select --install`)

### Build

```bash
make                    # build everything
```

### Run

```bash
sudo ./chooser          # Game launcher (recommended)
sudo ./space_impact     # Space Impact directly
sudo ./flappy_bird      # Flappy Bird directly
sudo ./space_invaders   # Space Invaders directly
sudo ./dino             # Dino Runner directly
sudo ./sensors          # Raw sensor output (debug)
sudo ./precise_sensors  # Precise sensor monitor (no calibration, 30Hz)
```

Without root, games run in auto-pilot demo mode - useful for testing the display without sensor access.

---

## Project Structure

```
TableImpact/
├── chooser.c          # Game launcher menu
├── space_impact.c     # Space Impact game
├── flappy_bird.c      # Flappy Bird game
├── space_invaders.c   # Space Invaders game
├── dino.c             # Dino Runner game
├── game_engine.h      # Shared engine (grid renderer, particles, starfield, 3D title)
├── sensors.h          # Sensor interface (accelerometer + lid angle)
├── sensors.c          # Standalone sensor debugger
└── Makefile           # Build system
```

Each game is a single `.c` file. Headers are header-only - no separate compilation units. Games share the engine but compile independently.

---

## How the Sensors Work

- **Accelerometer**: The Apple SPU exposes a HID device (`PrimaryUsagePage=0xFF00, PrimaryUsage=3`). TableImpact reads 22-byte input reports via `IOHIDDeviceRegisterInputReportCallback`, extracts int32 LE values at byte offsets 6/10/14, and divides by 65536 to get g-force. Impact detection compares the magnitude of deviation from a calibrated baseline against a threshold.

- **Lid angle**: Polled per frame via `IOHIDDeviceGetValue` on the lid sensor device (`UsagePage=0x0020, Usage=0x008A`). Returns degrees 0-360.

Both sensors require root access to open the underlying IOKit HID devices.

---

## Credits

By **UMIT** - [umit.cc](https://umit.cc) - [@akgunumit](https://github.com/akgunumit)

