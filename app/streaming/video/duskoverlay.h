// In-stream overlay for Dusk: a persistent draggable handle and the
// compact menu it drops down.
//
// This is a fork addition, not upstream Moonlight. It reuses the existing
// overlay pipeline rather than adding a rendering path: every renderer
// already uploads one SDL_Surface per OverlayType and composites it over
// the decoded frame, and nothing in that path assumes the surface holds
// text.
//
// # How the mouse works, and why there is no cursor of our own
//
// While streaming, Moonlight captures the mouse: the cursor is hidden and
// only relative deltas reach us, so there is no pointer to hit-test with.
// The first attempt at this drew a synthetic cursor from accumulated
// deltas. It was a mistake -- it drifted from the real pointer, and any
// moment we stopped receiving motion left it stranded on screen next to
// the system cursor.
//
// So the menu releases mouse capture while it is open and restores it on
// close. Moonlight already has that machinery for unbinding the mouse, and
// it hands back the real system cursor, correctly drawn and positioned by
// the OS, for free. The cost is that with the mouse captured the handle
// cannot be hovered -- there is genuinely nothing to hover with -- so the
// hotkey is the way in, and once the menu is open everything is clickable.
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

    /// Draw the handle, and the menu if it is open.
    void refresh();

    /// Returns true when the menu consumed the key. An open menu swallows
    /// the keyboard entirely rather than letting stray keys through to the
    /// host, which would type into whatever is running over there.
    bool handleKeyEvent(const SDL_KeyboardEvent* event);

    /// Pointer input, in window coordinates. Returns true when the overlay
    /// consumed the event, in which case the caller must not forward it to
    /// the host or act on it itself.
    ///
    /// `usable` is false when the pointer position is meaningless, which is
    /// the case while the mouse is captured in relative mode.
    bool handleMouseMotion(int x, int y, bool usable);
    bool handleMouseButton(int button, bool pressed, int x, int y, bool usable);

    void setWindowSize(int width, int height);

    /// Where the handle and menu sit, in a renderer's display space. They
    /// move, unlike the fixed corners the other overlays use, so renderers
    /// ask rather than hardcode.
    void handleOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                      int surfaceHeight, int* x, int* y);
    void menuOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                    int surfaceHeight, int* x, int* y);

private:
    enum Action {
        ActionStats,
        ActionSwitchDisplay,
        ActionFullScreen,
        ActionReleaseMouse,
        ActionMinimize,
        ActionDisconnect,
        ActionMax
    };

    /// Sunshine exposes up to twelve outputs on Ctrl+Alt+Shift+F1..F12.
    static const int k_DisplayCount = 8;

    /// The menu has two pages: the actions, and the display picker. A
    /// submenu rather than eight more rows, because the whole point of
    /// this thing is to cover as little of the game as possible.
    enum Page {
        PageMain,
        PageDisplays,
    };

    void repaintHandle();
    void repaintMenu();
    void activate(int index);
    const char* labelFor(int index) const;
    int rowCount() const;

    /// Press a hotkey on the host.
    ///
    /// Sent rather than typed: Ctrl+Alt+Shift is also Moonlight's own
    /// shortcut prefix, and on macOS the user would have to know that
    /// Option stands in for Alt. Going through the protocol sidesteps both.
    void sendHostHotkey(short keyCode);

    void setEngaged(bool engaged);

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
    Page m_Page;
    int m_Selected;
    bool m_HandleHot;
    bool m_Engaged;

    /// Whether capture was on before the menu took it, so closing restores
    /// what the user had rather than assuming.
    bool m_RestoreCapture;

    // Normalised 0..1 anchor for the handle, so it survives a resolution
    // change.
    float m_HandleX;
    float m_HandleY;

    bool m_Dragging;
    bool m_DragMoved;
    int m_DragOffsetX;
    int m_DragOffsetY;

    int m_WindowWidth;
    int m_WindowHeight;

    TTF_Font* m_Font;
    TTF_Font* m_TitleFont;
    QByteArray m_FontData;
};

}
