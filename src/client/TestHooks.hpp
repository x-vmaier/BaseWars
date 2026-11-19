#pragma once
// Screenshot / scripted test hooks driven by environment variables (only active with BW_SHOT):
//   BW_SHOT=<file.png>   save a frame after BW_SHOT_FRAMES (default 400) frames, then quit
//   BW_CMD=chat:...|clan:<name>|xfer:<id>   scripted commands      BW_MOUSE=dx,dy   mouse offset from the centre
//   BW_SITE=<verb>:<args>@<frame>;...   site commands: build:<kind>:<dx>,<dy> (tiles from the own Town Hall),
//            upgrade:<kind>, repair:<kind>, cancel:<kind> (first own building of that kind), ctx:<kind> (open its panel),
//            donate:<faction>:<res> (50 of a resource), alloc:<res>:<pct>, dispose:<res>, quota:<w>,<a>,<l>,<m>
//   BW_UI=build,place,social,map,pause,train,ctx,ctxop,ctxfac,townhall,chatbox   open panels (BW_TAB, BW_SEARCH, BW_MAPZ, BW_TSEL,
//            BW_THTAB=0..2 Town Hall tab, BW_THHOVER=<control> hover, BW_THCLICK=1 confirm state; see townHallHook)
// Also read elsewhere: BW_CAM / BW_ZOOM (S_Welcome), BW_SCALE / BW_MENU (main).
class App;

class TestHooks
{
public:
    explicit TestHooks(App &app);
    bool active() const { return shotPath != nullptr; }
    void update();       // before drawing: scripted commands and UI state
    void siteCommands(); // BW_SITE
    void townHallHook(); // BW_UI=townhall
    bool captureIfDue(); // after drawing the UI: true when the screenshot was taken (quit)

private:
    App &app;
    const char *shotPath;
    int shotFrames = 0, shotAfter;
};
