#include <3ds.h>

// Define basic device information
u8 consoleModel = 0;
u8 sysRegion = CFG_REGION_USA;
u64 appID = 0;

// ------------------------------------
// gather device information
// ------------------------------------
void gatherInfo()
{
	Result res = cfguInit();
	if (R_SUCCEEDED(res))
	{
		CFGU_SecureInfoGetRegion(&sysRegion);
		CFGU_GetSystemModel(&consoleModel);
		cfguExit();
	}

	aptInit();
	APT_GetProgramID(&appID);
	aptExit();
}

// ------------------------------------
// Turn screen off
// ------------------------------------
void screenoff()
{
	gspLcdInit();
	GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
	gspLcdExit();
}

// ------------------------------------
// Turn screen on
// ------------------------------------
void screenon()
{
	gspLcdInit();
	GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
	gspLcdExit();
}

// ------------------------------------
// Main process
// ------------------------------------
int main()
{
	// turn off screen for initialize app
	screenoff();
	amInit();

	// get device information
	gatherInfo();

	// init gfx
	gfxInitDefault();

	return 0;
}
