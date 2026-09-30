// In-stream overlay menu for Dusk.
//
// This is a fork addition, not upstream Moonlight. It reuses the existing
// overlay pipeline rather than adding a rendering path: every renderer
// already uploads one SDL_Surface per OverlayType and composites it over the
// decoded frame, and nothing in that path assumes the surface holds text. So
// an interactive menu is a surface we draw ourselves plus the input handling
// to drive it.
//
// The menu deliberately owns no Session state. Actions are dispatched through
// Session the same way the keyboard shortcuts in input/keyboard.cpp are, so
// there is exactly one place where each action is implemented.

#pragma once

#include <QByteArray>

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace Overlay {

class OverlayManager;

class Menu
{
public:
    explicit Menu(OverlayManager* manager);
    ~Menu();

    bool isVisible() const { return m_Visible; }

    void setVisible(bool visible);
    void toggle() { setVisible(!m_Visible); }

    /// Handle a key while the menu is open.
    ///
    /// Always returns true when the menu is visible: an open menu swallows
    /// the keyboard entirely rather than letting stray keys through to the
    /// host, which would type into whatever is running over there.
    bool handleKeyEvent(const SDL_KeyboardEvent* event);

private:
    struct Item {
        const char* label;
        const char* hint;
    };

    static const Item s_Items[];
    static const int s_ItemCount;

    void repaint();
    void activateSelected();
    SDL_Surface* render();

    bool openFont();
    void drawRect(SDL_Surface* surface, int x, int y, int w, int h, SDL_Color color);
    void drawText(SDL_Surface* surface, TTF_Font* font, const char* text, int x, int y, SDL_Color color);

    OverlayManager* m_Manager;
    bool m_Visible;
    int m_Selected;

    TTF_Font* m_Font;
    TTF_Font* m_TitleFont;
    QByteArray m_FontData;
};

}
