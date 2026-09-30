// In-stream overlay for Dusk: a persistent draggable handle plus the menu
// it opens.
//
// This is a fork addition, not upstream Moonlight. It reuses the existing
// overlay pipeline rather than adding a rendering path: every renderer
// already uploads one SDL_Surface per OverlayType and composites it over
// the decoded frame, and nothing in that path assumes the surface holds
// text. So the handle and the menu are surfaces we draw ourselves plus the
// input handling to drive them.
//
// # The mouse problem
//
// While streaming, Moonlight normally captures the mouse: the cursor is
// locked and only relative deltas reach us, so there is no pointer position
// to hit-test a button against. Two modes therefore exist:
//
//   - Uncaptured, or absolute mouse mode. SDL gives real window
//     coordinates and everything works the obvious way.
//   - Captured, relative mode. We integrate the deltas into a synthetic
//     cursor of our own, clamped to the window. Clamping is what makes it
//     usable rather than a drifting guess: the synthetic cursor and the
//     real one may diverge in the middle of the screen, but shoving the
//     mouse hard into an edge pins both, so a handle parked near an edge is
//     reliably reachable. A handle dragged to the middle of the screen is
//     not, which is why the default position is a corner.
//
// The menu deliberately owns no Session state. Actions are dispatched
// through Session the same way the keyboard shortcuts in
// input/keyboard.cpp are, so there is exactly one place where each action
// is implemented.

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

    bool isMenuOpen() const { return m_MenuOpen; }

    void setMenuOpen(bool open);
    void toggleMenu() { setMenuOpen(!m_MenuOpen); }

    /// Draw the handle, and the menu if it is open. Safe to call repeatedly;
    /// it only republishes surfaces that actually changed.
    void refresh();

    /// Handle a key while the menu is open.
    ///
    /// Returns true when the menu consumed it. An open menu swallows the
    /// keyboard entirely rather than letting stray keys through to the
    /// host, which would type into whatever is running over there.
    bool handleKeyEvent(const SDL_KeyboardEvent* event);

    /// Feed pointer input in. `x` and `y` are window coordinates and are
    /// ignored in relative mode, where `xrel`/`yrel` drive the synthetic
    /// cursor instead.
    ///
    /// Returns true when the overlay consumed the event, in which case the
    /// caller must not forward it to the host.
    bool handleMouseMotion(int x, int y, int xrel, int yrel, bool absolute);
    bool handleMouseButton(int button, bool pressed, int x, int y, bool absolute);

    /// Window size, needed to place the handle and clamp the cursor. The
    /// input handler knows it; the menu does not.
    void setWindowSize(int width, int height);

    /// Where the handle sits, in the coordinate space of a renderer's
    /// display. Renderers call this because the handle moves, unlike the
    /// fixed corners the other overlays use.
    void handleOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                      int surfaceHeight, int* x, int* y);

    /// Where the menu sits. Anchored to the handle rather than centred, so
    /// it appears where the user is already looking.
    void menuOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                    int surfaceHeight, int* x, int* y);

    /// Where our own pointer sits. Window coordinates scaled to the
    /// renderer's display, which differ on a HiDPI screen.
    void cursorOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                      int surfaceHeight, int* x, int* y);

private:
    struct Item {
        const char* label;
        const char* hint;
    };

    static const Item s_Items[];
    static const int s_ItemCount;

    void repaintHandle();
    void repaintMenu();
    void repaintCursor();
    /// Show or hide our pointer as the overlay takes and releases the mouse.
    void setEngaged(bool engaged);
    void activate(int index);

    bool openFont();
    void loadPosition();
    void savePosition();

    int menuWidth() const;
    int menuHeight() const;
    bool pointInHandle(int x, int y) const;
    int itemAtPoint(int x, int y) const;

    void drawRect(SDL_Surface* surface, int x, int y, int w, int h, SDL_Color color);
    void drawText(SDL_Surface* surface, TTF_Font* font, const char* text, int x, int y, SDL_Color color);

    OverlayManager* m_Manager;

    bool m_MenuOpen;
    int m_Selected;
    bool m_HandleHot;
    bool m_Engaged;

    // Normalised 0..1 anchor for the handle's top-left, so it survives a
    // resolution change.
    float m_HandleX;
    float m_HandleY;

    bool m_Dragging;
    bool m_DragMoved;
    int m_DragOffsetX;
    int m_DragOffsetY;

    // Synthetic cursor, used only when the real one is captured.
    int m_CursorX;
    int m_CursorY;

    int m_WindowWidth;
    int m_WindowHeight;

    TTF_Font* m_Font;
    TTF_Font* m_TitleFont;
    QByteArray m_FontData;
};

}
