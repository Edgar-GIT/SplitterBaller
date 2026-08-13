#ifndef BALL_ENGINE_HPP
#define BALL_ENGINE_HPP

#include <vector>
#include <string>
#include <atomic>

// RGB color structure
struct RGB { unsigned char r, g, b; };

// Supported ball size types
enum class Size : int { SMALL = 0, MEDIUM = 1, LARGE = 2 };

// Structure representing an individual ball in the simulation
struct Ball {
    double x = 0, y = 0;      // Position in pixel space
    double vx = 0, vy = 0;    // Velocity vectors
    Size   size = Size::SMALL;
    double hue = 0.0;         // Hue (0-360) for dynamic color
    double hueSpeed = 20.0;   // Rotation speed of the hue
    double sat = 0.85;        // Saturation
    double val = 0.95;        // Brightness/Value
    int    cooldown = 0;      // Frame wait time for merging/splitting
    bool   alive = true;      // Existence status of the ball
};

#endif // BALL_ENGINE_HPP
