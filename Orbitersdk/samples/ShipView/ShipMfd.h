// ShipView::MfdBank - real Orbiter MFDs on the consoles of our ships, with the camera on a person (common to all our vessels).
//
// Orbiter draws the MFDs of a virtual cockpit only for the vessel in focus; in our ships the focus is the person. So each
// console MFD is an ExternMFD stuck to the ship: its display is copied (with the button labels round it) into a render
// surface that replaces the texture of that MFD's mesh group. The buttons are the ship's 3D buttons, pressed with the mouse
// (OrbiterCrew OC_BUTTON) and passed to Press(mfd, button): 0..5 left, 6..11 right (top to bottom), 12 PWR, 13 SEL, 14 MNU.
//
// Touch MFDs (the user's rule: every screen is touch): the display fills the square screen, the button labels are drawn on its
// left / right edges and PWR / SEL / MNU along the bottom; a touch there presses the button (Touch, HitButton).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include <vector>

namespace shipview {

class MfdBank {
public:
    void Init(VESSEL* ship, UINT meshIdx) { v_ = ship; mesh_ = meshIdx; }
    int Add(DWORD texSlot, int mode);             // a console MFD (texture slot of its group, the starting mode MFD_*)
    void Start();                                 // clbkPostCreation: register the MFDs with Orbiter
    void OnVisualCreated(VISHANDLE vis);          // bind the surfaces to the mesh
    void OnVisualDestroyed() { vis_ = nullptr; }
    void Step();                                  // every step: redraw the MFDs that changed
    void Press(int mfd, int button);              // a button pressed with the mouse
    const char* Label(int mfd, int button) const; // its current label ("" if none)
    int Count() const { return int(m_.size()); }
    SURFHANDLE Display(int i) const;            // the MFD's display surface (Res() x Res()): for a unit drawn elsewhere (slot 0)
    int Res() const { return res_; }             // the MFDs' resolution (px)
    void SetRes(int px);                          // 1024 / 2048: the MFDs registered again at it
    bool Touch(int mfd, double u, double v);      // a touch on the screen (u right, v down, 0..1): its edge buttons
    static int HitButton(double u, double v);     // the button under (u, v), -1 none
    void Shutdown();                              // the vessel's destructor

    class Sticky;                                 // the ExternMFD stuck to the ship
private:
    struct Unit { DWORD slot; int mode; Sticky* mfd = nullptr; SURFHANDLE tex = nullptr; };
    void Compose(Unit& u);

    VESSEL* v_ = nullptr;
    UINT mesh_ = 0;
    VISHANDLE vis_ = nullptr;
    std::vector<Unit> m_;
    oapi::Font* font_ = nullptr;
    bool started_ = false;
    int res_ = 1024;
};

}  // namespace shipview
