// Roteiro FINITO de entradas para provas automatizadas (diagnóstico; nunca em jogo normal).
// NIER_INPUT_SCRIPT="2600:start,2900:a:6,3200:down" — quadro apresentado : botão [: quadros pressionado].
// Com sufixo 's' o instante é em segundos desde o 1º quadro apresentado: "45s:start,60s:a" (independe do FPS).
// Cada toque roda uma vez; no máximo 60 toques por execução (regra: nada de START/A em laço).
#include "renderer.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace nier::render {
namespace {
struct Press { uint64_t frame; int button; uint32_t hold; bool seconds = false; bool down = false, done = false; uint64_t downFrame = 0; };
std::chrono::steady_clock::time_point g_start;
bool g_started = false;
std::vector<Press> g_script;
SDL_Joystick* g_pad = nullptr;
bool g_parsed = false;

int Button(const std::string& n) {
  static const struct { const char* name; int button; } map[] = {
      {"a", SDL_GAMEPAD_BUTTON_SOUTH}, {"b", SDL_GAMEPAD_BUTTON_EAST}, {"x", SDL_GAMEPAD_BUTTON_WEST},
      {"y", SDL_GAMEPAD_BUTTON_NORTH}, {"start", SDL_GAMEPAD_BUTTON_START}, {"back", SDL_GAMEPAD_BUTTON_BACK},
      {"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
      {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};
  for (auto& m : map) if (n == m.name) return m.button;
  return -1;
}

void Parse() {
  g_parsed = true;
  const char* s = std::getenv("NIER_INPUT_SCRIPT");
  if (!s || !*s) return;
  std::string text = s;
  size_t at = 0;
  while (at < text.size() && g_script.size() < 60) {
    size_t end = text.find(',', at);
    std::string item = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    at = end == std::string::npos ? text.size() : end + 1;
    unsigned long long frame = 0; char name[16] = {}; unsigned hold = 6;
    bool seconds = false;
    if (std::sscanf(item.c_str(), "%llus:%15[a-z]:%u", &frame, name, &hold) >= 2) seconds = true;
    else if (std::sscanf(item.c_str(), "%llu:%15[a-z]:%u", &frame, name, &hold) < 2) continue;
    int b = Button(name);
    if (b >= 0) g_script.push_back({frame, b, hold ? hold : 6, seconds});
  }
  if (g_script.empty()) return;
  SDL_VirtualJoystickDesc desc{};
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.name = "NieR roteiro de prova (C1)";
  SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  g_pad = id ? SDL_OpenJoystick(id) : nullptr;
  if (g_pad) {
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
  }
  std::fprintf(stderr, "[nier-input] roteiro com %zu toques, controle virtual %s\n", g_script.size(), g_pad ? "ligado" : SDL_GetError());
}
}  // namespace

// Chamado a cada quadro apresentado (thread do jogo).
void DriveTestInput(uint64_t frame) {
  if (!g_parsed) Parse();
  if (!g_pad) return;
  if (!g_started) { g_started = true; g_start = std::chrono::steady_clock::now(); }
  uint64_t elapsed = uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start).count());
  for (auto& p : g_script) {
    if (p.done) continue;
    if (!p.down && (p.seconds ? elapsed >= p.frame : frame >= p.frame)) {
      SDL_SetJoystickVirtualButton(g_pad, p.button, true); p.down = true; p.downFrame = frame;
      std::fprintf(stderr, "[nier-input] quadro %llu (%llus): botão %d pressionado\n", (unsigned long long)frame, (unsigned long long)elapsed, p.button);
    } else if (p.down && frame >= p.downFrame + p.hold) {
      SDL_SetJoystickVirtualButton(g_pad, p.button, false); p.done = true;
    }
  }
}

}  // namespace nier::render
