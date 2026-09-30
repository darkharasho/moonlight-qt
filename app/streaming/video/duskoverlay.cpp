#include "duskoverlay.h"
#include "overlaymanager.h"
#include "path.h"
#include "streaming/session.h"

#include <QSettings>

using namespace Overlay;

// Panel geometry, in the overlay surface's own pixels.
#define HANDLE_SIZE     56
#define MENU_WIDTH      560
#define MENU_PADDING    28
#define TITLE_HEIGHT    64
#define ROW_HEIGHT      52
#define ROW_GAP         4
#define FONT_SIZE       22
#define TITLE_FONT_SIZE 28

// Far enough that a click with a shaky hand is still a click.
#define DRAG_THRESHOLD  4

#define SETTINGS_KEY_X "dusk/overlayhandlex"
#define SETTINGS_KEY_Y "dusk/overlayhandley"

// Dusk's accent, so the overlay reads as part of the same app as the grid.
static const SDL_Color ACCENT     = {0xE8, 0xB3, 0x3A, 0xFF};
static const SDL_Color PANEL_BG   = {0x14, 0x16, 0x1C, 0xEB};
static const SDL_Color HANDLE_BG  = {0x14, 0x16, 0x1C, 0xC8};
static const SDL_Color ROW_BG     = {0x1F, 0x22, 0x2B, 0xFF};
static const SDL_Color TEXT       = {0xEC, 0xEC, 0xEF, 0xFF};
static const SDL_Color TEXT_DIM   = {0x8A, 0x8F, 0x9A, 0xFF};
static const SDL_Color TEXT_DARK  = {0x12, 0x11, 0x0C, 0xFF};

const Menu::Item Menu::s_Items[] = {
    { "Resume",            "Close this menu"          },
    { "Performance stats", "Toggle the stats overlay" },
    { "Toggle fullscreen", ""                         },
    { "Disconnect",        "End the session"          },
};

const int Menu::s_ItemCount = SDL_arraysize(Menu::s_Items);

Menu::Menu(OverlayManager* manager) :
    m_Manager(manager),
    m_MenuOpen(false),
    m_Selected(0),
    m_HandleHot(false),
    m_HandleX(0.965f),
    m_HandleY(0.035f),
    m_Dragging(false),
    m_DragMoved(false),
    m_DragOffsetX(0),
    m_DragOffsetY(0),
    m_CursorX(0),
    m_CursorY(0),
    m_WindowWidth(1920),
    m_WindowHeight(1080),
    m_Font(nullptr),
    m_TitleFont(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
    loadPosition();
}

Menu::~Menu()
{
    if (m_Font != nullptr) {
        TTF_CloseFont(m_Font);
    }
    if (m_TitleFont != nullptr) {
        TTF_CloseFont(m_TitleFont);
    }
}

void Menu::loadPosition()
{
    QSettings settings;
    bool okX = false, okY = false;
    float x = settings.value(SETTINGS_KEY_X, m_HandleX).toFloat(&okX);
    float y = settings.value(SETTINGS_KEY_Y, m_HandleY).toFloat(&okY);

    // Clamp rather than trust: a corrupt or stale value must not park the
    // handle off-screen where it can never be dragged back.
    if (okX) {
        m_HandleX = SDL_clamp(x, 0.0f, 1.0f);
    }
    if (okY) {
        m_HandleY = SDL_clamp(y, 0.0f, 1.0f);
    }
}

void Menu::savePosition()
{
    QSettings settings;
    settings.setValue(SETTINGS_KEY_X, m_HandleX);
    settings.setValue(SETTINGS_KEY_Y, m_HandleY);
}

void Menu::setWindowSize(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    m_WindowWidth = width;
    m_WindowHeight = height;
}

bool Menu::openFont()
{
    if (m_Font != nullptr && m_TitleFont != nullptr) {
        return true;
    }

    if (m_FontData.isEmpty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dusk overlay font failed to load");
        return false;
    }

    // The RW ops take ownership (freesrc = 1), so each font needs its own.
    // m_FontData must outlive both, which it does as a member.
    if (m_Font == nullptr) {
        m_Font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                1, FONT_SIZE);
    }
    if (m_TitleFont == nullptr) {
        m_TitleFont = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                     1, TITLE_FONT_SIZE);
    }

    if (m_Font == nullptr || m_TitleFont == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_OpenFont() failed for Dusk overlay: %s", TTF_GetError());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------- geometry

int Menu::menuWidth() const
{
    return MENU_WIDTH;
}

int Menu::menuHeight() const
{
    return TITLE_HEIGHT + (s_ItemCount * (ROW_HEIGHT + ROW_GAP)) + MENU_PADDING;
}

void Menu::handleOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                        int surfaceHeight, int* x, int* y)
{
    *x = SDL_clamp((int)(m_HandleX * displayWidth), 0, SDL_max(0, displayWidth - surfaceWidth));
    *y = SDL_clamp((int)(m_HandleY * displayHeight), 0, SDL_max(0, displayHeight - surfaceHeight));
}

void Menu::menuOrigin(int displayWidth, int displayHeight, int surfaceWidth,
                      int surfaceHeight, int* x, int* y)
{
    // Anchored under the handle so the menu opens where the eye already is,
    // then clamped so it never hangs off the edge when the handle is parked
    // in a corner -- which is the common case.
    int handleX, handleY;
    handleOrigin(displayWidth, displayHeight, HANDLE_SIZE, HANDLE_SIZE, &handleX, &handleY);

    *x = SDL_clamp(handleX + HANDLE_SIZE / 2 - surfaceWidth / 2,
                   0, SDL_max(0, displayWidth - surfaceWidth));
    *y = SDL_clamp(handleY + HANDLE_SIZE + 12,
                   0, SDL_max(0, displayHeight - surfaceHeight));
}

bool Menu::pointInHandle(int x, int y) const
{
    int hx = SDL_clamp((int)(m_HandleX * m_WindowWidth), 0, SDL_max(0, m_WindowWidth - HANDLE_SIZE));
    int hy = SDL_clamp((int)(m_HandleY * m_WindowHeight), 0, SDL_max(0, m_WindowHeight - HANDLE_SIZE));
    return x >= hx && x < hx + HANDLE_SIZE && y >= hy && y < hy + HANDLE_SIZE;
}

int Menu::itemAtPoint(int x, int y) const
{
    int mx, my;
    const_cast<Menu*>(this)->menuOrigin(m_WindowWidth, m_WindowHeight,
                                        menuWidth(), menuHeight(), &mx, &my);

    int localX = x - mx;
    int localY = y - my;
    if (localX < MENU_PADDING || localX > menuWidth() - MENU_PADDING) {
        return -1;
    }

    for (int i = 0; i < s_ItemCount; i++) {
        int rowTop = TITLE_HEIGHT + i * (ROW_HEIGHT + ROW_GAP);
        if (localY >= rowTop && localY < rowTop + ROW_HEIGHT) {
            return i;
        }
    }
    return -1;
}

// ------------------------------------------------------------------- state

void Menu::setMenuOpen(bool open)
{
    if (m_MenuOpen == open) {
        return;
    }

    m_MenuOpen = open;
    if (open) {
        // Always open on the first item. Resuming where the selection was
        // left invites accidentally activating Disconnect on a blind press.
        m_Selected = 0;
    }

    m_Manager->setOverlayState(OverlayMenu, open);
    if (open) {
        repaintMenu();
    }
}

void Menu::refresh()
{
    m_Manager->setOverlayState(OverlayHandle, true);
    repaintHandle();
    if (m_MenuOpen) {
        repaintMenu();
    }
}

void Menu::activate(int index)
{
    Session* session = Session::get();

    switch (index) {
    case 0: // Resume
        setMenuOpen(false);
        break;

    case 1: // Performance stats
        if (session != nullptr) {
            OverlayManager& overlays = session->getOverlayManager();
            overlays.setOverlayState(OverlayDebug, !overlays.isOverlayEnabled(OverlayDebug));
        }
        setMenuOpen(false);
        break;

    case 2: // Toggle fullscreen
        setMenuOpen(false);
        if (Session::s_ActiveSession != nullptr) {
            Session::s_ActiveSession->toggleFullscreen();
        }
        break;

    case 3: // Disconnect
        setMenuOpen(false);
        {
            // Same path as the quit key combo, so there is one teardown.
            SDL_Event event;
            SDL_zero(event);
            event.type = SDL_QUIT;
            event.quit.timestamp = SDL_GetTicks();
            SDL_PushEvent(&event);
        }
        break;

    default:
        break;
    }
}

// ------------------------------------------------------------------- input

bool Menu::handleKeyEvent(const SDL_KeyboardEvent* event)
{
    if (!m_MenuOpen) {
        return false;
    }

    // Swallow key-up too, so the host never sees a release for a press it
    // never got.
    if (event->state != SDL_PRESSED) {
        return true;
    }

    switch (event->keysym.sym) {
    case SDLK_UP:
    case SDLK_k:
        m_Selected = (m_Selected + s_ItemCount - 1) % s_ItemCount;
        repaintMenu();
        break;

    case SDLK_DOWN:
    case SDLK_j:
        m_Selected = (m_Selected + 1) % s_ItemCount;
        repaintMenu();
        break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        activate(m_Selected);
        break;

    case SDLK_ESCAPE:
        setMenuOpen(false);
        break;

    default:
        break;
    }

    return true;
}

bool Menu::handleMouseMotion(int x, int y, int xrel, int yrel, bool absolute)
{
    if (absolute) {
        m_CursorX = x;
        m_CursorY = y;
    }
    else {
        // Integrate the deltas and clamp. The clamp is what keeps this
        // usable: mid-screen the synthetic cursor drifts from the real one,
        // but pushing into an edge pins both, which re-syncs them.
        m_CursorX = SDL_clamp(m_CursorX + xrel, 0, m_WindowWidth - 1);
        m_CursorY = SDL_clamp(m_CursorY + yrel, 0, m_WindowHeight - 1);
    }

    if (m_Dragging) {
        int newX = m_CursorX - m_DragOffsetX;
        int newY = m_CursorY - m_DragOffsetY;

        if (SDL_abs(xrel) + SDL_abs(yrel) > 0) {
            m_DragMoved = true;
        }

        m_HandleX = SDL_clamp((float)newX / SDL_max(1, m_WindowWidth), 0.0f, 1.0f);
        m_HandleY = SDL_clamp((float)newY / SDL_max(1, m_WindowHeight), 0.0f, 1.0f);
        repaintHandle();
        return true;
    }

    if (m_MenuOpen) {
        int hovered = itemAtPoint(m_CursorX, m_CursorY);
        if (hovered >= 0 && hovered != m_Selected) {
            m_Selected = hovered;
            repaintMenu();
        }
        // The whole pointer belongs to the menu while it is open, otherwise
        // aiming at a button would also be aiming in the game behind it.
        return true;
    }

    bool hot = pointInHandle(m_CursorX, m_CursorY);
    if (hot != m_HandleHot) {
        m_HandleHot = hot;
        repaintHandle();
    }

    // Motion outside the handle is the host's. Swallowing it would make the
    // stream feel broken for the sake of a button nobody is touching.
    return hot;
}

bool Menu::handleMouseButton(int button, bool pressed, int x, int y, bool absolute)
{
    if (absolute) {
        m_CursorX = x;
        m_CursorY = y;
    }

    if (button != SDL_BUTTON_LEFT) {
        // Right-click closes the menu; otherwise non-left buttons are the
        // host's business.
        if (m_MenuOpen && pressed) {
            setMenuOpen(false);
            return true;
        }
        return m_MenuOpen;
    }

    if (pressed) {
        if (pointInHandle(m_CursorX, m_CursorY)) {
            int hx = SDL_clamp((int)(m_HandleX * m_WindowWidth), 0,
                               SDL_max(0, m_WindowWidth - HANDLE_SIZE));
            int hy = SDL_clamp((int)(m_HandleY * m_WindowHeight), 0,
                               SDL_max(0, m_WindowHeight - HANDLE_SIZE));
            m_Dragging = true;
            m_DragMoved = false;
            m_DragOffsetX = m_CursorX - hx;
            m_DragOffsetY = m_CursorY - hy;
            return true;
        }

        if (m_MenuOpen) {
            int item = itemAtPoint(m_CursorX, m_CursorY);
            if (item >= 0) {
                activate(item);
            }
            else {
                // Clicking away closes, which is what every menu does.
                setMenuOpen(false);
            }
            return true;
        }

        return false;
    }

    // Released
    if (m_Dragging) {
        m_Dragging = false;
        if (m_DragMoved) {
            savePosition();
        }
        else {
            // A press and release without movement is a click, not a drag.
            toggleMenu();
        }
        return true;
    }

    return m_MenuOpen;
}

// ----------------------------------------------------------------- drawing

void Menu::drawRect(SDL_Surface* surface, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(surface, &rect, SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
}

void Menu::drawText(SDL_Surface* surface, TTF_Font* font, const char* text,
                    int x, int y, SDL_Color color)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text, color);
    if (rendered == nullptr) {
        return;
    }

    // BLEND rather than the default NONE: the text carries its own alpha
    // and must composite onto the panel instead of stamping transparent
    // pixels over it.
    SDL_SetSurfaceBlendMode(rendered, SDL_BLENDMODE_BLEND);

    SDL_Rect dst = { x, y, rendered->w, rendered->h };
    SDL_BlitSurface(rendered, nullptr, surface, &dst);
    SDL_FreeSurface(rendered);
}

void Menu::repaintHandle()
{
    if (!openFont()) {
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, HANDLE_SIZE, HANDLE_SIZE, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return;
    }

    const bool lit = m_HandleHot || m_MenuOpen;

    drawRect(surface, 0, 0, HANDLE_SIZE, HANDLE_SIZE, lit ? ACCENT : HANDLE_BG);
    if (!lit) {
        drawRect(surface, 0, 0, HANDLE_SIZE, 3, ACCENT);
    }

    // Three bars: a menu affordance that needs no glyph coverage from the
    // font and reads at any size.
    const SDL_Color bar = lit ? TEXT_DARK : TEXT;
    const int barW = HANDLE_SIZE / 2;
    const int barX = (HANDLE_SIZE - barW) / 2;
    for (int i = 0; i < 3; i++) {
        drawRect(surface, barX, 18 + (i * 8), barW, 3, bar);
    }

    m_Manager->setOverlaySurface(OverlayHandle, surface);
}

void Menu::repaintMenu()
{
    if (!m_MenuOpen || !openFont()) {
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, menuWidth(), menuHeight(), 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create Dusk overlay surface: %s", SDL_GetError());
        return;
    }

    // Partial alpha in the panel colour is what lets the stream show
    // through; the renderer blends the surface as a whole.
    drawRect(surface, 0, 0, menuWidth(), menuHeight(), PANEL_BG);
    drawRect(surface, 0, 0, menuWidth(), 3, ACCENT);

    drawText(surface, m_TitleFont, "DUSK", MENU_PADDING, MENU_PADDING - 6, ACCENT);

    for (int i = 0; i < s_ItemCount; i++) {
        const bool selected = (i == m_Selected);
        const int y = TITLE_HEIGHT + i * (ROW_HEIGHT + ROW_GAP);

        drawRect(surface, MENU_PADDING, y, menuWidth() - (MENU_PADDING * 2), ROW_HEIGHT,
                 selected ? ACCENT : ROW_BG);

        drawText(surface, m_Font, s_Items[i].label, MENU_PADDING + 16, y + 14,
                 selected ? TEXT_DARK : TEXT);

        if (!selected) {
            drawText(surface, m_Font, s_Items[i].hint,
                     menuWidth() - MENU_PADDING - 220, y + 14, TEXT_DIM);
        }
    }

    m_Manager->setOverlaySurface(OverlayMenu, surface);
}
