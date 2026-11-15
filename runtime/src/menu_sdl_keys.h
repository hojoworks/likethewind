#ifndef LIKETHEWIND_MENU_SDL_KEYS_H
#define LIKETHEWIND_MENU_SDL_KEYS_H

#ifdef LIKETHEWIND_HAVE_SDL
#include <SDL.h>

#include "likethewind/menu.h"

namespace likethewind {

inline bool menu_key_from_sym(SDL_Keycode sym, MenuKey &k) {
    switch (sym) {
    case SDLK_UP: k = MenuKey::Up; return true;
    case SDLK_DOWN: k = MenuKey::Down; return true;
    case SDLK_LEFT: k = MenuKey::Left; return true;
    case SDLK_RIGHT: k = MenuKey::Right; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: k = MenuKey::Enter; return true;
    case SDLK_q:
    case SDLK_PAGEUP: k = MenuKey::TabPrev; return true;
    case SDLK_e:
    case SDLK_PAGEDOWN: k = MenuKey::TabNext; return true;
    case SDLK_BACKSPACE: k = MenuKey::Back; return true;
    case SDLK_DELETE: k = MenuKey::Delete; return true;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS: k = MenuKey::Plus; return true;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: k = MenuKey::Minus; return true;
    case SDLK_s: k = MenuKey::Solo; return true;
    case SDLK_r: k = MenuKey::SendSelect; return true;
    default: return false;
    }
}

inline bool menu_key_from_button(Uint8 b, MenuKey &k) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: k = MenuKey::Up; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: k = MenuKey::Down; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: k = MenuKey::Left; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: k = MenuKey::Right; return true;
    case SDL_CONTROLLER_BUTTON_A: k = MenuKey::Enter; return true;
    case SDL_CONTROLLER_BUTTON_B: k = MenuKey::Back; return true;
    case SDL_CONTROLLER_BUTTON_X: k = MenuKey::SendSelect; return true;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: k = MenuKey::TabPrev; return true;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: k = MenuKey::TabNext; return true;
    default: return false;
    }
}

} // namespace likethewind

#endif // LIKETHEWIND_HAVE_SDL
#endif // LIKETHEWIND_MENU_SDL_KEYS_H
