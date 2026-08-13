# Ball Engine - Terminal Bouncing & Splitting

![Project Example](main.png)

A high-performance particle physics engine that runs directly in the Linux terminal, inspired by the **LAVAT** project. It uses ANSI rendering tricks to achieve sub-character resolutions and vibrant colors.

## 🚀 Features

- **Sub-character Rendering**: Uses the half-block character (`▀`) to double the terminal's vertical resolution.
- **Dynamic Colors**: HSV-based color cycles using TrueColor (24-bit) ANSI escape sequences.
- **Particle Physics**:
    - Collision detection with terminal boundaries.
    - **Merging**: Two balls of the same size attract each other magnetically and merge into a larger one.
    - **Splitting**: Large balls explode into several small ones upon colliding with walls.
- **Responsive**: Automatically adjusts the simulation when the terminal window is resized.
- **Efficient**: Written in pure C++17, with no external dependencies like ncurses or graphics APIs.

## 🐧 Compatibility

This project was developed exclusively for **Linux** systems due to the use of `ioctl` for terminal control and specific ANSI escape sequences.

## 🛠️ How to Compile and Run

Make sure you have `g++` installed.

```bash
g++ -O2 -std=c++17 -o ball_engine main/ball_engine.cpp
./ball_engine
```

## 🎮 Controls

- **Ctrl+C**: Exits the simulation and restores original terminal settings.
- **Resizing**: The engine detects the `SIGWINCH` signal and adapts the rendering instantly.

## 💡 Inspiration

Inspired by the visual effect of lava lamps and the [LAVAT](https://github.com/angelofallars/lavat) project.
