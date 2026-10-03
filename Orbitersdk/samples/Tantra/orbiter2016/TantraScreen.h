// TantraScreen: the bridge of the Tantra.
//   - the big concave screen: live outside view in three zones (ShipView::ViewScreen, common to all our ships);
//   - the button on the commander's armrest switches the big screen on/off;
//   - the Orbiter MFD on the commander's console (group bridge_mfd of TantraVC.msh).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "ViewScreen.h"

class TantraScreen {
public:
    void Init(VESSEL* v, UINT vcMeshIdx);           // clbkSetClassCaps, after the VC mesh is added
    void OnVisual(VISHANDLE vis);                   // clbkVisualCreated
    void OnVisualGone() { view_.OnVisualDestroyed(); mesh_ = nullptr; }
    void OnLoadVC(double meshDZ);                   // register the MFD and the button area
    void Update(double meshDZ);                     // follow the CG shift
    bool OnMouse(int id, int event);                // click on the button
    void SetViewer(OBJHANDLE body) { view_.SetHost(body); view_.Frame(); }   // every step: the viewer (a person's body in focus, nullptr = the ship), telescope
    void Shutdown() { view_.Shutdown(); }
    void SetHud(shipview::ViewScreen::Overlay fn, void* ctx) { view_.SetZoneOverlay(0, fn, ctx); }   // drawn over the front picture (after Init)

private:
    void Place(double meshDZ);
    void Lamps();

    VESSEL* v_ = nullptr;
    UINT vcMesh_ = 0;
    DEVMESHHANDLE mesh_ = nullptr;
    shipview::ViewScreen view_;
    bool registered_ = false;
    int tele_ = -1;                                 // the astronomer's telescope zone
    double dz_ = 1e9;
};
