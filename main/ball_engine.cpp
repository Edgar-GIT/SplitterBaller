// ball_engine.cpp
// Terminal Bouncing & Splitting Ball Engine
// -------------------------------------------------------------------------
// Raw C++17, no external libs (no ncurses / no graphics API).
// Renders sub-character resolution using the "upper half block" (▀) trick:
// each terminal cell encodes TWO vertical pixels by setting the foreground
// color to the top pixel and the background color to the bottom pixel.
// This roughly compensates for terminal cells being ~2x taller than wide,
// giving near-square pixels and smooth-looking circles.
//
// Build:  g++ -O2 -std=c++17 -o ball_engine ball_engine.cpp
// Run:    ./ball_engine
// Quit:   Ctrl+C
// -------------------------------------------------------------------------

#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <sys/ioctl.h>

// ----------------------------- Globals / Config ---------------------------

static std::atomic<bool>   g_running{true};
static std::atomic<bool>   g_resized{true}; // force initial size query
static int                 g_cols = 80, g_rows = 24;

constexpr int    TARGET_FPS      = 30;
constexpr double FRAME_DT        = 1.0 / TARGET_FPS;
constexpr int     MAX_BALLS       = 96;   // hard cap; pre-reserved capacity
constexpr int     INITIAL_BALLS   = 16;   // starting ball count

// Golden-angle hue step gives maximally spread, non-clumping colors as
// ball count grows (avoids random hues clustering together visually).
constexpr double HUE_GOLDEN_ANGLE = 137.50776405;

// Radii are expressed in "pixel space" (pxHeight = rows*2, pxWidth = cols).
constexpr double RADIUS_SMALL   = 3.0;
constexpr double RADIUS_MEDIUM  = 6.0;
constexpr double RADIUS_LARGE   = 12.0;

constexpr double MAGNET_RANGE_FACTOR = 6.0;   // attraction range = radius * factor
constexpr double MAGNET_ACCEL        = 55.0;  // px/s^2 pull strength
constexpr int     MERGE_COOLDOWN_FRAMES = 24; // frames before a fresh ball can merge/split again

// -------------------------------- Utility ---------------------------------

static void getTerminalSize(int &cols, int &rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0){
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
    else{
        cols = 80;
        rows = 24;
    }
}

static void onResize(int) { g_resized.store(true); }
static void onInterrupt(int) { g_running.store(false); }

struct RGB { unsigned char r, g, b; };

// Standard HSV -> RGB (h in [0,360), s,v in [0,1])
static RGB hsvToRgb(double h, double s, double v){
    double c = v * s;
    double hp = std::fmod(h, 360.0) / 60.0;
    double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    double r1 = 0, g1 = 0, b1 = 0;
    if (hp >= 0 && hp < 1)      { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2)            { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3)            { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4)            { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5)            { r1 = x; g1 = 0; b1 = c; }
    else                        { r1 = c; g1 = 0; b1 = x; }
    double m = v - c;
    return RGB{
        static_cast<unsigned char>((r1 + m) * 255.0),
        static_cast<unsigned char>((g1 + m) * 255.0),
        static_cast<unsigned char>((b1 + m) * 255.0)
    };
}

// ------------------------------- Ball model --------------------------------

enum class Size : int { SMALL = 0, MEDIUM = 1, LARGE = 2 };

static double radiusOf(Size s){
    switch (s){
        case Size::SMALL:  return RADIUS_SMALL;
        case Size::MEDIUM: return RADIUS_MEDIUM;
        case Size::LARGE:  return RADIUS_LARGE;
    }
    return RADIUS_SMALL;
}

static double massOf(Size s) { double r = radiusOf(s); return r * r; } // area-proportional mass

struct Ball {
    double x = 0, y = 0;
    double vx = 0, vy = 0;
    Size   size = Size::SMALL;
    double hue = 0.0;      // 0..360 personal color offset
    double hueSpeed = 20.0;
    double sat = 0.85;     // color saturation (varied for visual diversity)
    double val = 0.95;     // color brightness
    int    cooldown = 0;   // frames left before this ball may split/merge again
    bool   alive = true;
};

static std::mt19937 g_rng(std::random_device{}());

static double randRange(double lo, double hi){
    std::uniform_real_distribution<double> d(lo, hi);
    return d(g_rng);
}

// --------------------------- Simulation state ------------------------------

struct World{
    std::vector<Ball> balls;
    double bgHue = 0.0;

    World() { balls.reserve(MAX_BALLS + 16); } // pre-allocate: avoid heap churn on split/merge

    double pxWidth()  const { return static_cast<double>(g_cols); }
    double pxHeight() const { return static_cast<double>(g_rows) * 2.0; }

    void spawnInitial(){
        balls.clear();
        Size sizes[3] = { Size::SMALL, Size::MEDIUM, Size::LARGE };
        for (int i = 0; i < INITIAL_BALLS; ++i){
            Ball b;
            b.size = sizes[g_rng() % 3];
            double r = radiusOf(b.size);
            b.x = randRange(r, std::max(r + 1.0, pxWidth()  - r));
            b.y = randRange(r, std::max(r + 1.0, pxHeight() - r));
            double speed = randRange(18.0, 34.0);
            double ang = randRange(0, 2 * M_PI);
            b.vx = std::cos(ang) * speed;
            b.vy = std::sin(ang) * speed;
            // Golden-angle spread keeps colors distinct even with many balls;
            // small jitter avoids a too-mechanical rainbow-wheel look.
            b.hue = std::fmod(i * HUE_GOLDEN_ANGLE + randRange(-8, 8) + 3600.0, 360.0);
            b.hueSpeed = randRange(10.0, 40.0) * (g_rng() % 2 == 0 ? 1 : -1);
            b.sat = randRange(0.65, 1.0);
            b.val = randRange(0.75, 1.0);
            b.cooldown = 0;
            balls.push_back(b);
        }
    }

    // Splits one LARGE ball into `count` SMALL children bursting outward.
    void splitBall(const Ball &parent, std::vector<Ball> &outNew){
        int count = 3 + static_cast<int>(g_rng() % 3); // 3..5
        int slotsLeft = MAX_BALLS - static_cast<int>(balls.size()) - static_cast<int>(outNew.size());
        count = std::clamp(count, 0, std::max(0, slotsLeft));
        double parentSpeed = std::sqrt(parent.vx * parent.vx + parent.vy * parent.vy);
        double baseSpeed = std::max(parentSpeed, 20.0);
        for (int i = 0; i < count; ++i){
            Ball c;
            c.size = Size::SMALL;
            double spread = (2 * M_PI) * (static_cast<double>(i) / std::max(1, count));
            double jitter = randRange(-0.35, 0.35);
            double ang = spread + jitter;
            double burst = baseSpeed * randRange(0.8, 1.4);
            c.vx = std::cos(ang) * burst;
            c.vy = std::sin(ang) * burst;
            double r = radiusOf(c.size);
            c.x = std::clamp(parent.x + std::cos(ang) * r * 1.5, r, pxWidth()  - r);
            c.y = std::clamp(parent.y + std::sin(ang) * r * 1.5, r, pxHeight() - r);
            c.hue = std::fmod(parent.hue + randRange(-25, 25) + 360.0, 360.0);
            c.hueSpeed = randRange(10.0, 40.0) * (g_rng() % 2 == 0 ? 1 : -1);
            c.sat = randRange(0.65, 1.0);
            c.val = randRange(0.75, 1.0);
            c.cooldown = MERGE_COOLDOWN_FRAMES;
            outNew.push_back(c);
        }
    }

    void update(double dt){
        bgHue = std::fmod(bgHue + dt * 6.0, 360.0);

        std::vector<Ball> spawned;
        spawned.reserve(8);

        // 1) Integrate motion, handle wall bounce / large-ball splitting.
        for (auto &b : balls){
            if (!b.alive) continue;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            b.hue = std::fmod(b.hue + b.hueSpeed * dt + 360.0, 360.0);
            if (b.cooldown > 0) --b.cooldown;

            double r = radiusOf(b.size);
            bool hitWall = false;

            if (b.x - r < 0)              { b.x = r;              b.vx = std::fabs(b.vx);  hitWall = true; }
            else if (b.x + r > pxWidth()) { b.x = pxWidth()  - r; b.vx = -std::fabs(b.vx); hitWall = true; }
            if (b.y - r < 0)              { b.y = r;              b.vy = std::fabs(b.vy);  hitWall = true; }
            else if (b.y + r > pxHeight()){ b.y = pxHeight() - r; b.vy = -std::fabs(b.vy); hitWall = true; }

            if (hitWall && b.size == Size::LARGE && b.cooldown == 0) {
                splitBall(b, spawned);
                b.alive = false; // destroyed on split
            }
        }

        // 2) Proximity magnetism + merging between balls of the same size.
        for (size_t i = 0; i < balls.size(); ++i){
            Ball &a = balls[i];
            if (!a.alive || a.cooldown > 0 || a.size == Size::LARGE) continue;
            for (size_t j = i + 1; j < balls.size(); ++j) {
                Ball &b = balls[j];
                if (!b.alive || b.cooldown > 0 || b.size != a.size) continue;

                double dx = b.x - a.x, dy = b.y - a.y;
                double dist = std::sqrt(dx * dx + dy * dy);
                double rr = radiusOf(a.size) + radiusOf(b.size);
                double range = rr * MAGNET_RANGE_FACTOR;

                if (dist < 1e-6) dist = 1e-6;

                if (dist <= rr) {
                    // Merge -> next size up, momentum-conserving velocity.
                    Size newSize = (a.size == Size::SMALL) ? Size::MEDIUM : Size::LARGE;
                    double ma = massOf(a.size), mb = massOf(b.size);
                    Ball m;
                    m.size = newSize;
                    m.x = (a.x * ma + b.x * mb) / (ma + mb);
                    m.y = (a.y * ma + b.y * mb) / (ma + mb);
                    m.vx = (a.vx * ma + b.vx * mb) / (ma + mb);
                    m.vy = (a.vy * ma + b.vy * mb) / (ma + mb);
                    double nr = radiusOf(newSize);
                    m.x = std::clamp(m.x, nr, pxWidth()  - nr);
                    m.y = std::clamp(m.y, nr, pxHeight() - nr);
                    m.hue = std::fmod((a.hue + b.hue) / 2.0 + 360.0, 360.0);
                    m.hueSpeed = (a.hueSpeed + b.hueSpeed) / 2.0;
                    m.sat = (a.sat + b.sat) / 2.0;
                    m.val = (a.val + b.val) / 2.0;
                    m.cooldown = MERGE_COOLDOWN_FRAMES;
                    a.alive = false;
                    b.alive = false;
                    spawned.push_back(m);
                } else if (dist <= range) {
                    // Gentle magnetic attraction toward each other.
                    double pull = MAGNET_ACCEL * (1.0 - (dist / range));
                    double ux = dx / dist, uy = dy / dist;
                    a.vx += ux * pull * dt;
                    a.vy += uy * pull * dt;
                    b.vx -= ux * pull * dt;
                    b.vy -= uy * pull * dt;
                }
            }
        }

        // 3) Compact: drop dead balls, append newly spawned ones (capacity was pre-reserved).
        balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball &b) {return !b.alive; }), balls.end());
        for (auto &nb : spawned) {
            if (static_cast<int>(balls.size()) < MAX_BALLS) balls.push_back(nb);
        }

        // Safety net: never let the simulation go empty.
        if (balls.empty()) spawnInitial();
    }
};

// -------------------------------- Renderer ---------------------------------

struct Renderer{
    std::vector<RGB> pixels;      // pxWidth * pxHeight
    int pw = 0, ph = 0;
    std::string frame;

    void resize(int cols, int rows){
        pw = cols;
        ph = rows * 2;
        pixels.assign(static_cast<size_t>(pw) * ph, RGB{0, 0, 0});
        frame.reserve(static_cast<size_t>(cols) * rows * 40); // generous per-cell budget
    }

    void clear(RGB bg){
        std::fill(pixels.begin(), pixels.end(), bg);
    }

    inline void setPixel(int x, int y, RGB c){
        if (x < 0 || y < 0 || x >= pw || y >= ph) return;
        pixels[static_cast<size_t>(y) * pw + x] = c;
    }

    void drawBall(const Ball &b, RGB color){
        double r = radiusOf(b.size);
        int minX = static_cast<int>(std::floor(b.x - r));
        int maxX = static_cast<int>(std::ceil(b.x + r));
        int minY = static_cast<int>(std::floor(b.y - r));
        int maxY = static_cast<int>(std::ceil(b.y + r));
        minX = std::max(minX, 0); minY = std::max(minY, 0);
        maxX = std::min(maxX, pw - 1); maxY = std::min(maxY, ph - 1);
        double r2 = r * r;
        for (int y = minY; y <= maxY; ++y) {
            double dy = y - b.y;
            for (int x = minX; x <= maxX; ++x) {
                double dx = x - b.x;
                if (dx * dx + dy * dy <= r2) setPixel(x, y, color);
            }
        }
    }

    static void appendInt(std::string &s, int v){
        char buf[8];
        int n = std::snprintf(buf, sizeof(buf), "%d", v);
        s.append(buf, n);
    }

    // Build one full frame string using half-block characters, skipping
    // redundant escape codes when consecutive cells share the same colors.
    void present(){
        frame.clear();
        frame += "\033[H"; // cursor home -- avoids clearing (no flicker)

        int lastFg = -1, lastBg = -1;
        for (int row = 0; row < ph / 2; ++row) {
            for (int col = 0; col < pw; ++col) {
                const RGB &top = pixels[static_cast<size_t>(row * 2) * pw + col];
                const RGB &bot = pixels[static_cast<size_t>(row * 2 + 1) * pw + col];
                int fgKey = (top.r << 16) | (top.g << 8) | top.b;
                int bgKey = (bot.r << 16) | (bot.g << 8) | bot.b;

                if (fgKey != lastFg) {
                    frame += "\033[38;2;";
                    appendInt(frame, top.r); frame += ';';
                    appendInt(frame, top.g); frame += ';';
                    appendInt(frame, top.b); frame += 'm';
                    lastFg = fgKey;
                }
                if (bgKey != lastBg) {
                    frame += "\033[48;2;";
                    appendInt(frame, bot.r); frame += ';';
                    appendInt(frame, bot.g); frame += ';';
                    appendInt(frame, bot.b); frame += 'm';
                    lastBg = bgKey;
                }
                frame += "\xE2\x96\x80"; // UTF-8 for '▀' (upper half block)
            }
            if (row != (ph / 2) - 1) frame += '\n';
        }
        frame += "\033[0m";
        ssize_t written = write(STDOUT_FILENO, frame.data(), frame.size());
        (void)written;
    }
};

// ---------------------------------- Main ------------------------------------

int main(){
    std::signal(SIGWINCH, onResize);
    std::signal(SIGINT,   onInterrupt);
    std::signal(SIGTERM,  onInterrupt);

    // Enter alt screen, hide cursor, clear once.
    std::string setup = "\033[?1049h\033[2J\033[?25l\033[H";
    if (write(STDOUT_FILENO, setup.data(), setup.size()) < 0) return 1;

    getTerminalSize(g_cols, g_rows);
    if (g_cols < 10) g_cols = 10;
    if (g_rows < 6)  g_rows = 6;

    World world;
    Renderer renderer;
    renderer.resize(g_cols, g_rows);
    world.spawnInitial();

    using clock = std::chrono::steady_clock;
    auto lastTime = clock::now();

    while (g_running.load()){
        auto frameStart = clock::now();

        if (g_resized.exchange(false)){
            int nc, nr;
            getTerminalSize(nc, nr);
            if (nc != g_cols || nr != g_rows){
                g_cols = std::max(10, nc);
                g_rows = std::max(6, nr);
                renderer.resize(g_cols, g_rows);
                std::string clr = "\033[2J\033[H";
                if (write(STDOUT_FILENO, clr.data(), clr.size()) < 0) { /* ignore */ }
            }
        }

        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;
        dt = std::clamp(dt, 0.0, 0.05); // avoid huge steps after resize/stalls

        world.update(dt > 0 ? dt : FRAME_DT);

        RGB bg = hsvToRgb(world.bgHue, 0.55, 0.12); // dim, slowly cycling background
        renderer.clear(bg);
        for (const auto &b : world.balls) {
            if (!b.alive) continue;
            RGB c = hsvToRgb(b.hue, b.sat, b.val);
            renderer.drawBall(b, c);
        }
        renderer.present();

        auto frameEnd = clock::now();
        double elapsed = std::chrono::duration<double>(frameEnd - frameStart).count();
        double sleepSec = FRAME_DT - elapsed;
        if (sleepSec > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepSec));
        }
    }

    // Restore terminal.
    std::string teardown = "\033[0m\033[?25h\033[?1049l";
    if (write(STDOUT_FILENO, teardown.data(), teardown.size()) < 0) { /* ignore */ }
    return 0;
}
