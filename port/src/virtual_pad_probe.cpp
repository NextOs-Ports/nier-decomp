#include "virtual_pad_probe.h"
#include <rex/input/input_system.h>
#include <SDL3/SDL.h>
#include <array>
#include <cstdio>
#include <stdexcept>

void NierProbeVirtualGamepad(rex::input::InputSystem* input) {
    if (!input) throw std::runtime_error("virtual pad probe: no input system");
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT)-1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.name = "NieR automated virtual controller";
    const auto id = SDL_AttachVirtualJoystick(&desc);
    if (!id) throw std::runtime_error(SDL_GetError());
    auto* pad = SDL_OpenJoystick(id);
    if (!pad) throw std::runtime_error(SDL_GetError());
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    auto update = [&] {
        SDL_UpdateJoysticks();
        SDL_PumpEvents();
        rex::input::X_INPUT_STATE state{};
        for (int attempt=0; attempt<20; ++attempt) {
            if (input->GetState(0, &state) == 0) return state;
            SDL_Delay(5);
            SDL_PumpEvents();
        }
        throw std::runtime_error("virtual pad not assigned to XInput user 0");
    };
    update();
    const std::array<std::pair<int,unsigned>,14> buttons{{
        {SDL_GAMEPAD_BUTTON_SOUTH,0x1000},{SDL_GAMEPAD_BUTTON_EAST,0x2000},
        {SDL_GAMEPAD_BUTTON_WEST,0x4000},{SDL_GAMEPAD_BUTTON_NORTH,0x8000},
        {SDL_GAMEPAD_BUTTON_DPAD_UP,1},{SDL_GAMEPAD_BUTTON_DPAD_DOWN,2},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,4},{SDL_GAMEPAD_BUTTON_DPAD_RIGHT,8},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,0x100},{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,0x200},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,0x40},{SDL_GAMEPAD_BUTTON_RIGHT_STICK,0x80},
        {SDL_GAMEPAD_BUTTON_BACK,0x20},{SDL_GAMEPAD_BUTTON_START,0x10}}};
    for (auto [button,mask]:buttons) {
        SDL_SetJoystickVirtualButton(pad,button,true);
        auto state=update();
        if ((uint16_t(state.gamepad.buttons)&mask) != mask)
            throw std::runtime_error("virtual pad button mapping failed");
        SDL_SetJoystickVirtualButton(pad,button,false);
        state=update();
        if (uint16_t(state.gamepad.buttons)&mask)
            throw std::runtime_error("virtual pad button release failed");
    }
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, 20000);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTY, -18000);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX, -16000);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY, 14000);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
    SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    auto state=update();
    if (!(int16_t(state.gamepad.thumb_lx)>0 && int16_t(state.gamepad.thumb_ly)>0 &&
          int16_t(state.gamepad.thumb_rx)<0 && int16_t(state.gamepad.thumb_ry)<0 &&
          state.gamepad.left_trigger>250 && state.gamepad.right_trigger>250))
        throw std::runtime_error("virtual pad axis/trigger mapping failed");
    if (SDL_HasEvent(SDL_EVENT_QUIT)) throw std::runtime_error("isolated controls requested quit");
    SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_BACK, true);
    update();
    if (SDL_HasEvent(SDL_EVENT_QUIT)) throw std::runtime_error("BACK alone requested quit");
    std::fprintf(stderr, "NIER_PAD_PROBE mapping PASS buttons=14 axes=4 triggers=2 release=ok; applying immediate combo\n");
    const auto start = SDL_GetTicksNS();
    SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_START, true);
    update();
    if (!SDL_HasEvent(SDL_EVENT_QUIT)) throw std::runtime_error("combo did not request quit");
    std::fprintf(stderr,"NIER_PAD_PROBE PASS buttons=14 axes=4 triggers=2 release=ok combo_us=%llu\n",
        static_cast<unsigned long long>((SDL_GetTicksNS()-start)/1000));
    // Keep the virtual device alive until the queued quit is consumed by ReXApp.
}
