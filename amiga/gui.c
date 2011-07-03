/*
 * Copyright 2008-2010 Chris Young <chris@unsatisfactorysoftware.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* NetSurf core includes */
#include "content/urldb.h"
#include "css/utils.h"
#include "desktop/history_core.h"
#include "desktop/mouse.h"
#include "desktop/netsurf.h"
#include "desktop/options.h"
#include "desktop/save_complete.h"
#include "desktop/searchweb.h"
#include "desktop/selection.h"
#include "desktop/textinput.h"
#include "desktop/tree.h"
#include "image/ico.h"
#include "render/form.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/utf8.h"
#include "utils/utils.h"
#include "utils/url.h"
#include "content/fetchers/resource.h"

/* NetSurf Amiga platform includes */
#include "amiga/arexx.h"
#include "amiga/bitmap.h"
#include "amiga/clipboard.h"
#include "amiga/context_menu.h"
#include "amiga/cookies.h"
#include "amiga/datatypes.h"
#include "amiga/download.h"
#include "amiga/drag.h"
#include "amiga/filetype.h"
#include "amiga/font.h"
#include "amiga/gui.h"
#include "amiga/gui_options.h"
#include "amiga/history.h"
#include "amiga/history_local.h"
#include "amiga/hotlist.h"
#include "amiga/icon.h"
#include "amiga/launch.h"
#include "amiga/login.h"
#include "amiga/menu.h"
#include "amiga/misc.h"
#include "amiga/options.h"
#include "amiga/plotters.h"
#include "amiga/plugin_hack.h"
#include "amiga/print.h"
#include "amiga/schedule.h"
#include "amiga/search.h"
#include "amiga/theme.h"
#include "amiga/tree.h"
#include "amiga/utf8.h"

/* Custom StringView class */
#include "amiga/stringview/stringview.h"
#include "amiga/stringview/urlhistory.h"

/* AmigaOS libraries */
#ifdef __amigaos4__
#include <proto/application.h>
#endif
#include <proto/asl.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/locale.h>
#include <proto/Picasso96API.h>
#include <proto/utility.h>
#include <proto/wb.h>

/* Other OS includes */
#include <datatypes/textclass.h>
#include <devices/inputevent.h>
#ifdef __amigaos4__
#include <graphics/blitattr.h>
#include <libraries/application.h>
#include <libraries/keymap.h>
#endif
#include <libraries/gadtools.h>
#include <intuition/icclass.h>
#include <graphics/rpattr.h>
#include <workbench/workbench.h>

/* ReAction libraries */
#include <proto/bevel.h>
#include <proto/bitmap.h>
#include <proto/button.h>
#include <proto/clicktab.h>
#include <proto/layout.h>
#include <proto/space.h>
#include <proto/string.h>
#include <proto/window.h>

#include <classes/window.h>
#include <gadgets/button.h>
#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/scroller.h>
#include <gadgets/space.h>
#include <gadgets/string.h>
#include <images/bevel.h>
#include <images/bitmap.h>

#include <reaction/reaction_macros.h>

/* newlib includes */
#include <math.h>
#include <string.h>

/* Extra mouse button defines to match those in intuition/intuition.h */
#define SIDEDOWN  (IECODE_4TH_BUTTON)
#define SIDEUP    (IECODE_4TH_BUTTON | IECODE_UP_PREFIX)
#define EXTRADOWN (IECODE_5TH_BUTTON)
#define EXTRAUP   (IECODE_5TH_BUTTON | IECODE_UP_PREFIX)

char *default_stylesheet_url;
char *quirks_stylesheet_url;
char *adblock_stylesheet_url;

struct MsgPort *appport;
struct Library  *KeymapBase = NULL;
struct KeymapIFace *IKeymap = NULL;
struct Library *ApplicationBase=NULL;
struct ApplicationIFace *IApplication=NULL;

Class *urlStringClass;

BOOL locked_screen = FALSE;
BOOL screen_closed = FALSE;
ULONG screen_signal = -1;
struct MsgPort *applibport = NULL;
ULONG applibsig = 0;

const char tree_directory_icon_name[] = "def_drawer.info";
const char tree_content_icon_name[] = "def_project.info";
extern colour scrollbar_widget_fg_colour;
extern colour scrollbar_widget_bg_colour;
extern colour scrollbar_widget_arrow_colour;

static struct DrawInfo *dri;

void ami_update_buttons(struct gui_window_2 *);
void ami_scroller_hook(struct Hook *,Object *,struct IntuiMessage *);
void ami_switch_tab(struct gui_window_2 *gwin,bool redraw);
void ami_change_tab(struct gui_window_2 *gwin, int direction);
void ami_get_hscroll_pos(struct gui_window_2 *gwin, ULONG *xs);
void ami_get_vscroll_pos(struct gui_window_2 *gwin, ULONG *ys);
ULONG ami_set_border_gadget_balance(struct gui_window_2 *gwin);
ULONG ami_get_border_gadget_balance(struct gui_window_2 *gwin, ULONG *size1, ULONG *size2);
void ami_try_quit(void);
void ami_do_redraw_limits(struct gui_window *g, struct browser_window *bw,
		int x0, int y0, int x1, int y1);
Object *ami_gui_splash_open(void);
void ami_gui_splash_close(Object *win_obj);

STRPTR ami_locale_langs(void)
{
	struct Locale *locale;
	STRPTR acceptlangs = NULL, acceptlangs2 = NULL;
	int i;

	if(locale = OpenLocale(NULL))
	{
		for(i=0;i<10;i++)
		{
			if(locale->loc_PrefLanguages[i])
			{
				if(messages_get(locale->loc_PrefLanguages[i]) != locale->loc_PrefLanguages[i])
				{
					if(acceptlangs)
					{
						acceptlangs2 = acceptlangs;
						acceptlangs = ASPrintf("%s, %s",acceptlangs2,messages_get(locale->loc_PrefLanguages[i]));
						FreeVec(acceptlangs2);
						acceptlangs2 = NULL;
					}
					else
					{
						acceptlangs = ASPrintf("%s",messages_get(locale->loc_PrefLanguages[i]));
					}
				}
			}
			else
			{
				continue;
			}
		}
		CloseLocale(locale);
	}
	return acceptlangs;
}

bool ami_locate_resource(char *lang, char *file)
{
	struct Locale *locale;
	int i;
	BPTR lock = 0;
	bool found = false;

	if(lock=Lock("PROGDIR:Resources/LangNames",ACCESS_READ))
	{
		UnLock(lock);
		messages_load("PROGDIR:Resources/LangNames");
	}

	locale = OpenLocale(NULL);

	for(i=0;i<10;i++)
	{
		strcpy(lang,"PROGDIR:Resources/");
		if(locale->loc_PrefLanguages[i])
		{
			strcat(lang,messages_get(locale->loc_PrefLanguages[i]));
		}
		else
		{
			continue;
		}
		strcat(lang, "/");
		strcat(lang, file);

		if(lock=Lock(lang,ACCESS_READ))
		{
			UnLock(lock);
			found = true;
			break;
		}
	}

	if(!found)
	{
		/* If not found yet, check in PROGDIR:Resources/en,
		 * might not be in user's preferred languages */

		strcpy(lang, "PROGDIR:Resources/en/");
		strcat(lang, file);

		if(lock=Lock(lang, ACCESS_READ))
		{
			UnLock(lock);
			found = true;
		}
		else found = false;
	}

	CloseLocale(locale);

	if(!found)
	{
		/* Lastly check directly in PROGDIR:Resources */

		strcpy(lang, "PROGDIR:Resources/");
		strcat(lang, file);

		if(lock=Lock(lang, ACCESS_READ))
		{
			UnLock(lock);
			found = true;
		}
		else found = false;
	}

	return found;
}

void ami_open_resources(void)
{
	/* Allocate ports/ASL and open libraries and devices */

	if(KeymapBase = OpenLibrary("keymap.library",37))
	{
		IKeymap = (struct KeymapIFace *)GetInterface(KeymapBase,"main",1,NULL);
	}

	if(ApplicationBase = OpenLibrary("application.library",50))
	{
		IApplication = (struct ApplicationIFace *)GetInterface(ApplicationBase,"application",1,NULL);
	}

	urlStringClass = MakeStringClass();

    if(!(appport = AllocSysObjectTags(ASOT_PORT,
							ASO_NoTrack,FALSE,
							TAG_DONE))) die(messages_get("NoMemory"));

    if(!(sport = AllocSysObjectTags(ASOT_PORT,
							ASO_NoTrack,FALSE,
							TAG_DONE))) die(messages_get("NoMemory"));

	filereq = (struct FileRequester *)AllocAslRequest(ASL_FileRequest,NULL);
	savereq = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
							ASLFR_DoSaveMode,TRUE,
							ASLFR_RejectIcons,TRUE,
							ASLFR_InitialDrawer,option_download_dir,
							TAG_DONE);

	screen_signal = AllocSignal(-1);
}

void ami_set_options(void)
{
	STRPTR tempacceptlangs;
	BPTR lock = 0;

	/* The following line disables the popupmenu.class select menu
	** This will become a user option when/if popupmenu.class is
	** updated to show more items than can fit in one column vertically
	*/

	option_core_select_menu = true;

	#ifndef NS_AMIGA_CAIRO
	/* Ensure we get some output when Cairo not available */
	option_cairo_renderer = 0;
	#endif

	if((!option_accept_language) || (option_accept_language[0] == '\0'))
	{
		if(tempacceptlangs = ami_locale_langs())
		{
			option_accept_language = (char *)strdup(tempacceptlangs);
			FreeVec(tempacceptlangs);
		}
	}

	if((!option_cookie_file) || (option_cookie_file[0] == '\0'))
		option_cookie_file = (char *)strdup("PROGDIR:Resources/Cookies");

	if((!option_hotlist_file) || (option_hotlist_file[0] == '\0'))
		option_hotlist_file = (char *)strdup("PROGDIR:Resources/Hotlist");

	if((!option_url_file) || (option_url_file[0] == '\0'))
		option_url_file = (char *)strdup("PROGDIR:Resources/URLs");

/*
	if((!option_cookie_jar) || (option_cookie_jar[0] == '\0'))
		option_cookie_jar = (char *)strdup("PROGDIR:Resources/CookieJar");
*/

/* devs:curl-ca-bundle.crt is the default place for the ca bundle on OS4,
 * but we can't rely on it existing, so default to our local one in resources */

	if((!option_ca_bundle) || (option_ca_bundle[0] == '\0'))
		option_ca_bundle = (char *)strdup("PROGDIR:Resources/ca-bundle");

	if((!option_search_engines_file) || (option_search_engines_file[0] == '\0'))
		option_search_engines_file = (char *)strdup("PROGDIR:Resources/SearchEngines");

	search_engines_file_location = option_search_engines_file;

	if((!option_search_ico_file) || (option_search_ico_file[0] == '\0'))
		option_search_ico_file = (char *)strdup("PROGDIR:Resources/default.ico");

	search_default_ico_location = option_search_ico_file;

	if((!option_font_sans) || (option_font_sans[0] == '\0'))
		option_font_sans = (char *)strdup("DejaVu Sans");

	if((!option_font_serif) || (option_font_serif[0] == '\0'))
		option_font_serif = (char *)strdup("DejaVu Serif");

	if((!option_font_mono) || (option_font_mono[0] == '\0'))
		option_font_mono = (char *)strdup("DejaVu Sans Mono");

	if((!option_font_cursive) || (option_font_cursive[0] == '\0'))
		option_font_cursive = (char *)strdup("DejaVu Sans");

	if((!option_font_fantasy) || (option_font_fantasy[0] == '\0'))
		option_font_fantasy = (char *)strdup("DejaVu Serif");

	if((!option_font_unicode) || (option_font_unicode[0] == '\0'))
	{
		/* Search for some likely candidates */

		if(lock=Lock("FONTS:Code2000.font",ACCESS_READ))
		{
			UnLock(lock);
			option_font_unicode = (char *)strdup("Code2000");
		}
		else if(lock=Lock("FONTS:Bitstream Cyberbit.font",ACCESS_READ))
		{
			UnLock(lock);
			option_font_unicode = (char *)strdup("Bitstream Cyberbit");
		}
		else option_font_unicode = (char *)strdup("DejaVu Sans");
	}

	if((!option_theme) || (option_theme[0] == '\0'))
		option_theme = (char *)strdup("PROGDIR:Resources/Themes/Default");

	tree_set_icon_dir(strdup("ENV:Sys"));

	if((!option_arexx_dir) || (option_arexx_dir[0] == '\0'))
		option_arexx_dir = (char *)strdup("Rexx");

	if((!option_arexx_startup) || (option_arexx_startup[0] == '\0'))
		option_arexx_startup = (char *)strdup("Startup.nsrx");

	if((!option_arexx_shutdown) || (option_arexx_shutdown[0] == '\0'))
		option_arexx_shutdown = (char *)strdup("Shutdown.nsrx");

	if(!option_window_width) option_window_width = 800;
	if(!option_window_height) option_window_height = 600;
}

void ami_amiupdate(void)
{
	/* Create AppPath location for AmiUpdate use */

	BPTR lock = 0, amiupdatefh = 0;

	if(((lock = Lock("ENVARC:AppPaths",SHARED_LOCK)) == 0))
	{
		lock = CreateDir("ENVARC:AppPaths");
	}
	
	UnLock(lock);

	if(lock = Lock("PROGDIR:", ACCESS_READ))
	{
		char filename[1024];

		DevNameFromLock(lock,(STRPTR)&filename,1024L,DN_FULLPATH);

		amiupdatefh = FOpen("ENVARC:AppPaths/NetSurf",MODE_NEWFILE,0);
		FPuts(amiupdatefh,(CONST_STRPTR)&filename);
		FClose(amiupdatefh);
		UnLock(lock);
	}
}

char* gui_get_resource_url(const char *filename)
{
	char path[1024];
	char filename2[1024];

	if(ami_locate_resource(path, filename) == false)
	{
		if((strncmp(filename + strlen(filename) - 4, ".htm", 4) == 0) ||
			(strncmp(filename + strlen(filename) - 5, ".html", 5) == 0))
		{
			/* Try with RISC OS HTML filetype, might work */
			strcpy(filename2, filename);
			strcat(filename2, ",faf");

			if(ami_locate_resource(path, filename2) == false)
			{
				return NULL;
			}
		}
		else return NULL;
	}

	return path_to_url(path);
}

void gui_init(int argc, char** argv)
{
	BPTR lock = 0;

	ami_open_resources(); /* alloc ports/asl reqs, open libraries/devices */
	ami_print_init();
	ami_clipboard_init();
	ami_openurl_open();

	ami_set_options(); /* check options and set defaults where required */

	win_destroyed = false;
	nsscreentitle = ASPrintf("NetSurf %s",netsurf_version);

	default_stylesheet_url = "file:///PROGDIR:Resources/amiga.css";
	quirks_stylesheet_url = "file:///PROGDIR:Resources/quirks.css";
	adblock_stylesheet_url = "file:///PROGDIR:Resources/adblock.css";

	ami_font_setdevicedpi(0); /* for early font requests, eg treeview init */

	scrollbar_widget_fg_colour = 0x00aaaaaa;
	scrollbar_widget_bg_colour = 0x00833c3c;
	scrollbar_widget_arrow_colour = 0x00d6d6d6;

	ami_amiupdate(); /* set env-vars for AmiUpdate */
	ami_init_fonts();

	ami_context_menu_init();

	window_list = NewObjList();

	urldb_load(option_url_file);
	urldb_load_cookies(option_cookie_file);

	save_complete_init();
	ami_theme_init();
	ami_init_mouse_pointers();
	ami_theme_throbber_setup();
}

void ami_openscreen(void)
{
	ULONG id = 0;
	if(!option_use_pubscreen || option_use_pubscreen[0] == '\0')
	{
		if((option_modeid) && (strncmp(option_modeid,"0x",2) == 0))
		{
			id = strtoul(option_modeid,NULL,0);
		}
		else
		{
			struct ScreenModeRequester *screenmodereq = NULL;

			if(screenmodereq = AllocAslRequest(ASL_ScreenModeRequest,NULL))
			{
				if(AslRequestTags(screenmodereq,
						ASLSM_MinDepth,16,
						ASLSM_MaxDepth,32,
						TAG_DONE))
				{
					id = screenmodereq->sm_DisplayID;
					option_modeid = malloc(20);
					sprintf(option_modeid,"0x%lx",id);
					options_write("PROGDIR:Resources/Options");
				}
				FreeAslRequest(screenmodereq);
			}
		}

		scrn = OpenScreenTags(NULL,
					SA_DisplayID, id,
					SA_Title, nsscreentitle,
					SA_Type, PUBLICSCREEN,
					SA_PubName, "NetSurf",
					SA_LikeWorkbench, TRUE,
					SA_PubSig, screen_signal,
					TAG_DONE);

		if(scrn)
		{
			PubScreenStatus(scrn,0);
		}
		else
		{
			if(scrn = LockPubScreen("NetSurf"))
			{
				locked_screen = TRUE;
			}
			else
			{
				option_use_pubscreen = strdup("Workbench");
			}
		}
	}

	if(option_use_pubscreen && option_use_pubscreen[0] != '\0')
	{
		scrn = LockPubScreen(option_use_pubscreen);

		if(scrn == NULL)
		{
			scrn = LockPubScreen("Workbench");
		}
		locked_screen = TRUE;
	}
	dri = GetScreenDrawInfo(scrn);
	ami_font_setdevicedpi(id);

	gui_system_colour_finalize();
	gui_system_colour_init();
}

void ami_openscreenfirst(void)
{
	ami_openscreen();
	if(!browserglob.bm) ami_init_layers(&browserglob, 0, 0);
}

static void gui_init2(int argc, char** argv)
{
	struct browser_window *bw = NULL;
	struct RDArgs *args;
	STRPTR temp_homepage_url = NULL;
	BOOL notalreadyrunning;
	STRPTR template = "NSOPTS/M,URL/K,FORCE/S";
	long rarray[] = {0,0,0};
	enum
	{
		A_NSOPTS, /* ignored */
		A_URL,
		A_FORCE
	};

	notalreadyrunning = ami_arexx_init();

	/* Treeview init code ends up calling a font function which needs this */
	glob = &browserglob;
	/**/

	ami_hotlist_initialise(option_hotlist_file);
	ami_cookies_initialise();
	ami_global_history_initialise();
	sslcert_init(tree_content_icon_name);

	search_web_provider_details(option_search_provider);

	if(argc) // argc==0 is started from wb
	{
		if(args = ReadArgs(template,rarray,NULL))
		{
			if(notalreadyrunning && (option_startup_no_window == false))
				ami_openscreenfirst();

			if(rarray[A_URL])
			{
				temp_homepage_url = (char *)strdup(rarray[A_URL]);

				if(notalreadyrunning)
				{
					bw = browser_window_create(temp_homepage_url, 0, 0, true,false);
					free(temp_homepage_url);
				}
			}

			if(rarray[A_FORCE])
			{
				notalreadyrunning = TRUE;
			}

			FreeArgs(args);
		}
	}
	else
	{
		struct WBStartup *WBenchMsg = (struct WBStartup *)argv;
		struct WBArg *wbarg;
		int first=0,i=0;
		char fullpath[1024];

		if(notalreadyrunning && (option_startup_no_window == false))
			ami_openscreenfirst();

		for(i=0,wbarg=WBenchMsg->sm_ArgList;i<WBenchMsg->sm_NumArgs;i++,wbarg++)
		{
			if(i==0) continue;
			if((wbarg->wa_Lock)&&(*wbarg->wa_Name))
			{
				DevNameFromLock(wbarg->wa_Lock,fullpath,1024,DN_FULLPATH);
				AddPart(fullpath,wbarg->wa_Name,1024);

				if(!temp_homepage_url) temp_homepage_url = path_to_url(fullpath);

				if(notalreadyrunning)
				{
					if(!first)
					{
						bw = browser_window_create(temp_homepage_url, 0, 0, true,false);
 						first=1;
					}
					else
					{
						bw = browser_window_create(temp_homepage_url, bw, 0, true,false);
					}
					free(temp_homepage_url);
					temp_homepage_url = NULL;
				}
			}
		}
	}

	if ((!option_homepage_url) || (option_homepage_url[0] == '\0'))
    	option_homepage_url = (char *)strdup(NETSURF_HOMEPAGE);

	if(!notalreadyrunning)
	{
		STRPTR sendcmd = NULL;

		if(temp_homepage_url)
		{
			sendcmd = ASPrintf("OPEN \"%s\" NEW",temp_homepage_url);
			free(temp_homepage_url);
		}
		else
		{
			sendcmd = ASPrintf("OPEN \"%s\" NEW",option_homepage_url);
		}
		IDoMethod(arexx_obj,AM_EXECUTE,sendcmd,"NETSURF",NULL,NULL,NULL,NULL);
		IDoMethod(arexx_obj,AM_EXECUTE,"TOFRONT","NETSURF",NULL,NULL,NULL,NULL);
		FreeVec(sendcmd);

		netsurf_quit=true;
		return;
	}

	if(IApplication)
	{
		ULONG desc = REGAPP_Description;
		if(ApplicationBase->lib_Version < 53) desc = TAG_IGNORE;

		if(argc == 0)
		{
			ULONG noicon = TAG_IGNORE;

			if(option_hide_docky_icon) noicon = REGAPP_NoIcon;

			ami_appid = RegisterApplication(messages_get("NetSurf"),
				REGAPP_URLIdentifier, "netsurf-browser.org",
				REGAPP_WBStartup, (struct WBStartup *)argv,
				noicon, TRUE,
				REGAPP_HasPrefsWindow, TRUE,
				REGAPP_CanCreateNewDocs, TRUE,
				REGAPP_UniqueApplication, TRUE,
				desc, "Small as a mouse, fast as a cheetah and available for free. NetSurf is a multi-platform web browser.",
				TAG_DONE);
		}
		else
		{
/* TODO: Specify icon when run from Shell */
			ami_appid = RegisterApplication(messages_get("NetSurf"),
				REGAPP_URLIdentifier, "netsurf-browser.org",
				REGAPP_FileName, argv[0],
				REGAPP_NoIcon, TRUE,
				REGAPP_HasPrefsWindow, TRUE,
				REGAPP_CanCreateNewDocs, TRUE,
				REGAPP_UniqueApplication, TRUE,
				desc, "Small as a mouse, fast as a cheetah and available for free. NetSurf is a multi-platform web browser.",
				TAG_DONE);
		}

		GetApplicationAttrs(ami_appid, APPATTR_Port, (ULONG)&applibport, TAG_DONE);
		if(applibport) applibsig = (1L << applibport->mp_SigBit);
	}

	if(!bw && (option_startup_no_window == false))
		bw = browser_window_create(option_homepage_url, 0, 0, true,false);
}

/** Normal entry point from OS */
int main(int argc, char** argv)
{
	setbuf(stderr, NULL);
	char messages[100];
	char script[1024];
	
	Object *splash_window = ami_gui_splash_open();

	if(ami_locate_resource(messages, "Messages") == false)
		die("Cannot open Messages file");

	ami_mime_init("PROGDIR:Resources/mimetypes");
	ami_schedule_open_timer();
	ami_schedule_create();

	amiga_plugin_hack_init();
	amiga_datatypes_init();

	netsurf_init(&argc, &argv, "PROGDIR:Resources/Options", messages);

	amiga_icon_init();

	gui_init(argc, argv);
	gui_init2(argc, argv);

	ami_gui_splash_close(splash_window);

	strncpy(script, option_arexx_dir, 1024);
	AddPart(script, option_arexx_startup, 1024);
	ami_arexx_execute(script);

	netsurf_main_loop();

	strncpy(script, option_arexx_dir, 1024);
	AddPart(script, option_arexx_shutdown, 1024);
	ami_arexx_execute(script);

	netsurf_exit();

	amiga_plugin_hack_fini();
	amiga_datatypes_fini();
	amiga_icon_fini();

	ami_mime_free();

	return 0;
}

void ami_gui_history(struct gui_window_2 *gwin, bool back)
{
	if(back == true)
	{
		if(browser_window_back_available(gwin->bw))
			history_back(gwin->bw, gwin->bw->history);
	}
	else
	{
		if(browser_window_forward_available(gwin->bw))
			history_forward(gwin->bw, gwin->bw->history);
	}

	ami_update_buttons(gwin);
}

int ami_key_to_nskey(ULONG keycode, struct InputEvent *ie)
{
	int nskey = 0, chars;
	UBYTE buffer[20];

	if(keycode >= IECODE_UP_PREFIX) return 0;

	switch(keycode)
	{
		case RAWKEY_CRSRUP:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_PAGE_UP;
			}
			else if(ie->ie_Qualifier & IEQUALIFIER_RALT)
			{
				nskey = KEY_TEXT_START;
			}
			else nskey = KEY_UP;
		break;
		case RAWKEY_CRSRDOWN:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_PAGE_DOWN;
			}
			else if(ie->ie_Qualifier & IEQUALIFIER_RALT)
			{
				nskey = KEY_TEXT_END;
			}
			else nskey = KEY_DOWN;
		break;
		case RAWKEY_CRSRLEFT:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_LINE_START;
			}
			else if(ie->ie_Qualifier & IEQUALIFIER_RALT)
			{
				nskey = KEY_WORD_LEFT;
			}
			else nskey = KEY_LEFT;
		break;
		case RAWKEY_CRSRRIGHT:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_LINE_END;
			}
			else if(ie->ie_Qualifier & IEQUALIFIER_RALT)
			{
				nskey = KEY_WORD_RIGHT;
			}
			else nskey = KEY_RIGHT;
		break;
		case RAWKEY_ESC:
			nskey = KEY_ESCAPE;
		break;
		case RAWKEY_PAGEUP:
			nskey = KEY_PAGE_UP;
		break;
		case RAWKEY_PAGEDOWN:
			nskey = KEY_PAGE_DOWN;
		break;
		case RAWKEY_HOME:
			nskey = KEY_TEXT_START;
		break;
		case RAWKEY_END:
			nskey = KEY_TEXT_END;
		break;
		case RAWKEY_BACKSPACE:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_DELETE_LINE_START;
			}
			else nskey = KEY_DELETE_LEFT;
		break;
		case RAWKEY_DEL:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_DELETE_LINE_END;
			}
			else nskey = KEY_DELETE_RIGHT;
		break;
		case RAWKEY_TAB:
			if(ie->ie_Qualifier & IEQUALIFIER_RSHIFT)
			{
				nskey = KEY_SHIFT_TAB;
			}
			else nskey = KEY_TAB;
		break;
		case RAWKEY_F5:
			// don't translate
			nskey = keycode;
		break;
		default:
			if((chars = MapRawKey(ie,buffer,20,NULL)) > 0)
			{
				nskey = buffer[0];
			}
		break;
	}
	return nskey;
}

void ami_update_quals(struct gui_window_2 *gwin)
{
	uint32 quals = 0;

	GetAttr(WINDOW_Qualifier,gwin->objects[OID_MAIN],(uint32 *)&quals);

	gwin->key_state = 0;

	if((quals & IEQUALIFIER_LSHIFT) || (quals & IEQUALIFIER_RSHIFT)) 
	{
		gwin->key_state |= BROWSER_MOUSE_MOD_1;
	}

	if(quals & IEQUALIFIER_CONTROL) 
	{
		gwin->key_state |= BROWSER_MOUSE_MOD_2;
	}

	if((quals & IEQUALIFIER_LALT) || (quals & IEQUALIFIER_RALT)) 
	{
		gwin->key_state |= BROWSER_MOUSE_MOD_3;
	}
}

void ami_handle_msg(void)
{
	struct IntuiMessage *message = NULL;
	ULONG class,result,storage = 0,x,y,xs,ys,width=800,height=600;
	uint16 code,quals;
	struct IBox *bbox;
	struct nsObject *node;
	struct nsObject *nnode;
	struct gui_window_2 *gwin = NULL;
	struct MenuItem *item;
	struct InputEvent *ie;
	struct Node *tabnode;
	int i, nskey;
	struct browser_window *closedbw;
	struct timeval curtime;
	static int drag_x_move = 0, drag_y_move = 0;

	if(IsMinListEmpty(window_list))
	{
		/* no windows in list, so NetSurf should not be running */
		ami_try_quit();
		return;
	}

	node = (struct nsObject *)GetHead((struct List *)window_list);

	do
	{
		nnode=(struct nsObject *)GetSucc((struct Node *)node);

		gwin = node->objstruct;

		if(node->Type == AMINS_TVWINDOW)
		{
			if(ami_tree_event((struct treeview_window *)gwin))
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_FINDWINDOW)
		{
			if(ami_search_event())
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_HISTORYWINDOW)
		{
			if(ami_history_event((struct history_window *)gwin))
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_PRINTWINDOW)
		{
			if(ami_print_event((struct ami_print_window *)gwin))
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_GUIOPTSWINDOW)
		{
			if(ami_gui_opts_event())
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit with conditions ;) */
					if(scrn) ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_DLWINDOW)
		{
			if(ami_download_window_event((struct gui_download_window *)gwin))
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}
		else if(node->Type == AMINS_LOGINWINDOW)
		{
			if(ami_401login_event((struct gui_login_window *)gwin))
			{
				if(IsMinListEmpty(window_list))
				{
					/* last window closed, so exit */
					ami_try_quit();
				}
				break;
			}
			else
			{
				node = nnode;
				continue;
			}
		}

		while((result = RA_HandleInput(gwin->objects[OID_MAIN],&code)) != WMHI_LASTMSG)
		{

//printf("class %ld\n",class);
	        switch(result & WMHI_CLASSMASK) // class
   		   	{
				case WMHI_MOUSEMOVE:
					drag_x_move = 0;
					drag_y_move = 0;

					GetAttr(SPACE_AreaBox, (Object *)gwin->objects[GID_BROWSER],
							(ULONG *)&bbox);

					x = (ULONG)((gwin->win->MouseX - bbox->Left) / gwin->bw->scale);
					y = (ULONG)((gwin->win->MouseY - bbox->Top) / gwin->bw->scale);

					ami_get_hscroll_pos(gwin, (ULONG *)&xs);
					ami_get_vscroll_pos(gwin, (ULONG *)&ys);

					x += xs;
					y += ys;

					width=bbox->Width;
					height=bbox->Height;

					if(gwin->mouse_state & BROWSER_MOUSE_DRAG_ON &&
						((gwin->bw->drag_type == DRAGGING_SELECTION) ||
						ami_autoscroll == TRUE))
					{
						ami_drag_icon_move();

						if((gwin->win->MouseX < bbox->Left) &&
							((gwin->win->MouseX - bbox->Left) > -AMI_DRAG_THRESHOLD))
							drag_x_move = gwin->win->MouseX - bbox->Left;
						if((gwin->win->MouseX > (bbox->Left + bbox->Width)) &&
							((gwin->win->MouseX - (bbox->Left + bbox->Width)) < AMI_DRAG_THRESHOLD))
							drag_x_move = gwin->win->MouseX - (bbox->Left + bbox->Width);
						if((gwin->win->MouseY < bbox->Top) &&
							((gwin->win->MouseY - bbox->Top) > -AMI_DRAG_THRESHOLD))
							drag_y_move = gwin->win->MouseY - bbox->Top;
						if((gwin->win->MouseY > (bbox->Top + bbox->Height)) &&
							((gwin->win->MouseY - (bbox->Top + bbox->Height)) < AMI_DRAG_THRESHOLD))
							drag_y_move = gwin->win->MouseY - (bbox->Top + bbox->Height);
					}

					if((x>=xs) && (y>=ys) && (x<width+xs) && (y<height+ys))
					{
						ami_update_quals(gwin);
						ami_context_menu_mouse_trap(gwin, TRUE);

						if(gwin->mouse_state & BROWSER_MOUSE_PRESS_1)
						{
							browser_window_mouse_track(gwin->bw,BROWSER_MOUSE_DRAG_1 | gwin->key_state,x,y);
							gwin->mouse_state = BROWSER_MOUSE_HOLDING_1 | BROWSER_MOUSE_DRAG_ON;
						}
						else if(gwin->mouse_state & BROWSER_MOUSE_PRESS_2)
						{
							browser_window_mouse_track(gwin->bw,BROWSER_MOUSE_DRAG_2 | gwin->key_state,x,y);
							gwin->mouse_state = BROWSER_MOUSE_HOLDING_2 | BROWSER_MOUSE_DRAG_ON;
						}
						else
						{
							browser_window_mouse_track(gwin->bw,gwin->mouse_state | gwin->key_state,x,y);
						}
					}
					else
					{
						ami_context_menu_mouse_trap(gwin, FALSE);

						if(!gwin->mouse_state)	ami_update_pointer(gwin->win,GUI_POINTER_DEFAULT);
					}
				break;

				case WMHI_MOUSEBUTTONS:
					GetAttr(SPACE_AreaBox, (Object *)gwin->objects[GID_BROWSER],
								(ULONG *)&bbox);

					x = (ULONG)((gwin->win->MouseX - bbox->Left) / gwin->bw->scale);
					y = (ULONG)((gwin->win->MouseY - bbox->Top) / gwin->bw->scale);

					ami_get_hscroll_pos(gwin, (ULONG *)&xs);
					ami_get_vscroll_pos(gwin, (ULONG *)&ys);

					x += xs;
					y += ys;

					width=bbox->Width;
					height=bbox->Height;

					ami_update_quals(gwin);

					if((x>=xs) && (y>=ys) && (x<width+xs) && (y<height+ys))
					{
						//code = code>>16;
						switch(code)
						{
							case SELECTDOWN:
								browser_window_mouse_click(gwin->bw,BROWSER_MOUSE_PRESS_1 | gwin->key_state,x,y);
								gwin->mouse_state=BROWSER_MOUSE_PRESS_1;
							break;
							case MIDDLEDOWN:
								browser_window_mouse_click(gwin->bw,BROWSER_MOUSE_PRESS_2 | gwin->key_state,x,y);
								gwin->mouse_state=BROWSER_MOUSE_PRESS_2;
							break;
						}
					}

					if(x<xs) x=xs;
					if(y<ys) y=ys;
					if(x>=width+xs) x=width+xs-1;
					if(y>=height+ys) y=height+ys-1;

					switch(code)
					{
						case MENUDOWN:
							if(!option_sticky_context_menu)
								ami_context_menu_show(gwin,x,y);
						break;

						case MENUUP:
							if(option_sticky_context_menu)
								ami_context_menu_show(gwin,x,y);
						break;

						case SELECTUP:
							if(gwin->mouse_state & BROWSER_MOUSE_PRESS_1)
							{
								CurrentTime(&curtime.tv_sec, &curtime.tv_usec);

								gwin->mouse_state = BROWSER_MOUSE_CLICK_1;

								if(gwin->lastclick.tv_sec)
								{
									if(DoubleClick(gwin->lastclick.tv_sec,
												gwin->lastclick.tv_usec,
												curtime.tv_sec, curtime.tv_usec))
										gwin->mouse_state |= BROWSER_MOUSE_DOUBLE_CLICK;
								}

								browser_window_mouse_click(gwin->bw,
									gwin->mouse_state | gwin->key_state,x,y);

								if(gwin->mouse_state & BROWSER_MOUSE_DOUBLE_CLICK)
								{
									gwin->lastclick.tv_sec = 0;
									gwin->lastclick.tv_usec = 0;
								}
								else
								{
									gwin->lastclick.tv_sec = curtime.tv_sec;
									gwin->lastclick.tv_usec = curtime.tv_usec;
								}
							}
							else
							{
								browser_window_mouse_track(gwin->bw, 0, x, y);
							}
							gwin->mouse_state=0;
						break;

						case MIDDLEUP:
							if(gwin->mouse_state & BROWSER_MOUSE_PRESS_2)
							{
								CurrentTime(&curtime.tv_sec, &curtime.tv_usec);

								gwin->mouse_state = BROWSER_MOUSE_CLICK_2;

								if(gwin->lastclick.tv_sec)
								{
									if(DoubleClick(gwin->lastclick.tv_sec,
												gwin->lastclick.tv_usec,
												curtime.tv_sec, curtime.tv_usec))
										gwin->mouse_state |= BROWSER_MOUSE_DOUBLE_CLICK;
								}

								browser_window_mouse_click(gwin->bw,
									gwin->mouse_state | gwin->key_state,x,y);

								if(gwin->mouse_state & BROWSER_MOUSE_DOUBLE_CLICK)
								{
									gwin->lastclick.tv_sec = 0;
									gwin->lastclick.tv_usec = 0;
								}
								else
								{
									gwin->lastclick.tv_sec = curtime.tv_sec;
									gwin->lastclick.tv_usec = curtime.tv_usec;
								}
							}
							else
							{
								browser_window_mouse_track(gwin->bw, 0, x, y);
							}
							gwin->mouse_state=0;
						break;

						case SIDEUP:
							ami_gui_history(gwin, true);
						break;

						case SIDEDOWN:
							ami_gui_history(gwin, false);
						break;
					}

					if(drag_save_data && !gwin->mouse_state)
						ami_drag_save(gwin->win);
				break;

				case WMHI_GADGETUP:
					switch(result & WMHI_GADGETMASK)
					{
						case GID_TABS:
							GetAttrs(gwin->objects[GID_TABS],
								CLICKTAB_NodeClosed, &tabnode, TAG_DONE);
							if(tabnode)
							{
								GetClickTabNodeAttrs(tabnode,
									TNA_UserData, &closedbw,
									TAG_DONE);

								browser_window_destroy(closedbw);
							}
							else
							{
								ami_switch_tab(gwin, true);
							}
						break;

						case GID_CLOSETAB:
							browser_window_destroy(gwin->bw);
						break;

						case GID_ADDTAB:
							browser_window_create(option_homepage_url, gwin->bw, 0, true, true);
						break;

						case GID_URL:
							GetAttr(STRINGA_TextVal,
								(Object *)gwin->objects[GID_URL],
								(ULONG *)&storage);
							if(search_is_url((char *)storage) == false)
							{
								storage = (ULONG *)search_web_from_term((char *)storage);
							}

							browser_window_go(gwin->bw,(char *)storage, NULL, true);
						break;

						case GID_TOOLBARLAYOUT:
							/* Need fixing: never gets here */
							search_web_retrieve_ico(false);
						break;

						case GID_SEARCHSTRING:
							GetAttr(STRINGA_TextVal,
								(Object *)gwin->objects[GID_SEARCHSTRING],
								(ULONG *)&storage);
							storage = (ULONG *)search_web_from_term((char *)storage);

							browser_window_go(gwin->bw,(char *)storage, NULL, true);
						break;

						case GID_HOME:
							browser_window_go(gwin->bw,option_homepage_url,NULL,true);	
						break;

						case GID_STOP:
							if(browser_window_stop_available(gwin->bw))
								browser_window_stop(gwin->bw);
						break;

						case GID_RELOAD:
							ami_update_quals(gwin);

							if(browser_window_reload_available(gwin->bw))
							{
								if(gwin->key_state & BROWSER_MOUSE_MOD_1)
								{
									browser_window_reload(gwin->bw,true);
								}
								else
								{
									browser_window_reload(gwin->bw,false);
								}
							}
						break;

						case GID_BACK:
							ami_gui_history(gwin, true);
						break;

						case GID_FORWARD:
							ami_gui_history(gwin, false);
						break;

						default:
//							printf("GADGET: %ld\n",(result & WMHI_GADGETMASK));
						break;
					}
				break;

				case WMHI_MENUPICK:
					item = ItemAddress(gwin->win->MenuStrip,code);
					while (code != MENUNULL)
					{
						ami_menupick(code,gwin,item);
						if(win_destroyed) break;
						code = item->NextSelect;
					}
				break;

				case WMHI_RAWKEY:
					storage = result & WMHI_GADGETMASK;

					if(storage >= IECODE_UP_PREFIX) break;

					GetAttr(WINDOW_InputEvent,gwin->objects[OID_MAIN],(ULONG *)&ie);

					nskey = ami_key_to_nskey(storage, ie);

					if(ie->ie_Qualifier & IEQUALIFIER_RCOMMAND)
					{
/* We are duplicating the menu shortcuts here, as if RMBTRAP is active
 * (ie. when context menus are enabled and the mouse is over the browser
 * rendering area), Intuition also does not catch the menu shortcut
 * key presses.  Context menus need to be changed to use MENUVERIFY not RMBTRAP */
						switch(nskey)
						{
							case 'n':
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									browser_window_create(option_homepage_url, NULL,
										0, true, false);
							break;

							case 't':
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									browser_window_create(option_homepage_url,
										gwin->bw, 0, true, true);
							break;

							case 'k':
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									browser_window_destroy(gwin->bw);
							break;

							case 'p':
								ami_print_ui(gwin->bw->current_content);
							break;

							case 'q':
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									ami_quit_netsurf();
							break;

							case 'a':
								browser_window_key_press(gwin->bw, KEY_SELECT_ALL);
							break;

							case 'x':
								browser_window_key_press(gwin->bw, KEY_CUT_SELECTION);
							break;

							case 'c':
								browser_window_key_press(gwin->bw, KEY_COPY_SELECTION);
								browser_window_key_press(gwin->bw, KEY_CLEAR_SELECTION);
							break;

							case 'v':
								browser_window_key_press(gwin->bw, KEY_PASTE);
							break;

							case 'z':
								browser_window_key_press(gwin->bw, KEY_CLEAR_SELECTION);
							break;

							case 'f':
								ami_search_open(gwin->bw->window);
							break;

							case 'h':
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									ami_tree_open(hotlist_window, AMI_TREE_HOTLIST);
							break;

/* The following aren't available from the menu at the moment */

							case 'r': // reload
								if(browser_window_reload_available(gwin->bw))
									browser_window_reload(gwin->bw,false);
							break;

							case 'u': // open url
								if((option_kiosk_mode == false) &&
									(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
									ActivateGadget((struct Gadget *)gwin->objects[GID_URL],
										gwin->win, NULL);
							break;
						}
					}
					else
					{
						if(!browser_window_key_press(gwin->bw, nskey))
						{
							GetAttr(SPACE_AreaBox,
								(Object *)gwin->objects[GID_BROWSER],
								(ULONG *)&bbox);

							gui_window_get_scroll(gwin->bw->window,
								&gwin->bw->window->scrollx,
								&gwin->bw->window->scrolly);

							switch(nskey)
							{
								case KEY_UP:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx,
										gwin->bw->window->scrolly - 5);
								break;

								case KEY_DOWN:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx,
										gwin->bw->window->scrolly + 5);
								break;

								case KEY_LEFT:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx - 5,
										gwin->bw->window->scrolly);
								break;

								case KEY_RIGHT:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx + 5,
										gwin->bw->window->scrolly);
								break;

								case KEY_PAGE_UP:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx,
										gwin->bw->window->scrolly - bbox->Height);
								break;

								case KEY_PAGE_DOWN:
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx,
										gwin->bw->window->scrolly + bbox->Height);
								break;

								case KEY_LINE_START: // page left
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx - bbox->Width,
										gwin->bw->window->scrolly);
								break;

								case KEY_LINE_END: // page right
									gui_window_set_scroll(gwin->bw->window,
										gwin->bw->window->scrollx + bbox->Width,
										gwin->bw->window->scrolly);
								break;

								case KEY_TEXT_START: // home
									gui_window_set_scroll(gwin->bw->window, 0, 0);
								break;

								case KEY_TEXT_END: // end
									gui_window_set_scroll(gwin->bw->window, 
										content_get_width(gwin->bw->current_content),
										content_get_height(gwin->bw->current_content));
								break;

								case KEY_WORD_RIGHT: // alt+right
									ami_change_tab(gwin, 1);
								break;

								case KEY_WORD_LEFT: // alt+left
									ami_change_tab(gwin, -1);
								break;

								case KEY_DELETE_LEFT: // backspace
									ami_gui_history(gwin, true);
								break;

								case RAWKEY_F5: // reload
									if(browser_window_reload_available(gwin->bw))
										browser_window_reload(gwin->bw,false);
								break;
							}
						}
					}
				break;

				case WMHI_NEWSIZE:
					switch(node->Type)
					{
						struct Node *tab = NULL, *ntab = NULL;
						struct browser_window *bw = NULL;

						case AMINS_WINDOW:
							ami_set_border_gadget_balance(gwin);
							ami_update_throbber(gwin,true);

							if(gwin->tabs)
							{
								tab = GetHead(&gwin->tab_list);

								do
								{
									ntab=GetSucc(tab);
									GetClickTabNodeAttrs(tab,
										TNA_UserData, &bw,
										TAG_DONE);
									bw->reformat_pending = true;
								} while(tab=ntab);
							}
							// fall through
						case AMINS_FRAME:
							gwin->bw->reformat_pending = true;
							gwin->redraw_required = true;
						break;
					}
				break;

				case WMHI_CLOSEWINDOW:
					ami_close_all_tabs(gwin);
		        break;

				case WMHI_ICONIFY:
				{
					struct DiskObject *dobj;
					struct bitmap *bm;

					bm = urldb_get_thumbnail(content_get_url(gwin->bw->current_content));
					gwin->dobj = amiga_icon_from_bitmap(bm);
					HideWindow(gwin->win);
					gwin->appicon = AddAppIcon(gwin->objects[OID_MAIN], NULL,
											gwin->win->Title, appport, NULL,
											gwin->dobj, NULL);
				}
				break;

				case WMHI_INACTIVE:
					gwin->bw->window->c_h_temp = gwin->bw->window->c_h;
					gui_window_remove_caret(gwin->bw->window);
				break;

				case WMHI_ACTIVE:
					if(gwin->bw) curbw = gwin->bw;
					if(gwin->bw->window->c_h_temp)
						gwin->bw->window->c_h = gwin->bw->window->c_h_temp;
				break;

				case WMHI_INTUITICK:
				break;

	   	     	default:
//					printf("class: %ld\n",(result & WMHI_CLASSMASK));
   	       		break;
			}

			if(win_destroyed)
			{
					/* we can't be sure what state our window_list is in, so let's
					jump out of the function and start again */

				win_destroyed = false;
				return;
			}

			if(drag_x_move || drag_y_move)
			{
				gui_window_get_scroll(gwin->bw->window,
					&gwin->bw->window->scrollx, &gwin->bw->window->scrolly);

				gui_window_set_scroll(gwin->bw->window,
					gwin->bw->window->scrollx + drag_x_move,
					gwin->bw->window->scrolly + drag_y_move);
			}

//	ReplyMsg((struct Message *)message);
		}

		if((node->Type == AMINS_WINDOW) || (node->Type == AMINS_FRAME))
		{
			if(gwin->redraw_required || gwin->bw->reformat_pending)
				ami_do_redraw(gwin);

			if(gwin->bw->window->throbbing)
				ami_update_throbber(gwin,false);

			if(gwin->bw->window->c_h)
			{
				gui_window_place_caret(gwin->bw->window, gwin->bw->window->c_x,
					gwin->bw->window->c_y, gwin->bw->window->c_h);
			}
		}
	} while(node = nnode);
}

void ami_gui_appicon_remove(struct gui_window_2 *gwin)
{
	if(gwin->appicon)
	{
		RemoveAppIcon(gwin->appicon);
		amiga_icon_free(gwin->dobj);
		gwin->appicon = NULL;
	}
}

void ami_handle_appmsg(void)
{
	struct AppMessage *appmsg;
	struct gui_window_2 *gwin;
	struct IBox *bbox;
	ULONG x,y,xs,ys,width,height,len;
	struct WBArg *appwinargs;
	STRPTR filename;
	struct box *box,*file_box=0,*text_box=0;
	hlcache_handle *content;
	int box_x=0,box_y=0;
	BPTR fh = 0;
	char *utf8text,*urlfilename;

	while(appmsg=(struct AppMessage *)GetMsg(appport))
	{
		GetAttr(WINDOW_UserData, (Object *)appmsg->am_ID, (ULONG *)&gwin);

		if(appmsg->am_Type == AMTYPE_APPICON)
		{
			ami_gui_appicon_remove(gwin);
			ShowWindow(gwin->win, WINDOW_FRONTMOST);
		}
		else if(appmsg->am_Type == AMTYPE_APPWINDOW)
		{
			GetAttr(SPACE_AreaBox, (Object *)gwin->objects[GID_BROWSER],
				(ULONG *)&bbox);

			ami_get_hscroll_pos(gwin, (ULONG *)&xs);
			x = (appmsg->am_MouseX) - (bbox->Left) +xs;

			ami_get_vscroll_pos(gwin, (ULONG *)&ys);
			y = appmsg->am_MouseY - bbox->Top + ys;

			width=bbox->Width;
			height=bbox->Height;

			if(appwinargs = appmsg->am_ArgList)
			{
				if(filename = AllocVec(1024,MEMF_PRIVATE | MEMF_CLEAR))
				{
					if(appwinargs->wa_Lock)
					{
						NameFromLock(appwinargs->wa_Lock,filename,1024);
					}

					AddPart(filename,appwinargs->wa_Name,1024);

					if(((gwin->bw->current_content == NULL) ||
						(content_get_type(gwin->bw->current_content) != CONTENT_HTML)) ||
						(!((x>=xs) && (y>=ys) && (x<width+xs) && (y<height+ys))))
					{
						urlfilename = path_to_url(filename);
						browser_window_go(gwin->bw,urlfilename,NULL,true);
						free(urlfilename);
					}
					else
					{
						content = gwin->bw->current_content;
						box = html_get_box_tree(content);
						while ((box = box_at_point(box, x, y, &box_x, &box_y, &content)))
						{
							if (box->style && css_computed_visibility(box->style) == CSS_VISIBILITY_HIDDEN)	continue;

							if (box->gadget)
							{
								switch (box->gadget->type)
								{
									case GADGET_FILE:
										file_box = box;
									break;

									case GADGET_TEXTBOX:
									case GADGET_TEXTAREA:
									case GADGET_PASSWORD:
										text_box = box;
									break;

									default:
									break;
								}
							}
						}

						if((!file_box) && (!text_box))
						{
							urlfilename = path_to_url(filename);
							browser_window_go(gwin->bw, urlfilename, NULL, true);
							free(urlfilename);
							return;
						}

						if(file_box)
						{
							utf8_convert_ret ret;
							char *utf8_fn;

							if(utf8_from_local_encoding(filename,0,&utf8_fn) != UTF8_CONVERT_OK)
							{
								warn_user("NoMemory","");
								return;
							}

							free(file_box->gadget->value);
							file_box->gadget->value = utf8_fn;

							box_coords(file_box, (int *)&x, (int *)&y);
							ami_do_redraw_limits(gwin->bw->window, 
								gwin->bw->window->shared->bw, x, y,
								x + file_box->width,
								y + file_box->height);
						}
						else
						{
							Object *dto;
							STRPTR buffer;
							uint32 bufferlen;

							browser_window_mouse_click(gwin->bw, BROWSER_MOUSE_PRESS_1, x, y);

							if(dto = NewDTObject(filename,
								DTA_GroupID, GID_TEXT, TAG_DONE))
							{
								if(GetDTAttrs(dto,
									TDTA_Buffer, &buffer,
									TDTA_BufferLen, &bufferlen,
									TAG_DONE))
								{
									uint32 bufferlen2 = 256;
									int32 blen;

									blen = bufferlen;

									do
									{
										if(blen < 256) bufferlen2 = blen;

										if(utf8_from_local_encoding(buffer,
													bufferlen2,
													&utf8text) == UTF8_CONVERT_OK)
										{
											browser_window_paste_text(gwin->bw,
												utf8text, strlen(utf8text),
												(blen <= 256) ? true : false);
											free(utf8text);
										}
										buffer += 256;
										blen -= 256;
									}while(blen > 0);
								}
								DisposeDTObject(dto);
							}
						}
					}
					FreeVec(filename);
				}
			}
		}
		ReplyMsg((struct Message *)appmsg);

		if(gwin->redraw_required)
			ami_do_redraw(gwin);
	}
}

void ami_handle_applib(void)
{
	struct ApplicationMsg *applibmsg;
	struct browser_window *bw;

	if(!applibport) return;

	while((applibmsg=(struct ApplicationMsg *)GetMsg(applibport)))
	{
		switch (applibmsg->type)
		{
			case APPLIBMT_NewBlankDoc:
				bw = browser_window_create(option_homepage_url, 0, 0, true, false);
			break;

			case APPLIBMT_OpenDoc:
			{
				struct ApplicationOpenPrintDocMsg *applibopdmsg = applibmsg;
				char *tempurl;

				tempurl = path_to_url(applibopdmsg->fileName);
				bw = browser_window_create(tempurl, 0, 0, true, false);
				free(tempurl);
			}
			break;

			case APPLIBMT_ToFront:
				if(curbw)
				{
					ScreenToFront(scrn);
					WindowToFront(curbw->window->shared->win);
					ActivateWindow(curbw->window->shared->win);
				}
				else
				{
					bw = browser_window_create(option_homepage_url, 0, 0, true, false);
				}
			break;

			case APPLIBMT_OpenPrefs:
				ScreenToFront(scrn);
				ami_gui_opts_open();
			break;

			case APPLIBMT_Quit:
			case APPLIBMT_ForceQuit:
				ami_quit_netsurf();
			break;

			case APPLIBMT_CustomMsg:
			{
				struct ApplicationCustomMsg *applibcustmsg = applibmsg;
		//		STRPTR tempmsg;
		//		if(tempmsg = ASPrintf("\"%s\"",applibcustmsg->customMsg))
		//		{
					OpenWorkbenchObjectA(applibcustmsg->customMsg, NULL);
		//			FreeVec(tempmsg);
		//		}
			}
			break;
		}
		ReplyMsg((struct Message *)applibmsg);
	}
}

void ami_get_msg(void)
{
	ULONG winsignal = 1L << sport->mp_SigBit;
	ULONG appsig = 1L << appport->mp_SigBit;
	ULONG schedulesig = 1L << msgport->mp_SigBit;
	ULONG signal;
	struct TimerRequest *timermsg = NULL;
	struct MsgPort *printmsgport = ami_print_get_msgport();
	ULONG printsig = 1L << printmsgport->mp_SigBit;
    ULONG signalmask = winsignal | appsig | schedulesig | rxsig | printsig | applibsig;

    signal = Wait(signalmask);

	if(signal & winsignal)
		ami_handle_msg();

	if(signal & appsig)
		ami_handle_appmsg();

	if(signal & rxsig)
		ami_arexx_handle();

	if(signal & applibsig)
		ami_handle_applib();

	if(signal & printsig)
	{
		while(GetMsg(printmsgport));  //ReplyMsg
		ami_print_cont();
	}

	if(signal & schedulesig)
	{
		if(timermsg = GetMsg(msgport))
		{
			ReplyMsg(timermsg);
			schedule_run(FALSE);
		}
	}
}

void gui_multitask(void)
{
	/* This seems a bit topsy-turvy to me, but in this function, NetSurf is doing
	   stuff and we need to poll for user events */

	ami_handle_msg();
	ami_handle_appmsg();
	ami_handle_applib();
	ami_arexx_handle();
}

void gui_poll(bool active)
{
	/* However, down here we are waiting for the user to do something or for a
	   scheduled event to kick in (the active variable seems to have no real
	   bearing on reality, but is supposed to indicate that NetSurf wants
	   control back ASAP, so we poll in that case).
	   schedule_run checks every event, really they need to be sorted so only
	   the first event needs to be run on each signal. */

	if(active)
	{
		gui_multitask();
		schedule_run(TRUE);
	}
	else
	{
		ami_get_msg();
	}
}

void ami_change_tab(struct gui_window_2 *gwin, int direction)
{
	struct Node *tab_node = gwin->bw->window->tab_node;
	struct Node *ptab;
	ULONG ptabnum = 0;

	if(gwin->tabs <= 1) return;

	if(direction > 0)
	{
		ptab = GetSucc(tab_node);
	}
	else
	{
		ptab = GetPred(tab_node);
	}

	if(!ptab) return;

	GetClickTabNodeAttrs(ptab, TNA_Number, (ULONG *)&ptabnum, TAG_DONE);

	RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_TABS], gwin->win, NULL,
						CLICKTAB_Current, ptabnum,
						TAG_DONE);

	ami_switch_tab(gwin, true);
}

void ami_switch_tab(struct gui_window_2 *gwin,bool redraw)
{
	struct Node *tabnode;
	struct IBox *bbox;

	/* Clear the last new tab list */
	gwin->bw->window->last_new_tab = NULL;

	if(gwin->tabs == 0) return;

	gui_window_get_scroll(gwin->bw->window,&gwin->bw->window->scrollx,&gwin->bw->window->scrolly);

	GetAttr(CLICKTAB_CurrentNode, (Object *)gwin->objects[GID_TABS],
				(ULONG *)&tabnode);
	GetClickTabNodeAttrs(tabnode,
						TNA_UserData,&gwin->bw,
						TAG_DONE);
	curbw = gwin->bw;
	GetAttr(SPACE_AreaBox, (Object *)gwin->objects[GID_BROWSER], (ULONG *)&bbox);

	if(!gwin->bw->current_content)
	{
		RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_URL],
			gwin->win, NULL, STRINGA_TextVal, "", TAG_DONE);

		p96RectFill(gwin->win->RPort, bbox->Left, bbox->Top,
			bbox->Width+bbox->Left, bbox->Height+bbox->Top, 0xffffffff);
		return;
	}

	ami_update_buttons(gwin);
	ami_menu_update_disabled(gwin->bw->window, gwin->bw->current_content);

	if(redraw)
	{
		gui_window_set_icon(gwin->bw->window, gwin->bw->window->favicon);

		p96RectFill(gwin->win->RPort,bbox->Left,bbox->Top,bbox->Width+bbox->Left,bbox->Height+bbox->Top,0xffffffff);

		browser_window_update(gwin->bw,false);

		gui_window_set_scroll(gwin->bw->window,gwin->bw->window->scrollx,gwin->bw->window->scrolly);
		gwin->redraw_scroll = false;

		browser_window_refresh_url_bar(gwin->bw,
			content_get_url(gwin->bw->current_content), gwin->bw->frag_id);
	}
}

void ami_try_quit(void)
{
	if(option_close_no_quit == false)
	{
		netsurf_quit = true;
		return;
	}
	else
	{
		if(locked_screen == FALSE)
			if(CloseScreen(scrn)) scrn = NULL;
	}
}

void ami_quit_netsurf(void)
{
	struct nsObject *node;
	struct nsObject *nnode;
	struct gui_window_2 *gwin;

	if(!IsMinListEmpty(window_list))
	{
		node = (struct nsObject *)GetHead((struct List *)window_list);

		do
		{
			nnode=(struct nsObject *)GetSucc((struct Node *)node);
			gwin = node->objstruct;

			switch(node->Type)
			{
				case AMINS_TVWINDOW:
					ami_tree_close((struct treeview_window *)gwin);
				break;

				case AMINS_WINDOW:
					ami_close_all_tabs(gwin);				
				break;
			}

		} while(node = nnode);
	}

	if(IsMinListEmpty(window_list))
	{
		/* last window closed, so exit */
		netsurf_quit = true;
	}
}

void ami_gui_close_screen(struct Screen *scrn)
{
	ULONG scrnsig = 1 << screen_signal;

	if(scrn == NULL) return;
	if(CloseScreen(scrn)) return;

	LOG(("Waiting for visitor windows to close..."));
	Wait(scrnsig);
	CloseScreen(scrn);
}

void gui_quit(void)
{
	int i;

	ami_theme_throbber_free();

	urldb_save(option_url_file);
	urldb_save_cookies(option_cookie_file);
	ami_hotlist_free(option_hotlist_file);
	ami_cookies_free();
	ami_global_history_free();
	sslcert_cleanup();

	if(IApplication && ami_appid)
		UnregisterApplication(ami_appid, NULL);

	ami_arexx_cleanup();

	ami_free_layers(&browserglob);
	FreeScreenDrawInfo(scrn, dri);

	ami_close_fonts();

	/* If it is our public screen, close it or wait until the visitor windows leave */
	if(locked_screen == FALSE) ami_gui_close_screen(scrn);
	FreeSignal(screen_signal);

	FreeVec(nsscreentitle);

	ami_context_menu_free();

	ami_mouse_pointers_free();
	ami_clipboard_free();
	ami_print_free();

	FreeSysObject(ASOT_PORT,appport);
	FreeSysObject(ASOT_PORT,sport);

	FreeAslRequest(filereq);
	FreeAslRequest(savereq);

	ami_openurl_close();
    FreeStringClass(urlStringClass);

	if(IApplication) DropInterface((struct Interface *)IApplication);
	if(ApplicationBase) CloseLibrary(ApplicationBase);

	if(IKeymap) DropInterface((struct Interface *)IKeymap);
	if(KeymapBase) CloseLibrary(KeymapBase);

	ami_schedule_free();
	ami_schedule_close_timer();

	FreeObjList(window_list);
}

void ami_update_buttons(struct gui_window_2 *gwin)
{
	BOOL back=FALSE,forward=TRUE,tabclose=FALSE,stop=FALSE,reload=FALSE;

	if(gwin->bw->browser_window_type != BROWSER_WINDOW_NORMAL)
		return;

	if(!browser_window_back_available(gwin->bw))
		back=TRUE;

	if(browser_window_forward_available(gwin->bw))
		forward=FALSE;

	if(!browser_window_stop_available(gwin->bw))
		stop=TRUE;

	if(!browser_window_reload_available(gwin->bw))
		reload=TRUE;

	if(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL &&
		option_kiosk_mode == false)
	{
		if(gwin->tabs <= 1)
		{
			tabclose=TRUE;
			OffMenu(gwin->win,AMI_MENU_CLOSETAB);
		}
		else
		{
			OnMenu(gwin->win,AMI_MENU_CLOSETAB);
		}
	}

	RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_BACK],gwin->win,NULL,
		GA_Disabled,back,
		TAG_DONE);

	RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_FORWARD],gwin->win,NULL,
		GA_Disabled,forward,
		TAG_DONE);

	RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_RELOAD],gwin->win,NULL,
		GA_Disabled,reload,
		TAG_DONE);

	RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_STOP],gwin->win,NULL,
		GA_Disabled,stop,
		TAG_DONE);

	if((gwin->tabs) && (ClickTabBase->lib_Version < 53))
	{
		RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_CLOSETAB],gwin->win,NULL,
			GA_Disabled,tabclose,
			TAG_DONE);
	}
}

void ami_toggletabbar(struct gui_window_2 *gwin, bool show)
{
	if(ClickTabBase->lib_Version < 53) return;

	if(show)
	{
		struct TagItem attrs[3];

		attrs[0].ti_Tag = CHILD_WeightedWidth;
		attrs[0].ti_Data = 0;
		attrs[1].ti_Tag = CHILD_WeightedHeight;
		attrs[1].ti_Data = 0;
		attrs[2].ti_Tag = TAG_DONE;
		attrs[2].ti_Data = 0;

		gwin->objects[GID_TABS] = ClickTabObject,
					GA_ID, GID_TABS,
					GA_RelVerify, TRUE,
					GA_Underscore, 13, // disable kb shortcuts
					CLICKTAB_Labels, &gwin->tab_list,
					CLICKTAB_LabelTruncate, TRUE,
					CLICKTAB_CloseImage, gwin->objects[GID_CLOSETAB_BM],
					CLICKTAB_FlagImage, gwin->objects[GID_TABS_FLAG],
					ClickTabEnd;

		gwin->objects[GID_ADDTAB] = ButtonObject,
					GA_ID, GID_ADDTAB,
					GA_RelVerify, TRUE,
					GA_HintInfo, gwin->helphints[GID_ADDTAB],
					GA_Text, "+",
					BUTTON_RenderImage, gwin->objects[GID_ADDTAB_BM],
					ButtonEnd;

		IDoMethod(gwin->objects[GID_TABLAYOUT], LM_ADDCHILD,
				gwin->win, gwin->objects[GID_TABS], NULL);

		IDoMethod(gwin->objects[GID_TABLAYOUT], LM_ADDCHILD,
				gwin->win, gwin->objects[GID_ADDTAB], attrs);
	}
	else
	{
		IDoMethod(gwin->objects[GID_TABLAYOUT], LM_REMOVECHILD,
				gwin->win, gwin->objects[GID_TABS]);

		IDoMethod(gwin->objects[GID_TABLAYOUT], LM_REMOVECHILD,
				gwin->win, gwin->objects[GID_ADDTAB]);
	}

	FlushLayoutDomainCache((struct Gadget *)gwin->objects[GID_MAIN]);

	RethinkLayout((struct Gadget *)gwin->objects[GID_MAIN],
			gwin->win, NULL, TRUE);

	gwin->redraw_required = true;
	gwin->bw->reformat_pending = true;
}

struct gui_window *gui_create_browser_window(struct browser_window *bw,
		struct browser_window *clone, bool new_tab)
{
	struct gui_window *gwin = NULL;
	bool closegadg=TRUE;
	struct Node *node;
	ULONG curx=option_window_x,cury=option_window_y,curw=option_window_width,curh=option_window_height;
	char nav_west[100],nav_west_s[100],nav_west_g[100];
	char nav_east[100],nav_east_s[100],nav_east_g[100];
	char stop[100],stop_s[100],stop_g[100];
	char reload[100],reload_s[100],reload_g[100];
	char home[100],home_s[100],home_g[100];
	char closetab[100],closetab_s[100],closetab_g[100];
	char addtab[100],addtab_s[100],addtab_g[100];
	char tabthrobber[100];

	if(!scrn) ami_openscreenfirst();

	if(option_kiosk_mode) new_tab = false;
	bw->scale = 1.0;

	if(clone)
	{
		if(clone->window)
		{
			curx=clone->window->shared->win->LeftEdge;
			cury=clone->window->shared->win->TopEdge;
			curw=clone->window->shared->win->Width;
			curh=clone->window->shared->win->Height;
		}
	}

	gwin = AllocVec(sizeof(struct gui_window),MEMF_PRIVATE | MEMF_CLEAR);

	if(!gwin)
	{
		warn_user("NoMemory","");
		return NULL;
	}

	NewList(&gwin->dllist);

/*
	if(bw->browser_window_type == BROWSER_WINDOW_IFRAME)
	{
		gwin->shared = bw->parent->window->shared;
		gwin->bw = bw;
		return gwin;
	}
*/

	if(new_tab && clone && (bw->browser_window_type == BROWSER_WINDOW_NORMAL))
	{
		gwin->shared = clone->window->shared;
		gwin->tab = gwin->shared->next_tab;

		if(gwin->shared->tabs == 1)
			ami_toggletabbar(gwin->shared, true);

		SetGadgetAttrs((struct Gadget *)gwin->shared->objects[GID_TABS],
						gwin->shared->win, NULL,
						CLICKTAB_Labels, ~0,
						TAG_DONE);

		gwin->tab_node = AllocClickTabNode(TNA_Text,messages_get("NetSurf"),
								TNA_Number,gwin->tab,
								TNA_UserData,bw,
								TNA_CloseGadget, TRUE,
								TAG_DONE);

		if(option_new_tab_last)
		{
			AddTail(&gwin->shared->tab_list, gwin->tab_node);
		}
		else
		{
			struct Node *insert_after = clone->window->tab_node;

			if(clone->window->last_new_tab)
				insert_after = clone->window->last_new_tab;
			Insert(&gwin->shared->tab_list, gwin->tab_node, insert_after);
			clone->window->last_new_tab = gwin->tab_node;
		}

		RefreshSetGadgetAttrs((struct Gadget *)gwin->shared->objects[GID_TABS],
							gwin->shared->win, NULL,
							CLICKTAB_Labels, &gwin->shared->tab_list,
							TAG_DONE);

		if(option_new_tab_active)
		{
			RefreshSetGadgetAttrs((struct Gadget *)gwin->shared->objects[GID_TABS],gwin->shared->win,NULL,
							CLICKTAB_Current,gwin->tab,
							TAG_DONE);
		}

		if(ClickTabBase->lib_Version < 53)
			RethinkLayout((struct Gadget *)gwin->shared->objects[GID_TABLAYOUT],gwin->shared->win,NULL,TRUE);

		gwin->shared->tabs++;
		gwin->shared->next_tab++;

		if(option_new_tab_active) ami_switch_tab(gwin->shared,false);

		ami_update_buttons(gwin->shared);

		return gwin;
	}

	gwin->shared = AllocVec(sizeof(struct gui_window_2),MEMF_PRIVATE | MEMF_CLEAR);

	if(!gwin->shared)
	{
		warn_user("NoMemory","");
		return NULL;
	}

	gwin->shared->scrollerhook.h_Entry = (void *)ami_scroller_hook;
	gwin->shared->scrollerhook.h_Data = gwin->shared;

	switch(bw->browser_window_type)
	{
        case BROWSER_WINDOW_IFRAME:
        case BROWSER_WINDOW_FRAMESET:
        case BROWSER_WINDOW_FRAME:

			gwin->tab = 0;
			gwin->shared->tabs = 0;
			gwin->tab_node = NULL;

			gwin->shared->objects[OID_MAIN] = WindowObject,
       	    WA_ScreenTitle,nsscreentitle,
//           	WA_Title, messages_get("NetSurf"),
           	WA_Activate, FALSE,
           	WA_DepthGadget, TRUE,
           	WA_DragBar, TRUE,
           	WA_CloseGadget, FALSE,
			WA_Top,cury,
			WA_Left,curx,
			WA_Width,curw,
			WA_Height,curh,
           	WA_SizeGadget, TRUE,
			WA_CustomScreen,scrn,
			WA_ReportMouse,TRUE,
			WA_SmartRefresh,TRUE,
           	WA_IDCMP,IDCMP_MENUPICK | IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS |
				IDCMP_NEWSIZE | IDCMP_RAWKEY | IDCMP_GADGETUP | IDCMP_SIZEVERIFY |
				IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE, // | IDCMP_INTUITICKS,
//			WINDOW_IconifyGadget, TRUE,
//			WINDOW_NewMenu,menu,
			WINDOW_HorizProp,1,
			WINDOW_VertProp,1,
			WINDOW_IDCMPHook,&gwin->shared->scrollerhook,
			WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE | IDCMP_SIZEVERIFY,
            WINDOW_AppPort, appport,
			WINDOW_AppWindow,TRUE,
			WINDOW_BuiltInScroll,TRUE,
			WINDOW_SharedPort,sport,
			WINDOW_UserData,gwin->shared,
//         	WINDOW_Position, WPOS_CENTERSCREEN,
//			WINDOW_CharSet,106,
           	WINDOW_ParentGroup, gwin->shared->objects[GID_MAIN] = VGroupObject,
//				LAYOUT_CharSet,106,
               	LAYOUT_SpaceOuter, TRUE,
				LAYOUT_AddChild, gwin->shared->objects[GID_BROWSER] = SpaceObject,
					GA_ID,GID_BROWSER,
					SPACE_Transparent,TRUE,
/*
					GA_RelVerify,TRUE,
					GA_Immediate,TRUE,
					GA_FollowMouse,TRUE,
*/
				SpaceEnd,
			EndGroup,
		EndWindow;

		break;
        case BROWSER_WINDOW_NORMAL:
			if(!option_kiosk_mode)
			{
				ULONG addtabclosegadget = TAG_IGNORE;
				ULONG iconifygadget = FALSE;

				if(option_use_pubscreen && (locked_screen == TRUE) &&
					(strcmp(option_use_pubscreen,"Workbench") == 0))
						iconifygadget = TRUE;
				ami_create_menu(bw->browser_window_type, gwin->shared, dri);

				NewList(&gwin->shared->tab_list);
				gwin->tab_node = AllocClickTabNode(TNA_Text,messages_get("NetSurf"),
													TNA_Number, 0,
													TNA_UserData, bw,
													TNA_CloseGadget, TRUE,
													TAG_DONE);
				AddTail(&gwin->shared->tab_list,gwin->tab_node);

				gwin->shared->tabs=1;
				gwin->shared->next_tab=1;

				gwin->shared->svbuffer = AllocVec(2000, MEMF_CLEAR);

				gwin->shared->helphints[GID_BACK] =
					remove_escape_chars(messages_get("HelpToolbar0"), true);
				gwin->shared->helphints[GID_FORWARD] =
					remove_escape_chars(messages_get("HelpToolbar1"), true);
				gwin->shared->helphints[GID_STOP] =
					remove_escape_chars(messages_get("HelpToolbar2"), true);
				gwin->shared->helphints[GID_RELOAD] =
					remove_escape_chars(messages_get("HelpToolbar3"), true);
				gwin->shared->helphints[GID_HOME] =
					remove_escape_chars(messages_get("HelpToolbar4"), true);
				gwin->shared->helphints[GID_URL] =
					remove_escape_chars(messages_get("HelpToolbar14"), true);
				gwin->shared->helphints[GID_SEARCHSTRING] =
					remove_escape_chars(messages_get("HelpWebSearch"), true);
				gwin->shared->helphints[GID_ADDTAB] =
					remove_escape_chars(messages_get("HelpAddTab"), true);

				ami_get_theme_filename(nav_west,"theme_nav_west",false);
				ami_get_theme_filename(nav_west_s,"theme_nav_west_s",false);
				ami_get_theme_filename(nav_west_g,"theme_nav_west_g",false);
				ami_get_theme_filename(nav_east,"theme_nav_east",false);
				ami_get_theme_filename(nav_east_s,"theme_nav_east_s",false);
				ami_get_theme_filename(nav_east_g,"theme_nav_east_g",false);
				ami_get_theme_filename(stop,"theme_stop",false);
				ami_get_theme_filename(stop_s,"theme_stop_s",false);
				ami_get_theme_filename(stop_g,"theme_stop_g",false);
				ami_get_theme_filename(reload,"theme_reload",false);
				ami_get_theme_filename(reload_s,"theme_reload_s",false);
				ami_get_theme_filename(reload_g,"theme_reload_g",false);
				ami_get_theme_filename(home,"theme_home",false);
				ami_get_theme_filename(home_s,"theme_home_s",false);
				ami_get_theme_filename(home_g,"theme_home_g",false);
				ami_get_theme_filename(closetab,"theme_closetab",false);
				ami_get_theme_filename(closetab_s,"theme_closetab_s",false);
				ami_get_theme_filename(closetab_g,"theme_closetab_g",false);
				ami_get_theme_filename(addtab,"theme_addtab",false);
				ami_get_theme_filename(addtab_s,"theme_addtab_s",false);
				ami_get_theme_filename(addtab_g,"theme_addtab_g",false);
				ami_get_theme_filename(tabthrobber,"theme_tab_loading",false);

				gwin->shared->objects[GID_ADDTAB_BM] = BitMapObject,
							BITMAP_SourceFile, addtab,
							BITMAP_SelectSourceFile, addtab_s,
							BITMAP_DisabledSourceFile, addtab_g,
							BITMAP_Screen, scrn,
							BITMAP_Masking, TRUE,
							BitMapEnd;

				gwin->shared->objects[GID_CLOSETAB_BM] = BitMapObject,
							BITMAP_SourceFile, closetab,
							BITMAP_SelectSourceFile, closetab_s,
							BITMAP_DisabledSourceFile, closetab_g,
							BITMAP_Screen, scrn,
							BITMAP_Masking, TRUE,
							BitMapEnd;

				if(ClickTabBase->lib_Version < 53)
				{
					addtabclosegadget = LAYOUT_AddChild;
					gwin->shared->objects[GID_CLOSETAB] = ButtonObject,
							GA_ID, GID_CLOSETAB,
							GA_RelVerify, TRUE,
							BUTTON_RenderImage, gwin->shared->objects[GID_CLOSETAB_BM],
							ButtonEnd;

					gwin->shared->objects[GID_TABS] = ClickTabObject,
							GA_ID,GID_TABS,
							GA_RelVerify,TRUE,
							GA_Underscore,13, // disable kb shortcuts
							CLICKTAB_Labels,&gwin->shared->tab_list,
							CLICKTAB_LabelTruncate,TRUE,
							ClickTabEnd;

					gwin->shared->objects[GID_ADDTAB] = ButtonObject,
							GA_ID, GID_ADDTAB,
							GA_RelVerify, TRUE,
							GA_Text, "+",
							BUTTON_RenderImage, gwin->shared->objects[GID_ADDTAB_BM],
							ButtonEnd;
				}
				else
				{
					gwin->shared->objects[GID_TABS_FLAG] = BitMapObject,
							BITMAP_SourceFile, tabthrobber,
							BITMAP_Screen,scrn,
							BITMAP_Masking,TRUE,
							BitMapEnd;
				}

				gwin->shared->objects[OID_MAIN] = WindowObject,
		       	    WA_ScreenTitle,nsscreentitle,
        		   	WA_Activate, TRUE,
		           	WA_DepthGadget, TRUE,
		           	WA_DragBar, TRUE,
        		   	WA_CloseGadget, TRUE,
		           	WA_SizeGadget, TRUE,
					WA_Top,cury,
					WA_Left,curx,
					WA_Width,curw,
					WA_Height,curh,
					WA_CustomScreen,scrn,
					WA_ReportMouse,TRUE,
					WA_SmartRefresh,TRUE,
					WA_SizeBBottom, TRUE,
        		   	WA_IDCMP,IDCMP_MENUPICK | IDCMP_MOUSEMOVE |
								IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
								IDCMP_RAWKEY | IDCMP_SIZEVERIFY |
								IDCMP_GADGETUP | IDCMP_IDCMPUPDATE |
								IDCMP_ACTIVEWINDOW | // IDCMP_INTUITICKS |
								IDCMP_EXTENDEDMOUSE,
					WINDOW_IconifyGadget, iconifygadget,
					WINDOW_NewMenu, gwin->shared->menu,
					WINDOW_VertProp,1,
					WINDOW_IDCMPHook,&gwin->shared->scrollerhook,
					WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE |
								IDCMP_EXTENDEDMOUSE | IDCMP_SIZEVERIFY,
        		    WINDOW_AppPort, appport,
					WINDOW_AppWindow,TRUE,
					WINDOW_SharedPort,sport,
					WINDOW_BuiltInScroll,TRUE,
					WINDOW_GadgetHelp, TRUE,
					WINDOW_UserData,gwin->shared,
		           	WINDOW_ParentGroup, gwin->shared->objects[GID_MAIN] = VGroupObject,
		               	LAYOUT_SpaceOuter, TRUE,
						LAYOUT_AddChild, gwin->shared->objects[GID_TOOLBARLAYOUT] = HGroupObject,
							LAYOUT_VertAlignment, LALIGN_CENTER,
							LAYOUT_AddChild, gwin->shared->objects[GID_BACK] = ButtonObject,
								GA_ID,GID_BACK,
								GA_RelVerify,TRUE,
								GA_Disabled,TRUE,
								GA_HintInfo, gwin->shared->helphints[GID_BACK],
								BUTTON_RenderImage,BitMapObject,
									BITMAP_SourceFile,nav_west,
									BITMAP_SelectSourceFile,nav_west_s,
									BITMAP_DisabledSourceFile,nav_west_g,
									BITMAP_Screen,scrn,
									BITMAP_Masking,TRUE,
								BitMapEnd,
							ButtonEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_FORWARD] = ButtonObject,
								GA_ID,GID_FORWARD,
								GA_RelVerify,TRUE,
								GA_Disabled,TRUE,
								GA_HintInfo, gwin->shared->helphints[GID_FORWARD],
								BUTTON_RenderImage,BitMapObject,
									BITMAP_SourceFile,nav_east,
									BITMAP_SelectSourceFile,nav_east_s,
									BITMAP_DisabledSourceFile,nav_east_g,
									BITMAP_Screen,scrn,
									BITMAP_Masking,TRUE,
								BitMapEnd,
							ButtonEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_STOP] = ButtonObject,
								GA_ID,GID_STOP,
								GA_RelVerify,TRUE,
								GA_HintInfo, gwin->shared->helphints[GID_STOP],
								BUTTON_RenderImage,BitMapObject,
									BITMAP_SourceFile,stop,
									BITMAP_SelectSourceFile,stop_s,
									BITMAP_DisabledSourceFile,stop_g,
									BITMAP_Screen,scrn,
									BITMAP_Masking,TRUE,
								BitMapEnd,
							ButtonEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_RELOAD] = ButtonObject,
								GA_ID,GID_RELOAD,
								GA_RelVerify,TRUE,
								GA_HintInfo, gwin->shared->helphints[GID_RELOAD],
								BUTTON_RenderImage,BitMapObject,
									BITMAP_SourceFile,reload,
									BITMAP_SelectSourceFile,reload_s,
									BITMAP_DisabledSourceFile,reload_g,
									BITMAP_Screen,scrn,
									BITMAP_Masking,TRUE,
								BitMapEnd,
							ButtonEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_HOME] = ButtonObject,
								GA_ID,GID_HOME,
								GA_RelVerify,TRUE,
								GA_HintInfo, gwin->shared->helphints[GID_HOME],
								BUTTON_RenderImage,BitMapObject,
									BITMAP_SourceFile,home,
									BITMAP_SelectSourceFile,home_s,
									BITMAP_DisabledSourceFile,home_g,
									BITMAP_Screen,scrn,
									BITMAP_Masking,TRUE,
								BitMapEnd,
							ButtonEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_ICON] = SpaceObject,
								GA_ID, GID_ICON,
								SPACE_MinWidth, 16,
								SPACE_MinHeight, 16,
								SPACE_Transparent, TRUE,
							SpaceEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
							LAYOUT_AddChild, gwin->shared->objects[GID_URL] =
								NewObject(urlStringClass, NULL,
                    				STRINGA_MaxChars, 2000,
                    				GA_ID, GID_URL,
                    				GA_RelVerify, TRUE,
									GA_HintInfo, gwin->shared->helphints[GID_URL],
                    				GA_TabCycle, TRUE,
                    				STRINGA_Buffer, gwin->shared->svbuffer,
                    				STRINGVIEW_Header, URLHistory_GetList(),
                			StringEnd,

						//	GA_ID, GID_TOOLBARLAYOUT,
						//	GA_RelVerify, TRUE,
						//	LAYOUT_RelVerify, TRUE,
							LAYOUT_WeightBar, TRUE,
							LAYOUT_AddChild, HGroupObject,
								LAYOUT_VertAlignment, LALIGN_CENTER,
								LAYOUT_AddChild, gwin->shared->objects[GID_SEARCH_ICON] = SpaceObject,
									GA_ID, GID_SEARCH_ICON,
									SPACE_MinWidth, 16,
									SPACE_MinHeight, 16,
									SPACE_Transparent, TRUE,
								SpaceEnd,
								CHILD_WeightedWidth,0,
								CHILD_WeightedHeight,0,
								LAYOUT_AddChild, gwin->shared->objects[GID_SEARCHSTRING] =StringObject,
									GA_ID,GID_SEARCHSTRING,
                   					STRINGA_TextVal, NULL,
									GA_RelVerify,TRUE,
									GA_HintInfo, gwin->shared->helphints[GID_SEARCHSTRING],
								StringEnd,
							LayoutEnd,
							CHILD_WeightedWidth, 0,
							LAYOUT_AddChild, gwin->shared->objects[GID_THROBBER] = SpaceObject,
								GA_ID,GID_THROBBER,
								SPACE_MinWidth,throbber_width,
								SPACE_MinHeight,throbber_height,
								SPACE_Transparent,TRUE,
							SpaceEnd,
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
						LayoutEnd,
						CHILD_WeightedHeight,0,
						LAYOUT_AddImage, BevelObject,
							BEVEL_Style, BVS_SBAR_VERT,
						BevelEnd,
						CHILD_WeightedHeight, 0,
						LAYOUT_AddChild, gwin->shared->objects[GID_TABLAYOUT] = HGroupObject,
							LAYOUT_SpaceInner,FALSE,
							addtabclosegadget, gwin->shared->objects[GID_CLOSETAB],
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,

							addtabclosegadget, gwin->shared->objects[GID_TABS],
							CHILD_CacheDomain,FALSE,

							addtabclosegadget, gwin->shared->objects[GID_ADDTAB],
							CHILD_WeightedWidth,0,
							CHILD_WeightedHeight,0,
						LayoutEnd,
						CHILD_WeightedHeight,0,
						LAYOUT_AddChild, gwin->shared->objects[GID_BROWSER] = SpaceObject,
							GA_ID,GID_BROWSER,
							SPACE_Transparent,TRUE,
						SpaceEnd,
					EndGroup,
				EndWindow;
			}
			else
			{
				/* borderless kiosk mode window */
				gwin->tab = 0;
				gwin->shared->tabs = 0;
				gwin->tab_node = NULL;

				gwin->shared->objects[OID_MAIN] = WindowObject,
    		   	    WA_ScreenTitle,nsscreentitle,
		           	WA_Activate, TRUE,
		           	WA_DepthGadget, FALSE,
        		   	WA_DragBar, FALSE,
		           	WA_CloseGadget, FALSE,
					WA_Borderless,TRUE,
					WA_RMBTrap,TRUE,
					WA_Top,0,
					WA_Left,0,
					WA_Width, scrn->Width,
					WA_Height, scrn->Height,
		           	WA_SizeGadget, FALSE,
					WA_CustomScreen,scrn,
					WA_ReportMouse,TRUE,
        		   	WA_IDCMP,IDCMP_MENUPICK | IDCMP_MOUSEMOVE |
							IDCMP_MOUSEBUTTONS | IDCMP_NEWSIZE |
							IDCMP_RAWKEY | // IDCMP_INTUITICKS |
							IDCMP_GADGETUP | IDCMP_IDCMPUPDATE |
							IDCMP_EXTENDEDMOUSE,
					WINDOW_HorizProp,1,
					WINDOW_VertProp,1,
					WINDOW_IDCMPHook,&gwin->shared->scrollerhook,
					WINDOW_IDCMPHookBits,IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,
		            WINDOW_AppPort, appport,
					WINDOW_AppWindow,TRUE,
					WINDOW_SharedPort,sport,
					WINDOW_UserData,gwin->shared,
					WINDOW_BuiltInScroll,TRUE,
		           	WINDOW_ParentGroup, gwin->shared->objects[GID_MAIN] = VGroupObject,
		               	LAYOUT_SpaceOuter, TRUE,
						LAYOUT_AddChild, gwin->shared->objects[GID_BROWSER] = SpaceObject,
							GA_ID,GID_BROWSER,
							SPACE_Transparent,TRUE,
						SpaceEnd,
					EndGroup,
				EndWindow;
			}
		break;
	}

	gwin->shared->win = (struct Window *)RA_OpenWindow(gwin->shared->objects[OID_MAIN]);

	if(!gwin->shared->win)
	{
		warn_user("NoMemory","");
		FreeVec(gwin->shared);
		FreeVec(gwin);
		return NULL;
	}

	GetAttr(WINDOW_VertObject, gwin->shared->objects[OID_MAIN],
			(ULONG *)&gwin->shared->objects[OID_VSCROLL]);

	RefreshSetGadgetAttrs((struct Gadget *)(APTR)gwin->shared->objects[OID_VSCROLL],
			gwin->shared->win, NULL,
			GA_ID, OID_VSCROLL,
			ICA_TARGET, ICTARGET_IDCMP,
			TAG_DONE);

	if(bw->browser_window_type == BROWSER_WINDOW_NORMAL &&
				option_kiosk_mode == false)
	{
		ULONG sz, size1, size2;

		sz = ami_get_border_gadget_balance(gwin->shared,
				(ULONG *)&size1, (ULONG *)&size2);

		gwin->shared->objects[GID_HSCROLL] = (struct Gadget *)NewObject(
				NULL,
				"scrollergclass",
				GA_ID, GID_HSCROLL,
				PGA_Freedom, FREEHORIZ,
		//		GA_Left, size1,
				GA_RelRight, 1 - size2 - sz,
				GA_Width, size2,
				GA_BottomBorder, TRUE,
				GA_Immediate, TRUE,
				ICA_TARGET, ICTARGET_IDCMP,
				GA_DrawInfo, dri,
				TAG_DONE);

		GetAttr(GA_Height, (Object *)gwin->shared->objects[GID_HSCROLL],
				(ULONG *)&sz);

		gwin->shared->objects[GID_STATUS] = (struct Gadget *)NewObject(
				NULL,
				"frbuttonclass",
				GA_ID, GID_STATUS,
				GA_Left, scrn->WBorLeft + 1,
				GA_RelBottom, -((2 + sz + scrn->WBorBottom - scrn->RastPort.TxHeight)/2),
				GA_Width, size1,
				GA_DrawInfo, dri,
				GA_BottomBorder, TRUE,
				GA_ReadOnly, TRUE,
				GA_Image, (struct Image *)NewObject(
					NULL,
					"frameiclass",
					IA_FrameType, FRAME_DISPLAY,
					IA_Top, 2-(scrn->RastPort.TxHeight),
					IA_Left, -2,
					IA_Height, scrn->WBorBottom + scrn->RastPort.TxHeight,
					IA_InBorder, TRUE,
					TAG_DONE),
				GA_Next, gwin->shared->objects[GID_HSCROLL],
				TAG_DONE);

		AddGList(gwin->shared->win, (struct Gadget *)gwin->shared->objects[GID_STATUS],
				(UWORD)~0, -1, NULL);

	/* Apparently you can't set GA_Width on creation time for frbuttonclass */

		SetGadgetAttrs((struct Gadget *)gwin->shared->objects[GID_STATUS],
			gwin->shared->win, NULL,
			GA_Width, size1,
			TAG_DONE);

		RefreshGadgets((APTR)gwin->shared->objects[GID_STATUS],
				gwin->shared->win, NULL);
	}
	else
	{
		GetAttr(WINDOW_HorizObject, gwin->shared->objects[OID_MAIN],
				(ULONG *)&gwin->shared->objects[OID_HSCROLL]);

		RefreshSetGadgetAttrs((struct Gadget *)(APTR)gwin->shared->objects[OID_HSCROLL],
				gwin->shared->win, NULL,
				GA_ID, OID_HSCROLL,
				ICA_TARGET, ICTARGET_IDCMP,
				TAG_DONE);
	}

	gwin->shared->rmbtrapped = FALSE;
	gwin->shared->bw = bw;
	curbw = bw;

	gwin->shared->node = AddObject(window_list,AMINS_WINDOW);
	gwin->shared->node->objstruct = gwin->shared;

	glob = &browserglob;

	if(locked_screen) UnlockPubScreen(NULL,scrn);
	search_web_retrieve_ico(false);

	return gwin;
}

ULONG ami_set_border_gadget_balance(struct gui_window_2 *gwin)
{
	/* Reset gadget widths according to new calculation */
	ULONG size1, size2, sz;

	sz = ami_get_border_gadget_balance(gwin, &size1, &size2);

	RefreshSetGadgetAttrs((struct Gadget *)(APTR)gwin->objects[GID_HSCROLL],
			gwin->win, NULL,
			GA_RelRight, - size2 - sz,
			GA_Width, size2,
			TAG_DONE);

	RefreshSetGadgetAttrs((struct Gadget *)(APTR)gwin->objects[GID_STATUS],
			gwin->win, NULL,
			GA_Width, size1,
			TAG_DONE);

	RefreshWindowFrame(gwin->win);
}

ULONG ami_get_border_gadget_balance(struct gui_window_2 *gwin, ULONG *size1, ULONG *size2)
{
	/* Get the sizes that border gadget 1 (status) and 2 (hscroller) need to be.
	** Returns the width of the vertical scroller (right-hand window border) as
	** a convenience.
	*/

	ULONG sz;
	ULONG available_width;
	float gad1percent;

	GetAttr(GA_Width, gwin->objects[OID_VSCROLL], (ULONG *)&sz);

	available_width = gwin->win->Width - scrn->WBorLeft - sz;

	gad1percent = option_toolbar_status_width / 10000.0;

	*size1 = (ULONG)(available_width * gad1percent);
	*size2 = (ULONG)(available_width * (1 - gad1percent));

	return sz;
}

void ami_close_all_tabs(struct gui_window_2 *gwin)
{
	struct Node *tab;
	struct Node *ntab;

	if(gwin->tabs)
	{
		tab = GetHead(&gwin->tab_list);

		do
		{
			ntab=GetSucc(tab);
			GetClickTabNodeAttrs(tab,
								TNA_UserData,&gwin->bw,
								TAG_DONE);
			browser_window_destroy(gwin->bw);
		} while(tab=ntab);
	}
	else
	{
			browser_window_destroy(gwin->bw);
	}
}

void gui_window_destroy(struct gui_window *g)
{
	struct Node *ptab;
	ULONG ptabnum = 0;
	int gid;

	if(!g) return;

	if(g->shared->searchwin && (g->shared->searchwin->gwin == g))
	{
		ami_search_close();
		win_destroyed = true;
	}

	if(g->hw)
	{
		ami_history_close(g->hw);
		win_destroyed = true;
	}

	ami_free_download_list(&g->dllist);

	curbw = NULL;

	if(g->shared->tabs > 1)
	{
		SetGadgetAttrs((struct Gadget *)g->shared->objects[GID_TABS],g->shared->win,NULL,
						CLICKTAB_Labels,~0,
						TAG_DONE);

		GetAttr(CLICKTAB_Current, g->shared->objects[GID_TABS],
				(ULONG *)&ptabnum);

		if(ptabnum == g->tab)
		{
			ptab = GetSucc(g->tab_node);
			if(!ptab) ptab = GetPred(g->tab_node);

			GetClickTabNodeAttrs(ptab,TNA_Number,(ULONG *)&ptabnum,TAG_DONE);
		}

		Remove(g->tab_node);
		FreeClickTabNode(g->tab_node);
		RefreshSetGadgetAttrs((struct Gadget *)g->shared->objects[GID_TABS],g->shared->win,NULL,
						CLICKTAB_Labels,&g->shared->tab_list,
						CLICKTAB_Current,ptabnum,
						TAG_DONE);

		if(ClickTabBase->lib_Version < 53)
			RethinkLayout((struct Gadget *)g->shared->objects[GID_TABLAYOUT],g->shared->win,NULL,TRUE);

		g->shared->tabs--;
		ami_switch_tab(g->shared,true);

		if(g->shared->tabs == 1)
			ami_toggletabbar(g->shared, false);

		ami_utf8_free(g->tabtitle);

		FreeVec(g);
		return;
	}

	DisposeObject(g->shared->objects[OID_MAIN]);
	ami_gui_appicon_remove(g->shared);

	/* These aren't freed by the above.
	 * TODO: nav_west etc need freeing too */
	DisposeObject(g->shared->objects[GID_ADDTAB_BM]);
	DisposeObject(g->shared->objects[GID_CLOSETAB_BM]);
	DisposeObject(g->shared->objects[GID_TABS_FLAG]);

	ami_free_menulabs(g->shared);
	free(g->shared->wintitle);
	ami_utf8_free(g->shared->status);
	FreeVec(g->shared->svbuffer);

	for(gid = 0; gid < GID_LAST; gid++)
		free(g->shared->helphints[gid]);

	DelObject(g->shared->node);
	if(g->tab_node)
	{
		Remove(g->tab_node);
		FreeClickTabNode(g->tab_node);
	}
	FreeVec(g); // g->shared should be freed by DelObject()

	if(IsMinListEmpty(window_list))
	{
		/* last window closed, so exit */
		ami_try_quit();
	}

	win_destroyed = true;
}

void gui_window_set_title(struct gui_window *g, const char *title)
{
	struct Node *node;
	ULONG cur_tab = 0;
	char *utf8title;

	if(!g) return;
	if(!title) return;

	utf8title = ami_utf8_easy((char *)title);

	if(g->tab_node && (g->shared->tabs > 1))
	{
		node = g->tab_node;

		if((g->tabtitle == NULL) || (strcmp(utf8title, g->tabtitle)))
		{
			SetGadgetAttrs((struct Gadget *)g->shared->objects[GID_TABS],
							g->shared->win, NULL,
							CLICKTAB_Labels, ~0,
							TAG_DONE);

			SetClickTabNodeAttrs(node, TNA_Text, utf8title,
									TNA_HintInfo, utf8title,
									TAG_DONE);

			RefreshSetGadgetAttrs((struct Gadget *)g->shared->objects[GID_TABS],
								g->shared->win, NULL,
								CLICKTAB_Labels, &g->shared->tab_list,
								TAG_DONE);

			if(g->tabtitle) ami_utf8_free(g->tabtitle);
			g->tabtitle = utf8title;

			if(ClickTabBase->lib_Version < 53)
				RethinkLayout((struct Gadget *)g->shared->objects[GID_TABLAYOUT],
					g->shared->win, NULL, TRUE);
		}

		GetAttr(CLICKTAB_Current, g->shared->objects[GID_TABS],
				(ULONG *)&cur_tab);
	}

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		if((g->shared->wintitle == NULL) || (strcmp(utf8title, g->shared->wintitle)))
		{
			if(g->shared->wintitle) free(g->shared->wintitle);
			g->shared->wintitle = strdup(utf8title);
			SetWindowTitles(g->shared->win, g->shared->wintitle, nsscreentitle);
		}
	}
}

/**
 * Redraw an area of the browser window - Amiga-specific function
 *
 * \param  g   a struct gui_window 
 * \param  bw  a struct browser_window
 * \param  x0  top-left co-ordinate (in document co-ordinates)
 * \param  y0  top-left co-ordinate (in document co-ordinates)
 * \param  x1  bottom-right co-ordinate (in document co-ordinates)
 * \param  y1  bottom-right co-ordinate (in document co-ordinates)
 */

void ami_do_redraw_limits(struct gui_window *g, struct browser_window *bw,
		int x0, int y0, int x1, int y1)
{
	ULONG xoffset,yoffset,width=800,height=600;
	ULONG htemp,vtemp;
	struct IBox *bbox;
	ULONG cur_tab = 0;
	ULONG sx, sy;
	struct rect clip;
	struct RastPort *temprp;
	int posx, posy;
	struct redraw_context ctx = {
		.interactive = true,
		.plot = &amiplot
	};

	if(!g) return;
	if(browser_window_redraw_ready(bw) == false) return;

	sx = g->scrollx;
	sy = g->scrolly;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
				g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if(!((cur_tab == g->tab) || (g->shared->tabs <= 1)))
	{
		return;
	}

	GetAttr(SPACE_AreaBox, g->shared->objects[GID_BROWSER], (ULONG *)&bbox);

	current_redraw_browser = bw;

	width=bbox->Width / bw->scale;
	height=bbox->Height / bw->scale;
	xoffset=bbox->Left;
	yoffset=bbox->Top;

	if((y1<sy) || (y0>sy+height)) return;
	if((x1<sx) || (x0>sx+width)) return;

	if((x0-(int)sx)<0) x0 = sx;
	if((y0-(int)sy)<0) y0 = sy;

	/* Check this - xoffset/yoffset are window-relative, not bitmap-relative */
	if((x1-x0)+(xoffset+x0-sx)>(width)) x1 = (width-(x0-sx)+x0);
	if((y1-y0)+(yoffset+y0-sy)>(height)) y1 = (height-(y0-sy)+y0);

	glob = &browserglob;

	if(option_direct_render == false)
	{
		clip.x0 = (x0 - sx);
		clip.y0 = (y0 - sy);
		clip.x1 = (x1 - sx);
		clip.y1 = (y1 - sy);
		posx = - sx;
		posy = - sy;
	}
	else
	{
		temprp = browserglob.rp;
 		browserglob.rp = g->shared->win->RPort;
		clip.x0 = (x0 - sx) + bbox->Left;
		clip.y0 = (y0 - sy) + bbox->Top;
		clip.x1 = (x1 - sx) + bbox->Left;
		clip.y1 = (y1 - sy) + bbox->Top;
		posx = bbox->Left - sx;
		posy = bbox->Top - sy;
	}

	if(browser_window_redraw(bw, posx, posy, &clip, &ctx))
	{
		ami_clearclipreg(&browserglob);

		if(option_direct_render == false)
		{
/* This is identical to the below, but for some reason doesn't blit anything.
 * Probably some values are wrong and BltBitMapTags is fussier.

		BltBitMapTags(BLITA_SrcType, BLITT_BITMAP, 
				BLITA_Source, browserglob.bm,
				BLITA_SrcX, clip.x0 * g->shared->bw->scale,
				BLITA_SrcY, clip.y0 * g->shared->bw->scale,
				BLITA_DestType, BLITT_RASTPORT, 
				BLITA_Dest, g->shared->win->RPort,
				BLITA_DestX, xoffset + (clip.x0 * g->shared->bw->scale),
				BLITA_DestY, yoffset + (clip.y0 * g->shared->bw->scale),
				BLITA_Width, (x1 - x0) * g->shared->bw->scale,
				BLITA_Height, (y1 - y0) * g->shared->bw->scale,
				TAG_DONE);
*/

		BltBitMapRastPort(browserglob.bm,
						clip.x0 * g->shared->bw->scale,
						clip.y0 * g->shared->bw->scale,
						g->shared->win->RPort,
						xoffset + (clip.x0 * g->shared->bw->scale),
						yoffset + (clip.y0 * g->shared->bw->scale),
						(x1 - x0) * g->shared->bw->scale,
						(y1 - y0) * g->shared->bw->scale,
						0x0C0);
		}
		else
		{
	 		browserglob.rp = temprp;
		}
	}

	current_redraw_browser = NULL;
}

void gui_window_redraw_window(struct gui_window *g)
{
	ULONG cur_tab = 0;

	if(!g) return;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
				g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
		g->shared->redraw_required = true;
}

void gui_window_update_box(struct gui_window *g,
		const union content_msg_data *data)
{
	ULONG sx,sy;

	if(!g) return;

	ami_do_redraw_limits(g, g->shared->bw,
			data->redraw.x,data->redraw.y,
			data->redraw.width+data->redraw.x,
			data->redraw.height+data->redraw.y);
}

void ami_do_redraw(struct gui_window_2 *g)
{
	struct Region *reg = NULL;
	struct Rectangle rect;
	hlcache_handle *c;
	ULONG hcurrent,vcurrent,xoffset,yoffset,width=800,height=600,x0=0,y0=0;
	struct IBox *bbox;
	ULONG oldh=g->oldh,oldv=g->oldv;
	bool morescroll = false;
	struct RastPort *temprp;

	if(browser_window_redraw_ready(g->bw) == false) return;

	GetAttr(SPACE_AreaBox, (Object *)g->objects[GID_BROWSER], (ULONG *)&bbox);
	ami_get_hscroll_pos(g, (ULONG *)&hcurrent);
	ami_get_vscroll_pos(g, (ULONG *)&vcurrent);

	c = g->bw->current_content;

	current_redraw_browser = g->bw;

	width=bbox->Width;
	height=bbox->Height;
	xoffset=bbox->Left;
	yoffset=bbox->Top;

	if(g->bw->reformat_pending)
	{
		browser_window_reformat(g->bw,false,width,height);
		g->bw->reformat_pending = false;
		g->redraw_scroll = false;
	}

	if(g->redraw_scroll)
	{
		if((abs(vcurrent-oldv) > height) ||	(abs(hcurrent-oldh) > width))
			g->redraw_scroll = false;

 		if(g->new_content) g->redraw_scroll = false;

		if(g->bw->scale != 1.0) g->redraw_scroll = false;
	}

	if(g->redraw_scroll)
	{
		g->bw->window->c_h_temp = g->bw->window->c_h;
		gui_window_remove_caret(g->bw->window);

		ScrollWindowRaster(g->win,hcurrent-oldh,vcurrent-oldv,
				xoffset,yoffset,xoffset+width-1,yoffset+height-1);

		g->bw->window->c_h = g->bw->window->c_h_temp;

		if(vcurrent>oldv)
		{
			ami_do_redraw_limits(g->bw->window, g->bw,
					hcurrent, (height / g->bw->scale) + oldv,
					hcurrent + (width / g->bw->scale),
					vcurrent + (height / g->bw->scale));
		}
		else if(vcurrent<oldv)
		{
			ami_do_redraw_limits(g->bw->window, g->bw,
					hcurrent, vcurrent,
					hcurrent + (width / g->bw->scale),
					oldv);
		}

		if(hcurrent>oldh)
		{
			ami_do_redraw_limits(g->bw->window, g->bw,
					(width / g->bw->scale) + oldh, vcurrent,
					hcurrent + (width / g->bw->scale),
					vcurrent + (height / g->bw->scale));
		}
		else if(hcurrent<oldh)
		{
			ami_do_redraw_limits(g->bw->window, g->bw,
					hcurrent, vcurrent,
					oldh, vcurrent+(height / g->bw->scale));
		}
	}
	else
	{
		struct rect clip;
		struct redraw_context ctx = {
			.interactive = true,
			.plot = &amiplot
		};

		glob = &browserglob;

		if(option_direct_render == false)
		{
			clip.x0 = 0;
			clip.y0 = 0;
			clip.x1 = width;
			clip.y1 = height;
		}
		else
		{
			temprp = browserglob.rp;
 			browserglob.rp = g->win->RPort;
			clip.x0 = bbox->Left;
			clip.y0 = bbox->Top;
			clip.x1 = bbox->Left + bbox->Width;
			clip.y1 = bbox->Top + bbox->Height;
		}

		if(browser_window_redraw(g->bw, clip.x0 - hcurrent, clip.y0 - vcurrent, &clip, &ctx))
		{
			ami_clearclipreg(&browserglob);

			if(option_direct_render == false)
			{
				BltBitMapTags(BLITA_SrcType, BLITT_BITMAP, 
					BLITA_Source, browserglob.bm,
					BLITA_SrcX, 0,
					BLITA_SrcY, 0,
					BLITA_DestType, BLITT_RASTPORT, 
					BLITA_Dest, g->win->RPort,
					BLITA_DestX, bbox->Left,
					BLITA_DestY, bbox->Top,
					BLITA_Width, bbox->Width,
					BLITA_Height, bbox->Height,
					TAG_DONE);
			}
			else
			{
 				browserglob.rp = temprp;
			}
		}
	}

	current_redraw_browser = NULL;

	ami_update_buttons(g);

	g->oldh = hcurrent;
	g->oldv = vcurrent;

	g->redraw_scroll = false;
	g->redraw_required = false;
	g->new_content = false;
}

void ami_get_hscroll_pos(struct gui_window_2 *gwin, ULONG *xs)
{
	if(gwin->objects[GID_HSCROLL])
	{
		GetAttr(PGA_Top, (Object *)gwin->objects[GID_HSCROLL], xs);
	}
	else if(gwin->objects[OID_HSCROLL])
	{
		GetAttr(SCROLLER_Top, gwin->objects[OID_HSCROLL], xs);
	}

	*xs /= gwin->bw->scale;
}

void ami_get_vscroll_pos(struct gui_window_2 *gwin, ULONG *ys)
{
	GetAttr(SCROLLER_Top, gwin->objects[OID_VSCROLL], ys);
	*ys /= gwin->bw->scale;
}

bool gui_window_get_scroll(struct gui_window *g, int *sx, int *sy)
{
	ami_get_hscroll_pos(g->shared, (ULONG *)sx);
	ami_get_vscroll_pos(g->shared, (ULONG *)sy);
}

void gui_window_set_scroll(struct gui_window *g, int sx, int sy)
{
	struct IBox *bbox;
	ULONG cur_tab = 0;

	if(!g) return;
	if(!g->shared->bw || !g->shared->bw->current_content) return;

	GetAttr(SPACE_AreaBox, g->shared->objects[GID_BROWSER], (ULONG *)&bbox);

	if(sx < 0) sx=0;
	if(sy < 0) sy=0;

	if(sx >= content_get_width(g->shared->bw->current_content) - bbox->Width)
		sx = content_get_width(g->shared->bw->current_content) - bbox->Width;
	if(sy >= (content_get_height(g->shared->bw->current_content) - bbox->Height))
		sy = content_get_height(g->shared->bw->current_content) - bbox->Height;

	if(content_get_width(g->shared->bw->current_content) <= bbox->Width) sx = 0;
	if(content_get_height(g->shared->bw->current_content) <= bbox->Height) sy = 0;

	if(g->tab_node && (g->shared->tabs > 1))
				GetAttr(CLICKTAB_Current,
					g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[OID_VSCROLL],
			g->shared->win, NULL,
			SCROLLER_Top, (ULONG)(sy * g->shared->bw->scale),
			TAG_DONE);

		if(g->shared->objects[GID_HSCROLL])
		{
			RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[GID_HSCROLL],
				g->shared->win, NULL,
				PGA_Top, (ULONG)(sx * g->shared->bw->scale),
				TAG_DONE);
		}
		else if(g->shared->objects[OID_HSCROLL])
		{
			RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[OID_HSCROLL],
				g->shared->win, NULL,
				SCROLLER_Top, (ULONG)(sx * g->shared->bw->scale),
				TAG_DONE);
		}
		g->shared->redraw_required = true;

		if(option_faster_scroll == true) g->shared->redraw_scroll = true;
			else g->shared->redraw_scroll = false;

		g->scrollx = sx;
		g->scrolly = sy;

//		history_set_current_scroll(g->shared->bw->history,g->scrollx,g->scrolly);
	}
//	g->shared->new_content = false;
}

void gui_window_scroll_visible(struct gui_window *g, int x0, int y0,
		int x1, int y1)
{
	gui_window_set_scroll(g, x0, y0);
}

void gui_window_position_frame(struct gui_window *g, int x0, int y0,
		int x1, int y1)
{
	if(!g) return;

	ChangeWindowBox(g->shared->win,x0,y0,x1-x0,y1-y0);
}

void gui_window_get_dimensions(struct gui_window *g, int *width, int *height,
		bool scaled)
{
	struct IBox *bbox;
	if(!g) return;

	GetAttr(SPACE_AreaBox, g->shared->objects[GID_BROWSER], (ULONG *)&bbox);

	*width = bbox->Width;
	*height = bbox->Height;

	if(scaled)
	{
		*width /= g->shared->bw->scale;
		*height /= g->shared->bw->scale;
	}
}

void gui_window_update_extent(struct gui_window *g)
{
	struct IBox *bbox;
	ULONG cur_tab = 0;

	if(!g) return;
	if(!g->shared->bw->current_content) return;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
				g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		GetAttr(SPACE_AreaBox, g->shared->objects[GID_BROWSER],
				(ULONG *)&bbox);

		RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[OID_VSCROLL],g->shared->win,NULL,
			SCROLLER_Total, (ULONG)(content_get_height(g->shared->bw->current_content) * g->shared->bw->scale),
			SCROLLER_Visible, bbox->Height,
			TAG_DONE);

		if(g->shared->objects[GID_HSCROLL])
		{
			RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[GID_HSCROLL],
				g->shared->win, NULL,
				PGA_Total, (ULONG)(content_get_width(g->shared->bw->current_content) * g->shared->bw->scale),
				PGA_Visible, bbox->Width,
				TAG_DONE);
		}
		else if(g->shared->objects[OID_HSCROLL])
		{
			RefreshSetGadgetAttrs((struct Gadget *)(APTR)g->shared->objects[OID_HSCROLL],
				g->shared->win, NULL,
				SCROLLER_Total, (ULONG)(content_get_width(g->shared->bw->current_content) * g->shared->bw->scale),
				SCROLLER_Visible, bbox->Width,
				TAG_DONE);
		}

	}
	g->shared->new_content = true;
}

void gui_window_set_status(struct gui_window *g, const char *text)
{
	ULONG cur_tab = 0;
	char *utf8text;
	ULONG size;
	UWORD chars;
	struct TextExtent textex;

	if(!g) return;
	if(!text) return;
	if(!g->shared->objects[GID_STATUS]) return;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
			g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		utf8text = ami_utf8_easy((char *)text);
		if(utf8text == NULL) return;

		GetAttr(GA_Width, g->shared->objects[GID_STATUS], (ULONG *)&size);
		chars = TextFit(&scrn->RastPort, utf8text, strlen(utf8text),
					&textex, NULL, 1, size - 4, scrn->RastPort.TxHeight);

		utf8text[chars] = 0;

		SetGadgetAttrs((struct Gadget *)g->shared->objects[GID_STATUS],
			g->shared->win, NULL,
			GA_Text, utf8text,
			TAG_DONE);

		RefreshGList((struct Gadget *)g->shared->objects[GID_STATUS],
				g->shared->win, NULL, 1);

		if(g->shared->status) ami_utf8_free(g->shared->status);
		g->shared->status = utf8text;
	}
}

void gui_window_set_url(struct gui_window *g, const char *url)
{
	ULONG cur_tab = 0;

	if(!g) return;
	if(!url) return;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
				g->shared->objects[GID_TABS], (ULONG *)&cur_tab);

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		RefreshSetGadgetAttrs((struct Gadget *)g->shared->objects[GID_URL],
			g->shared->win, NULL, STRINGA_TextVal, url, TAG_DONE);
	}
}

/**
 * function to add retrieved favicon to gui
 */
void gui_window_set_icon(struct gui_window *g, hlcache_handle *icon)
{
	struct BitMap *bm = NULL;
	struct IBox *bbox;
	ULONG cur_tab = 0;

	if(option_kiosk_mode == true) return;
	if(!g) return;
	if(g->shared->bw->browser_window_type != BROWSER_WINDOW_NORMAL) return;

	if(g->tab_node && (g->shared->tabs > 1)) GetAttr(CLICKTAB_Current,
						g->shared->objects[GID_TABS],
						(ULONG *)&cur_tab);

	if ((icon != NULL) &&
		(content_get_status(icon) != CONTENT_STATUS_READY) &&
		(content_get_status(icon) != CONTENT_STATUS_DONE)) return;
	if ((icon != NULL) && (content_get_bitmap(icon) != NULL))
	{
		bm = ami_getcachenativebm(content_get_bitmap(icon), 16, 16,
					g->shared->win->RPort->BitMap);
	}

	if((cur_tab == g->tab) || (g->shared->tabs <= 1))
	{
		GetAttr(SPACE_AreaBox, g->shared->objects[GID_ICON], (ULONG *)&bbox);

		EraseRect(g->shared->win->RPort, bbox->Left, bbox->Top,
			bbox->Left+16, bbox->Top+16);

		if(bm)
		{
			BltBitMapTags(BLITA_SrcX, 0,
						BLITA_SrcY, 0,
						BLITA_DestX, bbox->Left,
						BLITA_DestY, bbox->Top,
						BLITA_Width, 16,
						BLITA_Height, 16,
						BLITA_Source, bm,
						BLITA_Dest, g->shared->win->RPort,
						BLITA_SrcType, BLITT_BITMAP,
						BLITA_DestType, BLITT_RASTPORT,
						BLITA_UseSrcAlpha, TRUE,
						TAG_DONE);
		}
	}

	g->favicon = icon;
}

/**
 * set gui display of a retrieved favicon representing the search
 * provider
 * \param ico may be NULL for local calls; then access current cache from
 * search_web_ico()
 */
void gui_window_set_search_ico(hlcache_handle *ico)
{
	struct BitMap *bm = NULL;
	struct IBox *bbox;
	ULONG cur_tab = 0;
	struct nsObject *node;
	struct nsObject *nnode;
	struct gui_window_2 *gwin;
	char fname[100];
	struct bitmap *nsbm;
	bool free_bm = false;

	if(IsMinListEmpty(window_list))	return;
	if(option_kiosk_mode == true) return;
/* disabled, as at the moment, favicon/search icons don't work
	if (ico == NULL) ico = search_web_ico();
	if ((ico != NULL) && (content_get_bitmap(ico) != NULL))
	{
		bm = ami_getcachenativebm(content_get_bitmap(ico), 16, 16, NULL);
	}
*/

	/* generic search image */
	if(bm == NULL)
	{
		ami_get_theme_filename(&fname, "theme_search", false);
		if(nsbm = ami_bitmap_from_datatype(fname))
		{
			bm = ami_getcachenativebm(nsbm, 16, 16, NULL);
		}
		free_bm = true;
	}

	node = (struct nsObject *)GetHead((struct List *)window_list);

	do {
		nnode=(struct nsObject *)GetSucc((struct Node *)node);
		gwin = node->objstruct;

		if((node->Type == AMINS_WINDOW) &&
			(gwin->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
		{
			GetAttr(SPACE_AreaBox, gwin->objects[GID_SEARCH_ICON], (ULONG *)&bbox);

			RefreshSetGadgetAttrs((struct Gadget *)gwin->objects[GID_SEARCH_ICON],
				gwin->win, NULL,
				GA_HintInfo, search_web_provider_name(),
				TAG_DONE);

			EraseRect(gwin->win->RPort, bbox->Left, bbox->Top,
				bbox->Left+16, bbox->Top+16);

			if(bm)
			{
				BltBitMapTags(BLITA_SrcX, 0,
							BLITA_SrcY, 0,
							BLITA_DestX, bbox->Left,
							BLITA_DestY, bbox->Top,
							BLITA_Width, 16,
							BLITA_Height, 16,
							BLITA_Source, bm,
							BLITA_Dest, gwin->win->RPort,
							BLITA_SrcType, BLITT_BITMAP,
							BLITA_DestType, BLITT_RASTPORT,
							BLITA_UseSrcAlpha, TRUE,
							TAG_DONE);
			}
		}
	} while(node = nnode);

	if(bm && free_bm) bitmap_destroy(nsbm);
}

void gui_window_place_caret(struct gui_window *g, int x, int y, int height)
{
	struct IBox *bbox;
	ULONG xs,ys;

	if(!g) return;

	gui_window_remove_caret(g);

	GetAttr(SPACE_AreaBox,g->shared->objects[GID_BROWSER],(ULONG *)&bbox);
	xs = g->scrollx;
	ys = g->scrolly;

	SetAPen(g->shared->win->RPort,3);

	if((y-ys+height) > (bbox->Height)) height = bbox->Height-y+ys;

	if(((x-xs) <= 0) || ((x-xs+2) >= (bbox->Width)) || ((y-ys) <= 0) || ((y-ys) >= (bbox->Height))) return;

	g->c_w = 2;

	SetDrMd(g->shared->win->RPort,COMPLEMENT);

	RectFill(g->shared->win->RPort, x + bbox->Left - xs, y + bbox->Top - ys,
		x + bbox->Left + g->c_w - xs, y+bbox->Top + height - ys);

	SetDrMd(g->shared->win->RPort,JAM1);

	g->c_x = x;
	g->c_y = y;
	g->c_h = height;

	if((option_kiosk_mode == false) &&
		(g->shared->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
		OnMenu(g->shared->win, AMI_MENU_PASTE);
}

void gui_window_remove_caret(struct gui_window *g)
{
	struct IBox *bbox;
	int xs,ys;

	if(!g) return;
	if(g->c_h == 0) return;

	if((option_kiosk_mode == false) &&
		(g->shared->bw->browser_window_type == BROWSER_WINDOW_NORMAL))
		OffMenu(g->shared->win, AMI_MENU_PASTE);

	ami_do_redraw_limits(g, g->shared->bw, g->c_x, g->c_y,
		g->c_x + g->c_w + 1, g->c_y + g->c_h + 1);

	g->c_h = 0;
}

void gui_window_new_content(struct gui_window *g)
{
	hlcache_handle *c;

	if(g && g->shared && g->shared->bw && g->shared->bw->current_content)
		c = g->shared->bw->current_content;
	else return;

	ami_clearclipreg(&browserglob);
	g->shared->new_content = true;
	g->scrollx = 0;
	g->scrolly = 0;
	g->shared->oldh = 0;
	g->shared->oldv = 0;
	g->favicon = NULL;

	ami_menu_update_disabled(g, c);
}

bool gui_window_scroll_start(struct gui_window *g)
{
	return true;
}

bool gui_window_box_scroll_start(struct gui_window *g,
		int x0, int y0, int x1, int y1)
{
	DebugPrintF("box scroll start\n");
	return true;
}

bool gui_window_frame_resize_start(struct gui_window *g)
{
	DebugPrintF("resize frame\n");
	return true;
}

void ami_scroller_hook(struct Hook *hook,Object *object,struct IntuiMessage *msg) 
{
	ULONG gid,x,y;
	struct gui_window_2 *gwin = hook->h_Data;
	struct IntuiWheelData *wheel;
	Object *reqrefresh = NULL;

	gui_window_get_scroll(gwin->bw->window,
		&gwin->bw->window->scrollx,&gwin->bw->window->scrolly);

	switch(msg->Class)
	{
		case IDCMP_IDCMPUPDATE:
			gid = GetTagData( GA_ID, 0, msg->IAddress );

/*
			if(reqrefresh = GetTagData( LAYOUT_RequestRefresh, 0, msg->IAddress ))
			{
				printf("LAYOUT_RequestRefresh\n");
			}
*/

			switch( gid ) 
			{
				case GID_HSCROLL:
 				case OID_HSCROLL: 
 				case OID_VSCROLL:
//					history_set_current_scroll(gwin->bw->history,
//						gwin->bw->window->scrollx,gwin->bw->window->scrolly);

					if(option_faster_scroll == true) gwin->redraw_scroll = true;
						else gwin->redraw_scroll = false;

					gwin->redraw_required = true;
 				break;
			} 
		break;

		case IDCMP_EXTENDEDMOUSE:
			if(msg->Code == IMSGCODE_INTUIWHEELDATA)
			{
				wheel = (struct IntuiWheelData *)msg->IAddress;

				gui_window_set_scroll(gwin->bw->window,
					gwin->bw->window->scrollx + (wheel->WheelX * 50),
					gwin->bw->window->scrolly + (wheel->WheelY * 50));
			}
		break;

		case IDCMP_SIZEVERIFY:
		break;
	}
//	ReplyMsg((struct Message *)msg);
} 

/* return the text box at posn x,y in window coordinates
   x,y are updated to be document co-ordinates */

struct box *ami_text_box_at_point(struct gui_window_2 *gwin, ULONG *x, ULONG *y)
{
	struct IBox *bbox;
	ULONG xs,ys,width,height;
	struct box *box,*text_box=0;
	hlcache_handle *content;
	int box_x=0,box_y=0;

	content = gwin->bw->current_content;

	if(content_get_type(content) != CONTENT_HTML) return NULL;

	GetAttr(SPACE_AreaBox, (Object *)gwin->objects[GID_BROWSER],
				(ULONG *)&bbox);

	ami_get_hscroll_pos(gwin, (ULONG *)&xs);
	*x = *x - (bbox->Left) +xs;

	ami_get_vscroll_pos(gwin, (ULONG *)&ys);
	*y = *y - (bbox->Top) + ys;

	width=bbox->Width;
	height=bbox->Height;

	box = html_get_box_tree(content);
	while ((box = box_at_point(box, *x, *y, &box_x, &box_y, &content)))
	{
		if (box->style && css_computed_visibility(box->style) == CSS_VISIBILITY_HIDDEN)	continue;

		if (box->gadget)
		{
			switch (box->gadget->type)
			{
				case GADGET_TEXTBOX:
				case GADGET_TEXTAREA:
				case GADGET_PASSWORD:
					text_box = box;
				break;

				default:
				break;
			}
		}
	}
	return text_box;
}

BOOL ami_gadget_hit(Object *obj, int x, int y)
{
	int top, left, width, height;

	GetAttrs(obj,
		GA_Left, &left,
		GA_Top, &top,
		GA_Width, &width,
		GA_Height, &height,
		TAG_DONE);

	if((x >= left) && (x <= (left + width)) && (y >= top) && (y <= (top + height)))
		return TRUE;
	else return FALSE;
}

Object *ami_gui_splash_open(void)
{
	Object *win_obj;
	struct Window *win;
	struct Screen *wbscreen = LockPubScreen("Workbench");

	win_obj = WindowObject,
				WA_Title, "Initialising...",
				WA_ToolBox, TRUE,
				WA_BusyPointer, TRUE,
				WINDOW_Position, WPOS_CENTERSCREEN,
				WINDOW_LockWidth, TRUE,
				WINDOW_LockHeight, TRUE,
				WINDOW_ParentGroup, VGroupObject,
					LAYOUT_AddImage, BitMapObject,
						BITMAP_SourceFile, "PROGDIR:Resources/netsurf.png",
						BITMAP_Screen, wbscreen,
					BitMapEnd,
				LayoutEnd,
			EndWindow;

	win = RA_OpenWindow(win_obj);

	UnlockPubScreen(NULL, wbscreen);

	return win_obj;
}

void ami_gui_splash_close(Object *win_obj)
{
	if(win_obj) DisposeObject(win_obj);
}
