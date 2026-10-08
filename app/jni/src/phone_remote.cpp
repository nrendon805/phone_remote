// phone_remote.cpp
// Wireless WiFi remote for the Gasoline Soundboard PC app, with category
// browsing (PC folder structure = submenus). Built for Cxxdroid.
//
// BEFORE BUILDING:
//   1. Make sure "SDL2" is installed via Cxxdroid's package manager.
//   2. Easiest path: start from Cxxdroid's built-in SDL2 example project,
//      replace main.cpp's contents with this whole file.
//   3. Edit SERVER_IP below to your PC's LAN IP (run "ipconfig" on the PC).
//   4. Allow soundboard.exe through Windows Firewall on the PC.
//
// Protocol (matches soundboard.cpp on the PC):
//   "CATS"                          -> "<count>\n<cat0>\n<cat1>\n...\n"
//   "LIST <catIndex>"                -> "<count>\n<sound0>\n<sound1>\n...\n"
//   "PLAY <catIndex> <soundIndex>"   -> plays it, no reply expected
//
// UI: category list screen (scrollable) -> tap a category -> scrollable
// sound list for that category, with a "< Back" button to return.

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// ---- Configuration ---------------------------------------------------------
// Used only the very first time the app runs, before any IP has been saved
// via the in-app Settings screen.
static const char* DEFAULT_SERVER_IP = "192.168.1.10";
static const int   SERVER_PORT = 5051;
static const Uint32 AUTO_REFRESH_MS = 5000;

static const int ROW_H       = 135; // 1.5x taller
static const int ROW_PAD     = 12;
static const int SIDE_MARGIN = 16;
static const int TOP_BAR_H   = 220;
static const int TOP_BTN_SIZE = 150; // square: width == height, for easy tapping
static const int TOP_BTN_GAP  = 10;

// Sound/category button text only - doesn't affect Refresh/Back/Pause labels.
static const int SOUND_TEXT_BOLDNESS = 8;       // stroke thickness in pixels
static const int SOUND_TEXT_LETTER_SPACING = 10; // pixel gap between letters

// ---- Globals ----------------------------------------------------------------
struct Item {
    std::string name;
    Uint8 r = 255, g = 255, b = 255;
};

static int g_sock = -1;
static std::vector<Item> g_categories;
static std::vector<Item> g_sounds;
static int g_currentCategory = -1; // -1 = viewing category list
static std::string g_status = "Connecting...";
static Uint32 g_lastRefreshTicks = 0;

// The PC's LAN IP - editable from the in-app Settings screen, persisted to
// disk so it survives app restarts.
static std::string g_serverIp = DEFAULT_SERVER_IP;

enum Screen { SCREEN_LIST, SCREEN_SETTINGS };
static Screen g_screen = SCREEN_LIST;
static std::string g_ipEditBuffer; // working copy edited on the Settings screen

// ---- Settings persistence ----------------------------------------------------

static std::string GetConfigFilePath() {
    char* prefPath = SDL_GetPrefPath("GasolineSoundboard", "PhoneRemote");
    if (!prefPath) return "";
    std::string path = std::string(prefPath) + "server_ip.txt";
    SDL_free(prefPath);
    return path;
}

static void LoadSavedServerIp() {
    std::string path = GetConfigFilePath();
    if (path.empty()) return;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char buf[64] = { 0 };
    if (fgets(buf, sizeof(buf), f)) {
        std::string ip(buf);
        while (!ip.empty() && (ip.back() == '\n' || ip.back() == '\r' || ip.back() == ' ')) {
            ip.pop_back();
        }
        if (!ip.empty()) g_serverIp = ip;
    }
    fclose(f);
}

static void SaveServerIp(const std::string& ip) {
    std::string path = GetConfigFilePath();
    if (path.empty()) return;
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fputs(ip.c_str(), f);
    fclose(f);
}

// Parse "#RRGGBB" (or "RRGGBB") into RGB bytes; defaults to white on failure.
static void ParseHexColor(const std::string& hex, Uint8& r, Uint8& g, Uint8& b) {
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() != 6) { r = g = b = 255; return; }
    r = (Uint8)strtol(h.substr(0, 2).c_str(), nullptr, 16);
    g = (Uint8)strtol(h.substr(2, 2).c_str(), nullptr, 16);
    b = (Uint8)strtol(h.substr(4, 2).c_str(), nullptr, 16);
}

static std::string ByteToHex(Uint8 v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s;
    s += digits[(v >> 4) & 0xF];
    s += digits[v & 0xF];
    return s;
}

// ---- Tiny built-in "stick" font (no external library needed) --------------
struct Seg { float x1, y1, x2, y2; };

static std::vector<Seg> GlyphSegs(char ch) {
    char c = (char)toupper((unsigned char)ch);
    switch (c) {
        case 'A': return { {0,4,0,1},{0,1,1,0},{1,0,2,1},{2,1,2,4},{0,2,2,2} };
        case 'B': return { {0,0,0,4},{0,0,2,0},{2,0,2,2},{0,2,2,2},{2,2,2,4},{0,4,2,4} };
        case 'C': return { {2,0,0,0},{0,0,0,4},{0,4,2,4} };
        case 'D': return { {0,0,0,4},{0,0,2,0},{2,0,2,4},{0,4,2,4} };
        case 'E': return { {0,0,0,4},{0,0,2,0},{0,2,2,2},{0,4,2,4} };
        case 'F': return { {0,0,0,4},{0,0,2,0},{0,2,2,2} };
        case 'G': return { {2,0,0,0},{0,0,0,4},{0,4,2,4},{2,4,2,2},{1,2,2,2} };
        case 'H': return { {0,0,0,4},{2,0,2,4},{0,2,2,2} };
        case 'I': return { {0,0,2,0},{1,0,1,4},{0,4,2,4} };
        case 'J': return { {2,0,2,3},{2,3,1,4},{1,4,0,4},{0,4,0,3} };
        case 'K': return { {0,0,0,4},{0,2,2,0},{0,2,2,4} };
        case 'L': return { {0,0,0,4},{0,4,2,4} };
        case 'M': return { {0,4,0,0},{0,0,1,2},{1,2,2,0},{2,0,2,4} };
        case 'N': return { {0,4,0,0},{0,0,2,4},{2,4,2,0} };
        case 'O': return { {0,0,2,0},{2,0,2,4},{2,4,0,4},{0,4,0,0} };
        case 'P': return { {0,0,0,4},{0,0,2,0},{2,0,2,2},{0,2,2,2} };
        case 'Q': return { {0,0,2,0},{2,0,2,4},{2,4,0,4},{0,4,0,0},{1,3,2,4} };
        case 'R': return { {0,0,0,4},{0,0,2,0},{2,0,2,2},{0,2,2,2},{0,2,2,4} };
        case 'S': return { {2,0,0,0},{0,0,0,2},{0,2,2,2},{2,2,2,4},{2,4,0,4} };
        case 'T': return { {0,0,2,0},{1,0,1,4} };
        case 'U': return { {0,0,0,4},{0,4,2,4},{2,4,2,0} };
        case 'V': return { {0,0,1,4},{1,4,2,0} };
        case 'W': return { {0,0,0,4},{0,4,1,1},{1,1,2,4},{2,4,2,0} };
        case 'X': return { {0,0,2,4},{2,0,0,4} };
        case 'Y': return { {0,0,1,2},{2,0,1,2},{1,2,1,4} };
        case 'Z': return { {0,0,2,0},{2,0,0,4},{0,4,2,4} };
        case '0': return { {0,0,2,0},{2,0,2,4},{2,4,0,4},{0,4,0,0} };
        case '1': return { {0,1,1,0},{1,0,1,4} };
        case '2': return { {0,0,2,0},{2,0,2,2},{2,2,0,4},{0,4,2,4} };
        case '3': return { {0,0,2,0},{2,0,2,4},{0,2,2,2},{0,4,2,4} };
        case '4': return { {0,0,0,2},{0,2,2,2},{2,0,2,4} };
        case '5': return { {2,0,0,0},{0,0,0,2},{0,2,2,2},{2,2,2,4},{2,4,0,4} };
        case '6': return { {0,0,0,4},{0,4,2,4},{2,4,2,2},{0,2,2,2} };
        case '7': return { {0,0,2,0},{2,0,0,4} };
        case '8': return { {0,0,2,0},{0,0,0,4},{2,0,2,4},{0,2,2,2},{0,4,2,4} };
        case '9': return { {0,0,2,0},{0,0,0,2},{2,0,2,4},{0,2,2,2} };
        case '(': return { {1,0,0,1},{0,1,0,3},{0,3,1,4} };
        case ')': return { {0,0,1,1},{1,1,1,3},{1,3,0,4} };
        case '_': return { {0,4.6f,2,4.6f} };
        case '.': return { {0.9f,3.7f,1.1f,3.9f} };
        case '-': return { {0,2,2,2} };
        case '<': return { {2,0,0,2},{0,2,2,4} };
        case '>': return { {0,0,2,2},{2,2,0,4} };
        default:  return {};
    }
}

static void DrawText(SDL_Renderer* r, int x, int y, const std::string& text, int cellW, int cellH, int thickness, int letterSpacing) {
    int penX = x;
    for (char ch : text) {
        auto segs = GlyphSegs(ch);
        for (auto& s : segs) {
            int x1 = penX + (int)(s.x1 * cellW / 2.0f);
            int y1 = y + (int)(s.y1 * cellH / 4.0f);
            int x2 = penX + (int)(s.x2 * cellW / 2.0f);
            int y2 = y + (int)(s.y2 * cellH / 4.0f);
            for (int t = -thickness / 2; t <= thickness / 2; ++t) {
                SDL_RenderDrawLine(r, x1, y1 + t, x2, y2 + t);
                SDL_RenderDrawLine(r, x1 + t, y1, x2 + t, y2);
            }
        }
        penX += cellW + letterSpacing;
    }
}

static int TextWidth(const std::string& text, int cellW, int letterSpacing) {
    return (int)text.size() * (cellW + letterSpacing);
}

// Draws `text` centered (both axes) within `btn` - used for the top-bar
// buttons now that they're big squares instead of small pill shapes.
static void DrawCenteredLabel(SDL_Renderer* r, const SDL_Rect& btn, const std::string& text, int cellW, int cellH, int thickness) {
    int spacing = cellW / 4;
    int tw = TextWidth(text, cellW, spacing);
    int tx = btn.x + std::max(4, (btn.w - tw) / 2);
    int ty = btn.y + (btn.h - cellH) / 2;
    DrawText(r, tx, ty, text, cellW, cellH, thickness, spacing);
}

// ---- Networking -------------------------------------------------------------

static int ConnectToServer() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, g_serverIp.c_str(), &addr.sin_addr) <= 0) { close(sock); return -1; }

    struct timeval tv{ 4, 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return -1; }
    return sock;
}

// Sends `cmd`, reads a "<count>\n<line>\n<line>...\n" reply into `out`.
static bool SendCommandGetList(const std::string& cmd, std::vector<Item>& out) {
    if (g_sock < 0) return false;
    if (send(g_sock, cmd.c_str(), (int)cmd.size(), 0) < 0) return false;

    std::string buffer;
    char chunk[512];
    int expectedCount = -1;
    int received_lines = 0;
    std::vector<Item> items;

    while (true) {
        int received = recv(g_sock, chunk, sizeof(chunk), 0);
        if (received <= 0) break;
        buffer.append(chunk, received);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buffer.erase(0, pos + 1);

            if (expectedCount < 0) {
                expectedCount = atoi(line.c_str());
            }
            else {
                Item item;
                size_t tab = line.find('\t');
                if (tab == std::string::npos) {
                    item.name = line; // no color sent - fall back to white
                }
                else {
                    item.name = line.substr(0, tab);
                    ParseHexColor(line.substr(tab + 1), item.r, item.g, item.b);
                }
                items.push_back(item);
                received_lines++;
            }
        }
        if (expectedCount >= 0 && received_lines >= expectedCount) {
            out.swap(items);
            for (auto& it : out) {
                SDL_Log("Item '%s' -> RGB(%d,%d,%d)", it.name.c_str(), it.r, it.g, it.b);
            }
            return true;
        }
    }
    return false;
}

static void SendPlay(int catIdx, int soundIdx) {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "PLAY %d %d\n", catIdx, soundIdx);

    if (g_sock >= 0) {
        if (send(g_sock, cmd, (int)strlen(cmd), 0) > 0) return;
        close(g_sock);
        g_sock = -1;
    }
    g_sock = ConnectToServer();
    if (g_sock >= 0) {
        send(g_sock, cmd, (int)strlen(cmd), 0);
        g_status = "Reconnected";
    }
    else {
        g_status = "Connection lost - tap Refresh";
    }
}

// Sends a bare command (no arguments) to the PC, reconnecting once if needed.
static void SendBareCommand(const char* cmd) {
    if (g_sock >= 0) {
        if (send(g_sock, cmd, (int)strlen(cmd), 0) > 0) return;
        close(g_sock);
        g_sock = -1;
    }
    g_sock = ConnectToServer();
    if (g_sock >= 0) {
        send(g_sock, cmd, (int)strlen(cmd), 0);
        g_status = "Reconnected";
    }
    else {
        g_status = "Connection lost - tap Refresh";
    }
}

// Tells the PC to toggle its media playback (pause/unpause Chrome).
static void SendPause() { SendBareCommand("PAUSE\n"); }
static void SendNext() { SendBareCommand("NEXT\n"); }
static void SendPrev() { SendBareCommand("PREV\n"); }

// Refreshes whichever list is currently on screen (categories, or the
// current category's sounds).
static void RefreshFromServer() {
    g_status = "Connecting...";
    if (g_sock >= 0) { close(g_sock); g_sock = -1; }

    g_sock = ConnectToServer();
    if (g_sock < 0) {
        g_status = "Can't reach PC - check IP/WiFi";
        return;
    }

    if (g_currentCategory == -1) {
        if (SendCommandGetList("CATS\n", g_categories)) {
            g_status = std::to_string(g_categories.size()) + " categories";
        }
        else {
            g_status = "Connected, but category list failed";
        }
    }
    else {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "LIST %d\n", g_currentCategory);
        if (SendCommandGetList(cmd, g_sounds)) {
            g_status = std::to_string(g_sounds.size()) + " sounds";
        }
        else {
            g_status = "Connected, but sound list failed";
        }
    }
    g_lastRefreshTicks = SDL_GetTicks();
}

// ---- Main ---------------------------------------------------------------

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Gasoline Remote",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 1200, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        // Fall back to software rendering if the accelerated/vsync combo isn't available.
        SDL_Log("SDL_CreateRenderer (accelerated) failed: %s - retrying with default flags", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        return 1;
    }

    LoadSavedServerIp();
    RefreshFromServer();

    float scrollY = 0.0f;
    bool dragging = false;
    bool movedPastThreshold = false;
    int dragStartX = 0, dragStartY = 0;
    float dragStartScroll = 0;

    bool running = true;
    SDL_Event ev;

    while (running) {
        int winW, winH;
        SDL_GetWindowSize(window, &winW, &winH);

        bool inCategory = (g_currentCategory != -1);
        const std::vector<Item>& items = inCategory ? g_sounds : g_categories;

        SDL_Rect refreshBtn{ SIDE_MARGIN, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

        SDL_Rect backBtn{ refreshBtn.x + TOP_BTN_SIZE + TOP_BTN_GAP, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

        SDL_Rect pauseBtn{ winW - SIDE_MARGIN - TOP_BTN_SIZE, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

        // Left edge of the "free" middle zone: right after Back if it's
        // showing, otherwise right after Refresh.
        int leftEdge = (inCategory ? (backBtn.x + backBtn.w) : (refreshBtn.x + refreshBtn.w));

        // Center the Prev/Next pair in that zone, so the gap on the left
        // (from Back/Refresh) matches the gap on the right (to Pause).
        int middleWidth = pauseBtn.x - leftEdge;
        int contentWidth = 2 * TOP_BTN_SIZE + TOP_BTN_GAP;
        int outerGap = std::max(TOP_BTN_GAP, (middleWidth - contentWidth) / 2);

        SDL_Rect prevBtn{ leftEdge + outerGap, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };
        SDL_Rect nextBtn{ prevBtn.x + TOP_BTN_SIZE + TOP_BTN_GAP, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

        // Small "SETTINGS" button, tucked under Refresh on the status line.
        // (spacing here must match what DrawCenteredLabel uses internally: cellW/4)
        int settingsBtnW = TextWidth("SETTINGS", 10, 10 / 4) + 24;
        SDL_Rect settingsBtn{ winW - SIDE_MARGIN - settingsBtnW, refreshBtn.y + refreshBtn.h + 8, settingsBtnW, 44 };

        int rowWidth = winW - 2 * SIDE_MARGIN;

        std::vector<SDL_Rect> rowRects(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            SDL_Rect r;
            r.x = SIDE_MARGIN;
            r.y = TOP_BAR_H + (int)i * (ROW_H + ROW_PAD) - (int)scrollY;
            r.w = rowWidth;
            r.h = ROW_H;
            rowRects[i] = r;
        }

        int contentH = TOP_BAR_H + (int)items.size() * (ROW_H + ROW_PAD);
        float maxScroll = std::max(0.0f, (float)(contentH - winH));
        if (scrollY > maxScroll) scrollY = maxScroll;

        if (g_screen == SCREEN_LIST && SDL_GetTicks() - g_lastRefreshTicks > AUTO_REFRESH_MS) {
            RefreshFromServer();
        }

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            }
            else if (g_screen == SCREEN_SETTINGS) {
                // ---- Settings screen: text entry + Back/Save only ----
                if (ev.type == SDL_TEXTINPUT) {
                    for (const char* p = ev.text.text; *p; ++p) {
                        char c = *p;
                        if ((c >= '0' && c <= '9') || c == '.') {
                            if (g_ipEditBuffer.size() < 15) g_ipEditBuffer += c;
                        }
                    }
                }
                else if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.keysym.sym == SDLK_BACKSPACE && !g_ipEditBuffer.empty()) {
                        g_ipEditBuffer.pop_back();
                    }
                }
                else if (ev.type == SDL_MOUSEBUTTONUP || ev.type == SDL_FINGERUP) {
                    int tx, ty;
                    if (ev.type == SDL_MOUSEBUTTONUP) { tx = ev.button.x; ty = ev.button.y; }
                    else { tx = (int)(ev.tfinger.x * winW); ty = (int)(ev.tfinger.y * winH); }
                    SDL_Point pt{ tx, ty };

                    SDL_Rect settingsBackBtn{ SIDE_MARGIN, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };
                    SDL_Rect settingsSaveBtn{ winW - SIDE_MARGIN - TOP_BTN_SIZE, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

                    if (SDL_PointInRect(&pt, &settingsBackBtn)) {
                        g_screen = SCREEN_LIST;
                        SDL_StopTextInput();
                    }
                    else if (SDL_PointInRect(&pt, &settingsSaveBtn)) {
                        if (!g_ipEditBuffer.empty()) {
                            g_serverIp = g_ipEditBuffer;
                            SaveServerIp(g_serverIp);
                        }
                        g_screen = SCREEN_LIST;
                        SDL_StopTextInput();
                        RefreshFromServer();
                    }
                }
                continue; // skip the list-screen handling below entirely
            }
            else if (ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_FINGERDOWN) {
                int tx, ty;
                if (ev.type == SDL_MOUSEBUTTONDOWN) { tx = ev.button.x; ty = ev.button.y; }
                else { tx = (int)(ev.tfinger.x * winW); ty = (int)(ev.tfinger.y * winH); }
                dragging = true;
                movedPastThreshold = false;
                dragStartX = tx; dragStartY = ty;
                dragStartScroll = scrollY;
            }
            else if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_FINGERMOTION) {
                if (dragging) {
                    int tx, ty;
                    if (ev.type == SDL_MOUSEMOTION) { tx = ev.motion.x; ty = ev.motion.y; }
                    else { tx = (int)(ev.tfinger.x * winW); ty = (int)(ev.tfinger.y * winH); }
                    int dy = ty - dragStartY;
                    int dx = tx - dragStartX;
                    if (abs(dy) > 12 || abs(dx) > 12) movedPastThreshold = true;
                    if (movedPastThreshold) {
                        scrollY = dragStartScroll - dy;
                        if (scrollY < 0) scrollY = 0;
                        if (scrollY > maxScroll) scrollY = maxScroll;
                    }
                }
            }
            else if (ev.type == SDL_MOUSEWHEEL) {
                scrollY -= ev.wheel.y * 40;
                if (scrollY < 0) scrollY = 0;
                if (scrollY > maxScroll) scrollY = maxScroll;
            }
            else if (ev.type == SDL_MOUSEBUTTONUP || ev.type == SDL_FINGERUP) {
                if (dragging && !movedPastThreshold) {
                    int tx, ty;
                    if (ev.type == SDL_MOUSEBUTTONUP) { tx = ev.button.x; ty = ev.button.y; }
                    else { tx = (int)(ev.tfinger.x * winW); ty = (int)(ev.tfinger.y * winH); }
                    SDL_Point pt{ tx, ty };

                    if (SDL_PointInRect(&pt, &refreshBtn)) {
                        RefreshFromServer();
                    }
                    else if (SDL_PointInRect(&pt, &settingsBtn)) {
                        g_ipEditBuffer = g_serverIp;
                        g_screen = SCREEN_SETTINGS;
                        SDL_StartTextInput();
                    }
                    else if (SDL_PointInRect(&pt, &pauseBtn)) {
                        SendPause();
                    }
                    else if (SDL_PointInRect(&pt, &prevBtn)) {
                        SendPrev();
                    }
                    else if (SDL_PointInRect(&pt, &nextBtn)) {
                        SendNext();
                    }
                    else if (inCategory && SDL_PointInRect(&pt, &backBtn)) {
                        g_currentCategory = -1;
                        scrollY = 0;
                        RefreshFromServer();
                    }
                    else {
                        for (size_t i = 0; i < rowRects.size(); ++i) {
                            if (SDL_PointInRect(&pt, &rowRects[i])) {
                                if (inCategory) {
                                    SendPlay(g_currentCategory, (int)i);
                                }
                                else {
                                    g_currentCategory = (int)i;
                                    scrollY = 0;
                                    RefreshFromServer();
                                }
                                break;
                            }
                        }
                    }
                }
                dragging = false;
            }
        }

        // ---- Draw ----
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);

        if (g_screen == SCREEN_SETTINGS) {
            SDL_Rect settingsBackBtn{ SIDE_MARGIN, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };
            SDL_Rect settingsSaveBtn{ winW - SIDE_MARGIN - TOP_BTN_SIZE, SIDE_MARGIN, TOP_BTN_SIZE, TOP_BTN_SIZE };

            SDL_SetRenderDrawColor(renderer, 120, 60, 60, 255);
            SDL_RenderFillRect(renderer, &settingsBackBtn);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawCenteredLabel(renderer, settingsBackBtn, "<BACK", 13, 14, 2);

            SDL_SetRenderDrawColor(renderer, 60, 150, 90, 255);
            SDL_RenderFillRect(renderer, &settingsSaveBtn);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawCenteredLabel(renderer, settingsSaveBtn, "SAVE", 13, 14, 2);

            SDL_SetRenderDrawColor(renderer, 200, 200, 60, 255);
            DrawText(renderer, SIDE_MARGIN, SIDE_MARGIN + TOP_BTN_SIZE + 30, "PC IP ADDRESS", 14, 18, 3, 4);

            SDL_Rect ipBox{ SIDE_MARGIN, SIDE_MARGIN + TOP_BTN_SIZE + 80, winW - 2 * SIDE_MARGIN, 100 };
            SDL_SetRenderDrawColor(renderer, 45, 45, 52, 255);
            SDL_RenderFillRect(renderer, &ipBox);
            SDL_SetRenderDrawColor(renderer, 120, 120, 135, 255);
            SDL_RenderDrawRect(renderer, &ipBox);

            std::string shown = g_ipEditBuffer;
            if ((SDL_GetTicks() / 500) % 2 == 0) shown += "_"; // blinking cursor
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawText(renderer, ipBox.x + 20, ipBox.y + 25, shown, 22, 50, 4, 8);

            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            DrawText(renderer, SIDE_MARGIN, ipBox.y + ipBox.h + 30, "DIGITS AND DOTS ONLY", 10, 12, 2, 4);

            SDL_RenderPresent(renderer);
            SDL_Delay(16);
            continue;
        }

        SDL_Rect listClip{ 0, TOP_BAR_H, winW, winH - TOP_BAR_H };
        SDL_RenderSetClipRect(renderer, &listClip);
        for (size_t i = 0; i < rowRects.size(); ++i) {
            if (rowRects[i].y + rowRects[i].h < TOP_BAR_H || rowRects[i].y > winH) continue;
            const Item& item = items[i];
            SDL_SetRenderDrawColor(renderer, item.r, item.g, item.b, 255);
            SDL_RenderFillRect(renderer, &rowRects[i]);

            int cellW = 20, cellH = 40; // 2.5x larger than the original 16
            int tw = TextWidth(item.name, cellW, SOUND_TEXT_LETTER_SPACING);
            int tx = rowRects[i].x + std::max(12, (rowRects[i].w - tw) / 2);
            int ty = rowRects[i].y + (rowRects[i].h - cellH) / 2;
            // Text color is a true inversion of the button's background,
            // matching the PC app.
            SDL_SetRenderDrawColor(renderer, 255 - item.r, 255 - item.g, 255 - item.b, 255);
            DrawText(renderer, tx, ty, item.name, cellW, cellH, SOUND_TEXT_BOLDNESS, SOUND_TEXT_LETTER_SPACING);
        }
        SDL_RenderSetClipRect(renderer, nullptr);

        // Top bar (fixed)
        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_Rect topBarBg{ 0, 0, winW, TOP_BAR_H };
        SDL_RenderFillRect(renderer, &topBarBg);

        SDL_SetRenderDrawColor(renderer, 60, 60, 200, 255);
        SDL_RenderFillRect(renderer, &refreshBtn);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawCenteredLabel(renderer, refreshBtn, "REFRESH", 13, 14, 2);

        if (inCategory) {
            SDL_SetRenderDrawColor(renderer, 120, 60, 60, 255);
            SDL_RenderFillRect(renderer, &backBtn);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            DrawCenteredLabel(renderer, backBtn, "<BACK", 13, 14, 2);
        }

        SDL_SetRenderDrawColor(renderer, 90, 150, 90, 255);
        SDL_RenderFillRect(renderer, &prevBtn);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawCenteredLabel(renderer, prevBtn, "<<", 13, 14, 2);

        SDL_SetRenderDrawColor(renderer, 90, 150, 90, 255);
        SDL_RenderFillRect(renderer, &pauseBtn);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawCenteredLabel(renderer, pauseBtn, "PAUSE", 13, 14, 2);

        SDL_SetRenderDrawColor(renderer, 90, 150, 90, 255);
        SDL_RenderFillRect(renderer, &nextBtn);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawCenteredLabel(renderer, nextBtn, ">>", 13, 14, 2);

        SDL_SetRenderDrawColor(renderer, 200, 200, 60, 255);
        DrawText(renderer, SIDE_MARGIN, refreshBtn.y + refreshBtn.h + 14, g_status, 10, 12, 2, 10 / 4);

        SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
        SDL_RenderFillRect(renderer, &settingsBtn);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        DrawCenteredLabel(renderer, settingsBtn, "SETTINGS", 10, 12, 2);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (g_sock >= 0) close(g_sock);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
