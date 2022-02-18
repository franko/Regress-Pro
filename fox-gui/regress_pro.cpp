#include "defs.h"
#include "regress_pro.h"
#include "icons_all.h"

#ifdef WIN32
#define SCRIPT_FONT_NAME "Consolas"
#define LITERATE_FONT_NAME "Segoe UI"
#define BIG_UI_FONT_NAME "Segoe UI"
#define SMALL_UI_FONT_NAME "Helvetica"
#else
#define SCRIPT_FONT_NAME "Monospace"
#define LITERATE_FONT_NAME "Helvetica"
#define BIG_UI_FONT_NAME "Helvetica"
#define SMALL_UI_FONT_NAME "Helvetica"
#endif

FXDEFMAP(regress_pro) regress_pro_map[]= {
};

// Object implementation
FXIMPLEMENT(regress_pro, FX::FXApp, regress_pro_map,ARRAYNUMBER(regress_pro_map));

regress_pro::regress_pro() :
    FX::FXApp("Regress Pro", "Francesco Abbate"),
    appicon(this, regressproicon),
    small_font(this, SMALL_UI_FONT_NAME, 9, FXFont::Normal, FXFont::Straight),
    bold_font(this, SMALL_UI_FONT_NAME, 9, FXFont::Bold, FXFont::Italic),
    lit_font(this, LITERATE_FONT_NAME, 10, FXFont::Normal, FXFont::Straight),
    monospace_font(this, SCRIPT_FONT_NAME, 9),
    big_web_font(this, BIG_UI_FONT_NAME, 14, FXFont::Bold, FXFont::Straight),
    blue_web(FXRGB(100,114,161)), blue_highlight(FXRGB(3,12,180)), red_warning(FXRGB(180,5,10)), black(FXRGB(0,0,0)),
    m_script_mode(false)
{
    delete_icon = new FXGIFIcon(this, delete_gif);
    add_icon = new FXGIFIcon(this, new_gif);
    broom_icon = new FXGIFIcon(this, broom_gif);
}

FXString regress_pro::get_release_string() const {
#ifdef RELEASE_BUILD
    return FXString::value("%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
#else
    return FXString::value("%d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD_EXT);
#endif
}

FXString regress_pro::get_host_string() const {
    return FXString::value("%s %s", HOST_SYSTEM, CPU_FAMILY);
}

regress_pro::~regress_pro()
{
	delete delete_icon;
	delete add_icon;
    delete broom_icon;
}
