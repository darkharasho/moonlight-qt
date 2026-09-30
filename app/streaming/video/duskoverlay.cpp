#include "duskoverlay.h"
#include "overlaymanager.h"
#include "path.h"
#include "streaming/session.h"

using namespace Overlay;

// Panel geometry, in the overlay surface's own pixels. The renderers scale
// this to the display, so these are design units rather than device pixels.
#define PANEL_WIDTH     560
#define PANEL_PADDING   28
#define TITLE_HEIGHT    64
#define ROW_HEIGHT      52
#define ROW_GAP         4
#define FONT_SIZE       22
#define TITLE_FONT_SIZE 28

// Dusk's accent, so the menu reads as part of the same app as the grid.
static const SDL_Color ACCENT     = {0xE8, 0xB3, 0x3A, 0xFF};
static const SDL_Color PANEL_BG   = {0x14, 0x16, 0x1C, 0xEB};
static const SDL_Color ROW_BG     = {0x1F, 0x22, 0x2B, 0xFF};
static const SDL_Color TEXT       = {0xEC, 0xEC, 0xEF, 0xFF};
static const SDL_Color TEXT_DIM   = {0x8A, 0x8F, 0x9A, 0xFF};
static const SDL_Color TEXT_ON_ACCENT = {0x12, 0x11, 0x0C, 0xFF};

const Menu::Item Menu::s_Items[] = {
    { "Resume",           "Close this menu"          },
    { "Performance stats", "Toggle the stats overlay" },
    { "Toggle fullscreen", ""                         },
    { "Disconnect",        "End the session"          },
};

const int Menu::s_ItemCount = SDL_arraysize(Menu::s_Items);

Menu::Menu(OverlayManager* manager) :
    m_Manager(manager),
    m_Visible(false),
    m_Selected(0),
    m_Font(nullptr),
    m_TitleFont(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
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

bool Menu::openFont()
{
    if (m_Font != nullptr && m_TitleFont != nullptr) {
        return true;
    }

    if (m_FontData.isEmpty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Dusk overlay font failed to load");
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
                    "TTF_OpenFont() failed for Dusk overlay: %s",
                    TTF_GetError());
        return false;
    }

    return true;
}

void Menu::setVisible(bool visible)
{
    if (m_Visible == visible) {
        return;
    }

    m_Visible = visible;
    if (visible) {
        // Always open on the first item. Resuming where the selection was
        // left invites accidentally activating Disconnect on a blind press.
        m_Selected = 0;
    }

    m_Manager->setOverlayState(OverlayMenu, visible);
    if (visible) {
        repaint();
    }
}

bool Menu::handleKeyEvent(const SDL_KeyboardEvent* event)
{
    if (!m_Visible) {
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
        repaint();
        break;

    case SDLK_DOWN:
    case SDLK_j:
        m_Selected = (m_Selected + 1) % s_ItemCount;
        repaint();
        break;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        activateSelected();
        break;

    case SDLK_ESCAPE:
        setVisible(false);
        break;

    default:
        break;
    }

    return true;
}

void Menu::activateSelected()
{
    Session* session = Session::get();

    switch (m_Selected) {
    case 0: // Resume
        setVisible(false);
        break;

    case 1: // Performance stats
        if (session != nullptr) {
            OverlayManager& overlays = session->getOverlayManager();
            overlays.setOverlayState(OverlayDebug, !overlays.isOverlayEnabled(OverlayDebug));
        }
        setVisible(false);
        break;

    case 2: // Toggle fullscreen
        setVisible(false);
        if (Session::s_ActiveSession != nullptr) {
            Session::s_ActiveSession->toggleFullscreen();
        }
        break;

    case 3: // Disconnect
        setVisible(false);
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

void Menu::repaint()
{
    if (!m_Visible) {
        return;
    }

    SDL_Surface* surface = render();
    if (surface != nullptr) {
        m_Manager->setOverlaySurface(OverlayMenu, surface);
    }
}

void Menu::drawRect(SDL_Surface* surface, int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_FillRect(surface, &rect,
                 SDL_MapRGBA(surface->format, color.r, color.g, color.b, color.a));
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

    // BLEND rather than the default NONE: the text carries its own alpha and
    // must composite onto the panel instead of stamping transparent pixels
    // over it.
    SDL_SetSurfaceBlendMode(rendered, SDL_BLENDMODE_BLEND);

    SDL_Rect dst = { x, y, rendered->w, rendered->h };
    SDL_BlitSurface(rendered, nullptr, surface, &dst);
    SDL_FreeSurface(rendered);
}

SDL_Surface* Menu::render()
{
    if (!openFont()) {
        return nullptr;
    }

    const int height = TITLE_HEIGHT + (s_ItemCount * (ROW_HEIGHT + ROW_GAP)) + PANEL_PADDING;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, PANEL_WIDTH, height, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create Dusk overlay surface: %s",
                    SDL_GetError());
        return nullptr;
    }

    // The panel is drawn opaque-per-pixel into an alpha surface; the renderer
    // blends the whole thing, so partial alpha here is what makes the stream
    // show through.
    drawRect(surface, 0, 0, PANEL_WIDTH, height, PANEL_BG);
    drawRect(surface, 0, 0, PANEL_WIDTH, 3, ACCENT);

    drawText(surface, m_TitleFont, "DUSK", PANEL_PADDING, PANEL_PADDING - 6, ACCENT);

    int y = TITLE_HEIGHT;
    for (int i = 0; i < s_ItemCount; i++) {
        const bool selected = (i == m_Selected);

        drawRect(surface, PANEL_PADDING, y,
                 PANEL_WIDTH - (PANEL_PADDING * 2), ROW_HEIGHT,
                 selected ? ACCENT : ROW_BG);

        drawText(surface, m_Font, s_Items[i].label,
                 PANEL_PADDING + 16, y + 14,
                 selected ? TEXT_ON_ACCENT : TEXT);

        if (!selected) {
            drawText(surface, m_Font, s_Items[i].hint,
                     PANEL_WIDTH - PANEL_PADDING - 220, y + 14, TEXT_DIM);
        }

        y += ROW_HEIGHT + ROW_GAP;
    }

    return surface;
}
