#ifndef BALL_ENGINE_HPP
#define BALL_ENGINE_HPP

#include <vector>
#include <string>
#include <atomic>

// Estrutura de cor RGB
struct RGB { unsigned char r, g, b; };

// Tipos de tamanho de bola suportados
enum class Size : int { SMALL = 0, MEDIUM = 1, LARGE = 2 };

// Estrutura que representa uma bola individual na simulação
struct Ball {
    double x = 0, y = 0;      // Posição no espaço de pixels
    double vx = 0, vy = 0;    // Vetores de velocidade
    Size   size = Size::SMALL;
    double hue = 0.0;         // Matiz (0-360) para cor dinâmica
    double hueSpeed = 20.0;
    double sat = 0.85;        // Saturação
    double val = 0.95;        // Brilho
    int    cooldown = 0;      // Frames de espera para fusão/divisão
    bool   alive = true;      // Status de existência da bola
};

#endif // BALL_ENGINE_HPP
