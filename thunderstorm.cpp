// A thunderstorm of letters. Light arrives at once; thunder crawls at 343 m/s.
// An original scene with a terminal player, in the style of the python.animate
// YouTube channel (https://www.youtube.com/@python.animate): setup(cols, rows)
// once, then frame(t) as a pure function of time, drawn with letters grouped by
// how much ink they carry. That letter table (LEVELS) is the channel's.
// The storm repeats every DURATION seconds, so --loop is seamless.
//
//   g++ -O2 -std=c++17 -o thunderstorm thunderstorm.cpp
//   ./thunderstorm              play once (24 s); q or Ctrl-C quits
//   ./thunderstorm --loop       keep going
//   ./thunderstorm --still 2.6  print the frame at t=2.6 s as plain text and exit
//   ./thunderstorm --no-color   letters only
//   ./thunderstorm --fps N      frames per second (default 30)
//   ./thunderstorm --cols N --rows N

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <random>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------- the scene

static const double DURATION = 24.0;
static int          FPS      = 30;
static const int    ACCENT[3] = {120, 160, 255};   // status stripe

// letters grouped by how much ink they carry, dark -> bright
static const std::vector<std::string> LEVELS = {
    " ", ".`'", ":;,-~", "=+*^", "ijltrxz/\\|", "oecnuv()", "0O8B&%", "#@$QW"};

static const double SOUND = 0.343;        // speed of sound, km/s
struct Strike { double t, dist, across; };
// Lightning strikes: time s, distance km, across the screen 0..1.
// Near ones are tall bolts; far ones are small and dim; beyond FAR_KM there is
// no bolt at all, only a glow inside the clouds (and you cannot hear it).
static const Strike STRIKES[] = {{2.6, 2.4, 0.30}, {4.6, 10.5, 0.86}, {6.9, 5.8, 0.68},
                                 {7.6, 13.0, 0.20}, {10.4, 9.0, 0.52}, {12.8, 1.8, 0.45},
                                 {15.1, 12.0, 0.08}, {17.2, 6.5, 0.76}, {20.6, 3.5, 0.20},
                                 {22.3, 11.0, 0.62}};
static const int NSTRIKES = sizeof(STRIKES) / sizeof(STRIKES[0]);
static const double NEAR_KM = 1.5, FAR_KM = 12.0;
// distant rain storms hanging under the far clouds: across 0..1, width 0..1, strength
struct Storm { double across, width, strength; };
static const Storm STORMS[] = {{0.14, 0.07, 0.9}, {0.52, 0.10, 0.7}, {0.84, 0.06, 1.0}};
static const int NSTORMS = sizeof(STORMS) / sizeof(STORMS[0]);
static const double DROPS = 0.03;         // raindrops per letter cell: a drizzle
static const int HUD_ROWS = 3;

struct BoltCell { int idx; double w; };
struct Bolt {
    std::vector<BoltCell> cells;
    double cx = 0, cy = 0, sx = 1, sy = 1, f = 0;     // f: 0 near .. 1 far
};

struct Scene {
    int cols = 0, rows = 0;
    int horizon = 0, rtop = 0, sky = 0;
    std::vector<double> ridge;                        // hill line, per column
    std::vector<Bolt> bolts;                          // per strike
    std::vector<double> x0, sp, ph, thr;              // per drop
    std::vector<int> ln;
} S;

struct Frame {
    std::vector<double> lum;                          // rows*cols
    std::vector<double> rgb;                          // rows*cols*3
    std::string hud[3];
};

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline double smooth(double v) {
    v = clampd(v, 0.0, 1.0);
    return v * v * (3 - 2 * v);
}

static inline double pymod(double a, double m) {
    double r = std::fmod(a, m);
    if (r < 0) r += m;
    return r;
}

// colour: night blue -> storm blue -> flash white
static void palette(double lum, double out[3]) {
    static const double xs[4] = {0.0, 0.3, 0.7, 1.0};
    static const double cs[4][3] = {{6, 8, 20}, {40, 55, 100},
                                    {150, 170, 230}, {250, 250, 255}};
    lum = clampd(lum, 0.0, 1.0);
    int k = 0;
    while (k < 2 && lum > xs[k + 1]) k++;
    double f = (lum - xs[k]) / (xs[k + 1] - xs[k]);
    for (int c = 0; c < 3; c++) out[c] = cs[k][c] + f * (cs[k + 1][c] - cs[k][c]);
}

static inline double hash2(double x, double y) {
    return pymod(std::sin(x * 12.9898 + y * 78.233) * 43758.5453, 1.0);
}

// smooth value noise
static double vnoise(double x, double y) {
    double ix = std::floor(x), iy = std::floor(y);
    double fx = smooth(x - ix), fy = smooth(y - iy);
    double a = hash2(ix, iy), b = hash2(ix + 1, iy);
    double c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

static double fbm(double x, double y) {
    return 0.55 * vnoise(x, y) + 0.30 * vnoise(2 * x + 5.2, 2 * y + 1.3)
         + 0.15 * vnoise(4 * x + 9.1, 4 * y + 3.7);
}

// setup: the hills, every bolt and every raindrop, fixed once
static void setup(int cols, int rows) {
    S = Scene();
    S.cols = cols;
    S.rows = rows;
    std::mt19937_64 rng(7);
    auto uni = [&](double a, double b) {
        return std::uniform_real_distribution<double>(a, b)(rng);
    };
    S.horizon = (int)(rows * 0.84);
    S.rtop = (int)(rows * 0.24);                       // where the nearest bolts leave the cloud
    S.sky = S.horizon - 2;                             // the horizon line the clouds converge to
    S.ridge.resize(cols);
    for (int j = 0; j < cols; j++)
        S.ridge[j] = S.horizon + 1.6 * std::sin(j * 0.09 + 0.5) + 1.0 * std::sin(j * 0.23 + 2.0)
                   + 1.2 * hash2(j, 3.0);

    std::discrete_distribution<int> jag({5, 20, 50, 20, 5});     // -2 .. +2
    for (int s = 0; s < NSTRIKES; s++) {
        double dist = STRIKES[s].dist;
        double f = clampd((dist - NEAR_KM) / (FAR_KM - NEAR_KM - 1), 0, 1);   // 0 near .. 1 far
        std::vector<double> g((size_t)rows * cols, 0.0);
        auto put = [&](int r, int c, double w) {
            if (r >= 0 && r < rows && c >= 0 && c < cols)
                g[(size_t)r * cols + c] = std::max(g[(size_t)r * cols + c], w);
        };
        int x = (int)(STRIKES[s].across * cols);
        int end = (int)S.ridge[std::min(std::max(x, 0), cols - 1)];
        int tp = (int)(S.sky - (S.sky - S.rtop) * (1 - 0.85 * f));    // far bolts: short, near the horizon
        if (dist < FAR_KM) {
            for (int r = tp; r < end; r++) {
                int nx = x + jag(rng) - 2;
                for (int c = std::min(x, nx); c <= std::max(x, nx); c++) put(r, c, 1.0);
                x = nx;
                if (tp + 3 < r && r < end - 4 && uni(0, 1) < 0.10) {
                    int d = uni(0, 1) < 0.5 ? -1 : 1;
                    int ln = 4 + (int)uni(0, 8);
                    int bx = x, br = r;
                    for (int k = 0; k < ln; k++) {
                        if (uni(0, 1) < 0.7) br++;
                        bx += d;
                        put(br, bx, 0.55 * (1 - (double)k / ln));
                    }
                }
            }
        }
        if (f < 0.5) {                                  // only near bolts have a soft glow
            std::vector<double> base = g;
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++) {
                    double w = base[(size_t)r * cols + c];
                    if (w <= 0) continue;
                    for (int off = 1; off <= 2; off++)
                        for (int sg = -1; sg <= 1; sg += 2)
                            put(r, c + sg * off, (off == 1 ? 0.25 : 0.10) * w);
                }
        }
        Bolt bolt;
        for (size_t p = 0; p < g.size(); p++)
            if (g[p] > 0) bolt.cells.push_back({(int)p, g[p]});
        bolt.cx = STRIKES[s].across * cols;
        bolt.cy = tp + 0.35 * (end - tp);
        bolt.sx = 0.2 * cols * (1 - 0.6 * f);
        bolt.sy = std::max(0.6 * (end - tp), 4.0);
        bolt.f = f;
        S.bolts.push_back(bolt);
    }

    int n = (int)(cols * rows * DROPS);
    S.x0.resize(n); S.sp.resize(n); S.ph.resize(n); S.thr.resize(n); S.ln.resize(n);
    for (double& v : S.x0) v = uni(0, cols + rows * 0.5);
    for (double& v : S.sp) v = uni(18, 30);
    for (double& v : S.ph) v = uni(0, 1);
    for (double& v : S.thr) v = uni(0, 1);
    for (int& v : S.ln) v = 1 + (int)uni(0, 3);
}

// a return stroke, a second one, and a weak third (lightning flickers)
static double envelope(double age) {
    double e = std::exp(-age / 0.05);
    if (age >= 0.13) e += 0.8 * std::exp(-(age - 0.13) / 0.05);
    if (age >= 0.27) e += 0.5 * std::exp(-(age - 0.27) / 0.07);
    return e;
}

// one frame: a pure function of time, no state between frames
static void frame(double t, Frame& F) {
    int R = S.rows, C = S.cols;
    double tc = pymod(t, DURATION);
    double inten = 0.475 + 0.225 * std::sin(2 * M_PI * t / DURATION - 1.2);   // 0.25 .. 0.70
    double mmh = 1 + 7 * (inten - 0.25) / 0.45;

    // lightning: far flashes only glow inside the clouds, near ones light the whole sky
    double amb = 0.0;
    struct Loc { const Bolt* b; double ke; };
    std::vector<Loc> locs;
    std::vector<double> bolt((size_t)R * C, 0.0);
    const double Ts[2] = {tc, tc + DURATION};            // a strike from the end can spill over
    for (double T : Ts)
        for (int s = 0; s < NSTRIKES; s++) {
            double age = T - STRIKES[s].t;
            if (!(age >= 0 && age < 1.2)) continue;
            const Bolt& b = S.bolts[s];
            double e = envelope(age);
            double k = clampd(2.4 / (1 + 0.5 * STRIKES[s].dist), 0.12, 1.3);   // far flashes are dimmer
            amb += k * (1 - 0.65 * b.f) * (0.6 * std::min(e, 1.4) + 0.4 * std::exp(-age / 0.35));
            locs.push_back({&b, k * e});
            if (age < 0.5 && !b.cells.empty()) {
                double I = (0.6 + 0.4 * std::min(e, 1.0)) * smooth((0.5 - age) / 0.12) * (1 - 0.5 * b.f);
                for (const BoltCell& bc : b.cells)
                    bolt[bc.idx] = std::max(bolt[bc.idx], bc.w * I);
            }
        }
    amb = std::min(amb, 1.6);

    // distant rain storms: where each one is this frame
    double storm_cx[NSTORMS];
    for (int q = 0; q < NSTORMS; q++) storm_cx[q] = pymod(STORMS[q].across * C + 0.5 * t, (double)C);

    F.lum.assign((size_t)R * C, 0.0);
    F.rgb.assign((size_t)R * C * 3, 0.0);
    for (int i = 0; i < R; i++) {
        // clouds seen from below, in perspective: a flat layer above us that runs
        // off to the horizon, so the pattern shrinks and bunches up with distance
        double d = std::max(S.sky - i, 0) + 1.2;         // rows above the horizon line
        double Z = (R * 0.9) / d;                        // how far away that row of cloud is
        double far = smooth((Z - 3.0) / 12.0);           // 0 overhead .. 1 at the horizon
        for (int j = 0; j < C; j++) {
            double X = (j - C / 2.0) / (C * 0.5) * Z;    // sideways position on the cloud layer
            double n = fbm(X * 1.6 + 0.06 * t, Z * 1.4 + 0.015 * t);
            double dens = clampd((n - 0.22) * 2.2, 0, 1);
            double loc = 0;
            for (const Loc& L : locs) {
                double qx = (j - L.b->cx) / L.b->sx, qy = (i - L.b->cy) / L.b->sy;
                loc += L.ke * std::exp(-(qx * qx + qy * qy));
            }
            loc = std::min(loc, 1.5);
            double lum = 0.012 + 0.17 * far * (0.6 + 0.4 * (1 - dens)) + dens * (0.27 - 0.14 * far)
                       + 0.05 * smooth((i - 0.55 * R) / (0.3 * R))
                       + dens * (0.40 * amb + 0.9 * loc) + (1 - dens) * 0.08 * amb * (1 - (double)i / R);

            // distant rain storms: faint curtains hanging under the far clouds
            for (int q = 0; q < NSTORMS; q++) {
                double dx = std::fabs(j - storm_cx[q]);
                dx = std::min(dx, C - dx);
                double px = std::exp(-std::pow(dx / (STORMS[q].width * C), 2));
                if (px < 1e-3) continue;
                double v = smooth((i - (S.sky - 8)) / 3.0) * (i < S.ridge[j] ? 1.0 : 0.0);
                if (v <= 0) continue;
                double st = vnoise(j * 1.1 - 0.25 * i, 0.9 * t + STORMS[q].across * 50);
                lum += 0.20 * STORMS[q].strength * px * v * (0.4 + 0.8 * st) * (1 + 1.2 * amb);
            }

            // the hills are a dark shape against the sky, lit only a little
            if (i >= S.ridge[j]) lum = 0.004 + 0.07 * amb;
            F.lum[(size_t)i * C + j] = lum;
        }
    }

    // drizzle: a few faint short streaks drifting down
    int rtop = S.rtop, hz = S.horizon;
    double span = hz - rtop;
    double slope = 0.20 + 0.08 * std::sin(0.4 * t);
    int nd = (int)S.x0.size();
    for (int dd = 0; dd < nd; dd++) {
        if (!(S.thr[dd] < inten)) continue;
        double pos = std::fmod(S.ph[dd] * span + S.sp[dd] * t, span);
        for (int k = 0; k < 3 && k < S.ln[dd]; k++) {
            double row = rtop + pos - k;
            int col = (int)std::nearbyint(S.x0[dd] - slope * (row - rtop));
            int r = (int)std::nearbyint(row);
            if (r < rtop || r >= R || col < 0 || col >= C) continue;
            if (!(r < S.ridge[col])) continue;
            double w = (0.30 - 0.09 * k) * smooth((row - rtop) / 4.0) * (1 + 0.35 * std::min(amb, 1.0));
            double& cell = F.lum[(size_t)r * C + col];
            cell = std::max(cell, w);
        }
    }

    double fade = 0.5 + 0.5 * smooth(t / 1.0);                          // fade in
    for (size_t p = 0; p < F.lum.size(); p++) {
        double v = std::max(F.lum[p], bolt[p]);
        F.lum[p] = clampd(v * fade, 0, 1);
        palette(F.lum[p], &F.rgb[p * 3]);
    }

    // which flash does the readout talk about: the latest one so far
    double bt = -1e9, bd = 0;
    for (int s = 0; s < NSTRIKES; s++)
        for (int kk = -1; kk <= 0; kk++) {
            double ts = STRIKES[s].t + 24 * kk;
            if (ts <= tc && ts > bt) { bt = ts; bd = STRIKES[s].dist; }
        }
    double wait = bd / SOUND - (tc - bt);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s %3.1f mm/h", mmh < 3 ? "drizzle" : "light rain", mmh);
    F.hud[0] = buf;
    std::snprintf(buf, sizeof buf, "flash %4.1f km away", bd);
    F.hud[1] = buf;
    if (bd >= FAR_KM) std::snprintf(buf, sizeof buf, "too far to hear");
    else if (wait > 0) std::snprintf(buf, sizeof buf, "thunder in %4.1f s", wait);
    else std::snprintf(buf, sizeof buf, "%s", wait > -1.5 ? "thunder!" : "quiet");
    F.hud[2] = buf;
}

// ---------------------------------------------------------------- the player

// level picks the group of letters; a hash of position and time picks one
static char pick_char(double lum, int i, int j, double t) {
    int n = (int)LEVELS.size();
    int lvl = std::min(std::max((int)(lum * n), 0), n - 1);
    const std::string& g = LEVELS[lvl];
    uint32_t h = (uint32_t)i * 73856093u ^ (uint32_t)j * 19349663u ^ (uint32_t)(long)(t * 12) * 83492791u;
    return g[h % g.size()];
}

static std::string render(const Frame& F, double t, bool color, bool erase = true) {
    int R = S.rows, C = S.cols;
    std::string out;
    out.reserve((size_t)R * C * (color ? 12 : 2));
    char esc[40];
    for (int i = 0; i < R; i++) {
        int last[3] = {-1, -1, -1};
        const std::string* hud = i < HUD_ROWS ? &F.hud[i] : nullptr;
        if (hud && color) out += "\x1b[38;2;170;170;170m";
        for (int j = 0; j < C; j++) {
            if (hud && j < (int)hud->size()) { out += (*hud)[j]; continue; }
            if (hud && j == (int)hud->size()) last[0] = -1;     // force colour resync
            char ch = pick_char(F.lum[(size_t)i * C + j], i, j, t);
            if (ch == ' ') { out += ' '; continue; }
            if (color) {
                int px[3];
                for (int k = 0; k < 3; k++)
                    px[k] = (int)clampd(F.rgb[((size_t)i * C + j) * 3 + k], 0, 255);
                if (px[0] != last[0] || px[1] != last[1] || px[2] != last[2]) {
                    std::snprintf(esc, sizeof esc, "\x1b[38;2;%d;%d;%dm", px[0], px[1], px[2]);
                    out += esc;
                    std::memcpy(last, px, sizeof last);
                }
            }
            out += ch;
        }
        if (color) out += "\x1b[0m";
        if (erase) out += "\x1b[K";
        out += "\n";
    }
    return out;
}

static std::string status(int cols, double t, bool color, bool loop) {
    char right[64];
    if (loop)
        std::snprintf(right, sizeof right, "running  %5.1fs  %d fps", t, FPS);
    else
        std::snprintf(right, sizeof right, "running  %4.1fs / %.0fs  %d fps", t, DURATION, FPS);
    std::string left = "thunderstorm (C++)";
    int pad = std::max(1, cols - (int)left.size() - (int)std::strlen(right));
    std::string line = (left + std::string(pad, ' ') + right).substr(0, cols);
    if (!color) return line;
    char head[48];
    std::snprintf(head, sizeof head, "\x1b[7;38;2;%d;%d;%dm", ACCENT[0], ACCENT[1], ACCENT[2]);
    return std::string(head) + line + "\x1b[0m";
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

static void write_all(const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::write(STDOUT_FILENO, p, left);
        if (n <= 0) break;
        p += n;
        left -= (size_t)n;
    }
}

static void play(bool loop, bool color, int cols, int rows) {
    termios old{};
    bool tty_in = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    write_all("\x1b[?1049h\x1b[?25l\x1b[2J");
    if (tty_in) {
        termios raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSADRAIN, &raw);
    }

    setup(cols, rows - 1);
    using clk = std::chrono::steady_clock;
    auto start = clk::now();
    const double dt = 1.0 / FPS;
    long n = 0;
    Frame F;
    while (!g_stop) {
        double t = std::chrono::duration<double>(clk::now() - start).count();
        // With --loop the clock just keeps running: the storm repeats every
        // DURATION seconds with no seam, and the clouds keep drifting.
        if (t >= DURATION && !loop) break;
        frame(t, F);
        write_all("\x1b[H" + render(F, t, color) + status(cols, t, color, loop));
        n++;
        double wait = (n * dt) - std::chrono::duration<double>(clk::now() - start).count();
        if (wait < -1.0)                       // fell far behind: resync
            n = (long)(std::chrono::duration<double>(clk::now() - start).count() / dt);
        if (tty_in) {
            pollfd pfd{STDIN_FILENO, POLLIN, 0};
            int ms = wait > 0 ? (int)(wait * 1000) : 0;
            if (poll(&pfd, 1, ms) > 0) {
                char ch;
                while (read(STDIN_FILENO, &ch, 1) == 1)
                    if (ch == 'q' || ch == 'Q') g_stop = 1;
            }
        } else if (wait > 0) {
            usleep((useconds_t)(wait * 1e6));
        }
    }

    write_all("\x1b[0m\x1b[?25h\x1b[?1049l");
    if (tty_in) tcsetattr(STDIN_FILENO, TCSADRAIN, &old);
}

static void usage(const char* me) {
    std::fprintf(stderr,
        "usage: %s [--loop] [--no-color] [--still T] [--fps N] [--cols N] [--rows N]\n", me);
}

int main(int argc, char** argv) {
    bool loop = false, color = true, still = false;
    double still_t = 0;
    int cols = 0, rows = 0;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--loop") loop = true;
        else if (a == "--no-color") color = false;
        else if (a == "--still" && i + 1 < argc) { still = true; still_t = std::atof(argv[++i]); }
        else if (a == "--fps" && i + 1 < argc) FPS = std::min(std::max(std::atoi(argv[++i]), 1), 240);
        else if (a == "--cols" && i + 1 < argc) cols = std::atoi(argv[++i]);
        else if (a == "--rows" && i + 1 < argc) rows = std::atoi(argv[++i]);
        else { usage(argv[0]); return a == "--help" || a == "-h" ? 0 : 2; }
    }
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) { ws.ws_col = 100; ws.ws_row = 40; }
    if (!cols) cols = ws.ws_col;
    if (!rows) rows = ws.ws_row;
    if (cols < 40 || rows < 20) {
        std::fprintf(stderr, "terminal too small (%dx%d); need at least 40x20\n", cols, rows);
        return 1;
    }

    if (still) {
        setup(cols, rows - 1);
        Frame F;
        frame(still_t, F);
        std::string s = render(F, still_t, false, false);
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }
    play(loop, color, cols, rows);
    return 0;
}
