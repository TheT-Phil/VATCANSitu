/*	Drawing on the main radar screen is done with this file
	This draws:
	1. CSiT Tools Menu
	2. VFR radar target
	3. Mouse halo if enabled
	4. Aircraft specific halos if enabled
	5. PTLS for aircrafts if enabled
*/

#include "pch.h"
#include "CSiTRadar.h"
#include "HaloTool.h"
#include "constants.h"
#include "TopMenu.h"
#include "SituPlugin.h"
#include "GndRadar.h"
#include <chrono>

using namespace Gdiplus;


CSiTRadar::CSiTRadar()
{
	halfSec = clock();
}

CSiTRadar::~CSiTRadar()
{
}

void CSiTRadar::OnRefresh(HDC hdc, int phase)
{

	// get cursor position and screen info
	POINT p;

	if (GetCursorPos(&p)) {
		if (ScreenToClient(GetActiveWindow(), &p)) {}
	}

	RECT radarea = GetRadarArea();
	
	// time based functions
	double time = ((double)clock() - (double)halfSec) / ((double)CLOCKS_PER_SEC);
	if (time >= 0.5) {
		halfSec = clock();
		halfSecTick = !halfSecTick;
	}

	// set up the drawing renderer
	CDC dc;
	dc.Attach(hdc);

	Graphics g(hdc);

	int pixnm = PixelsPerNM();

	if (phase == REFRESH_PHASE_AFTER_TAGS) {

		// Draw the mouse halo before menu, so it goes behind it
		if (mousehalo == TRUE) {
			HaloTool::drawHalo(dc, p, halorad, pixnm);
			RequestRefresh();
		}

		// add orange PPS to aircrafts with VFR Flight Plans that have correlated targets
		// iterate over radar targets

		for (CRadarTarget radarTarget = GetPlugIn()->RadarTargetSelectFirst(); radarTarget.IsValid();
			radarTarget = GetPlugIn()->RadarTargetSelectNext(radarTarget))
		{
			// altitude filtering 
			if (altFilterOn && radarTarget.GetPosition().GetPressureAltitude() < altFilterLow * 100) {
				continue;
			}

			if (altFilterOn && altFilterHigh > 0 && radarTarget.GetPosition().GetPressureAltitude() > altFilterHigh * 100) {
				continue;
			}

			// aircraft equipment parsing
			string icaoACData = radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetAircraftInfo();
			regex icaoRVSM("(.*)\\/(.*)\\-(.*)[W](.*)\\/(.*)", regex::icase);
			bool isRVSM = regex_search(icaoACData, icaoRVSM);
			regex icaoADSB("(.*)\\/(.*)\\-(.*)\\/(.*)(E|L|B1|B2|U1|U2|V1|V2)(.*)");
			bool isADSB = regex_search(icaoACData, icaoADSB);

			// get the target's position on the screen and add it as a screen object
			POINT p = ConvertCoordFromPositionToPixel(radarTarget.GetPosition().GetPosition());
			RECT prect;
			prect.left = p.x - 5;
			prect.top = p.y - 5;
			prect.right = p.x + 5;
			prect.bottom = p.y + 5;
			AddScreenObject(AIRCRAFT_SYMBOL, radarTarget.GetCallsign(), prect, FALSE, "");

			// Handoff warning system: if the plane is within 2 minutes of exiting your airspace, CJS will blink

			if (radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
				if (radarTarget.GetCorrelatedFlightPlan().GetSectorExitMinutes() <= 2 
					&& radarTarget.GetCorrelatedFlightPlan().GetSectorExitMinutes() >= 0) {
					// blink the CJS
					string callsign = radarTarget.GetCallsign();
					isBlinking[callsign] = TRUE;
				}
			}
			else {
				string callsign = radarTarget.GetCallsign();

				isBlinking.erase(callsign);
			}

			// if in the process of handing off, flash the PPS (to be added), CJS and display the frequency 
			if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") != 0
				&& radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()
				) {
				string handOffFreq = "-" + to_string(GetPlugIn()->ControllerSelectByPositionId(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId()).GetPrimaryFrequency()).substr(0,6);
				string handOffCJS = radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId();

				string handOffText = handOffCJS + handOffFreq;

				CFont font;
				LOGFONT lgfont;

				memset(&lgfont, 0, sizeof(LOGFONT));
				lgfont.lfWeight = 500;
				strcpy_s(lgfont.lfFaceName, _T("EuroScope"));
				lgfont.lfHeight = 12;
				font.CreateFontIndirect(&lgfont);

				dc.SetTextColor(RGB(255, 255, 255));

				dc.SelectObject(font);
				if (isBlinking.find(radarTarget.GetCallsign()) != isBlinking.end()
					&& halfSecTick) {
					handOffText=""; // blank CJS symbol drawing when blinked out
				}

				RECT rectCJS;
				rectCJS.left = p.x - 6;
				rectCJS.right = p.x + 75;
				rectCJS.top = p.y - 18;
				rectCJS.bottom = p.y;

				dc.DrawText(handOffText.c_str(), &rectCJS, DT_LEFT);

				DeleteObject(font);
			}
			else {

				// show CJS for controller tracking aircraft
				string CJS = radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerId();

				CFont font;
				LOGFONT lgfont;

				memset(&lgfont, 0, sizeof(LOGFONT));
				lgfont.lfWeight = 500;
				strcpy_s(lgfont.lfFaceName, _T("EuroScope"));
				lgfont.lfHeight = 12;
				font.CreateFontIndirect(&lgfont);

				dc.SelectObject(font);
				dc.SetTextColor(RGB(202, 205, 169));

				RECT rectCJS;
				rectCJS.left = p.x - 6 ;
				rectCJS.right = p.x + 75;
				rectCJS.top = p.y - 18;
				rectCJS.bottom = p.y;

				dc.DrawText(CJS.c_str(), &rectCJS, DT_LEFT);

				DeleteObject(font);
			}

			// plane halo looks at the <map> hashalo to see if callsign has a halo, if so, draws halo
			if (hashalo.find(radarTarget.GetCallsign()) != hashalo.end()) {
				HaloTool::drawHalo(dc, p, halorad, pixnm);
			}

			// if squawking ident, PPS blinks -- skips drawing symbol every 0.5 seconds
			if (radarTarget.GetPosition().GetTransponderI()
				&& radarTarget.GetPosition().GetRadarFlags() != 0) {
				
				if (halfSecTick) {
					continue;
				}
			}

			// Draw red triangle for emergency aircraft

			if (!strcmp(radarTarget.GetPosition().GetSquawk(), "7600") ||
				!strcmp(radarTarget.GetPosition().GetSquawk(), "7700")) {

				COLORREF targetPenColor;
				targetPenColor = RGB(209, 39, 27); // Red
				HPEN targetPen;
				HBRUSH targetBrush;
				targetBrush = CreateSolidBrush(RGB(209, 39, 27));
				targetPen = CreatePen(PS_SOLID, 1, targetPenColor);

				dc.SelectObject(targetPen);
				dc.SelectObject(targetBrush);

				// draw the shape

				POINT vertices[] = { { p.x - 3, p.y + 3 } , { p.x, p.y - 3 } , { p.x + 3,p.y + 3 } };
				dc.Polygon(vertices, 3);

				DeleteObject(targetBrush);
				DeleteObject(targetPen);

				continue;
			}

			// ADSB targets; if no primary or secondary radar, but the plane has ADSB equipment suffix (assumed space based ADS-B with no gaps)

			if (radarTarget.GetPosition().GetRadarFlags() == 0
				&& isADSB) { // need to add ADSB equipment logic -- currently based on filed FP; no tag will display though. WIP

				COLORREF targetPenColor;
				targetPenColor = RGB(202, 205, 169); // amber colour
				HPEN targetPen;
				targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
				dc.SelectObject(targetPen);
				dc.SelectStockObject(NULL_BRUSH);

				// draw the shape
				dc.MoveTo(p.x - 5, p.y - 5);
				dc.LineTo(p.x + 5, p.y -5);
				dc.LineTo(p.x + 5, p.y + 5);
				dc.LineTo(p.x - 5, p.y + 5);
				dc.LineTo(p.x - 5, p.y - 5);

				// if primary and secondary target, draw the middle line
				if (isRVSM) {
					dc.MoveTo(p.x, p.y - 5);
					dc.LineTo(p.x, p.y + 5);
				}

				// cleanup
				DeleteObject(targetPen);
			}

			// if primary target draw the symbol in magenta

			if (radarTarget.GetPosition().GetRadarFlags() == 1) {
				COLORREF targetPenColor;
				targetPenColor = RGB(197, 38, 212); // magenta colour
				HPEN targetPen;
				targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
				dc.SelectObject(targetPen);
				dc.SelectStockObject(NULL_BRUSH);

				// draw the shape
				dc.MoveTo(p.x, p.y + 4);
				dc.LineTo(p.x, p.y);
				dc.LineTo(p.x - 4, p.y - 4);
				dc.MoveTo(p.x, p.y);
				dc.LineTo(p.x + 4, p.y - 4);

				// cleanup
				DeleteObject(targetPen);

			}

			// if RVSM draw the RVSM diamond

			if ((radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetCapibilities() == 'L' || 
				radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetCapibilities() == 'W' ||
				radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetCapibilities() == 'Z' || // FAA RVSM
				isRVSM) // ICAO equpmnet code indicates RVSM -- contains 'W'

				&& radarTarget.GetPosition().GetRadarFlags() != 0 && 
				radarTarget.GetPosition().GetRadarFlags() != 1) {

				COLORREF targetPenColor;
				targetPenColor = RGB(202, 205, 169); // amber colour
				HPEN targetPen;
				targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
				dc.SelectObject(targetPen);
				dc.SelectStockObject(NULL_BRUSH);

				// draw the shape
				dc.MoveTo(p.x, p.y - 5);
				dc.LineTo(p.x + 5, p.y);
				dc.LineTo(p.x, p.y + 5);
				dc.LineTo(p.x - 5, p.y);
				dc.LineTo(p.x, p.y - 5);

				// if primary and secondary target, draw the middle line
				if (radarTarget.GetPosition().GetRadarFlags() == 3) {
					dc.MoveTo(p.x, p.y - 5);
					dc.LineTo(p.x, p.y + 5);
				}

				DeleteObject(targetPen);
			}
			else {

				if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetPlanType(), "I") == 0 
					&& radarTarget.GetPosition().GetRadarFlags() != 0
					&& radarTarget.GetPosition().GetRadarFlags() != 1) {
					COLORREF targetPenColor;
					targetPenColor = RGB(202, 205, 169); // white when squawking ident
					HPEN targetPen;
					targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
					dc.SelectObject(targetPen);
					dc.SelectStockObject(NULL_BRUSH);

					dc.SelectObject(targetPen);

					// Hexagon for secondary
					dc.MoveTo(p.x - 4, p.y - 2);
					dc.LineTo(p.x - 4, p.y + 2);
					dc.LineTo(p.x, p.y + 5);
					dc.LineTo(p.x + 4, p.y + 2);
					dc.LineTo(p.x + 4, p.y - 2);
					dc.LineTo(p.x, p.y - 5);
					dc.LineTo(p.x - 4, p.y - 2);

					// Triangle for primary
					if (radarTarget.GetPosition().GetRadarFlags() == 3) {
						dc.MoveTo(p.x - 4, p.y + 2);
						dc.LineTo(p.x, p.y - 4);
						dc.LineTo(p.x + 4, p.y + 2);
						dc.LineTo(p.x - 4, p.y + 2);
					}

					// cleanup
					DeleteObject(targetPen);
				}
			}

			
			// if VFR
			if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetPlanType(), "V") == 0
				&& radarTarget.GetPosition().GetTransponderC() == TRUE
				&& radarTarget.GetPosition().GetRadarFlags() != 0
				&& radarTarget.GetPosition().GetRadarFlags() != 1) {

				COLORREF targetPenColor;
				targetPenColor = RGB(242, 120, 57); // PPS orange color
				HPEN targetPen;
				targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
				dc.SelectObject(targetPen);
				dc.SelectStockObject(NULL_BRUSH);

				// draw the shape
				dc.Ellipse(p.x - 4, p.y - 4, p.x + 6, p.y + 6);

				dc.SelectObject(targetPen);
				dc.MoveTo(p.x - 3, p.y - 2);
				dc.LineTo(p.x + 1, p.y + 4);
				dc.LineTo(p.x + 4, p.y - 2);

				DeleteObject(targetPen);
			}

			// if ptl tag applied, draw it => not implemented

		}

		// Flight plan loop. Goes through flight plans, and if not correlated will display
		for (CFlightPlan flightPlan = GetPlugIn()->FlightPlanSelectFirst(); flightPlan.IsValid();
			flightPlan = GetPlugIn()->FlightPlanSelectNext(flightPlan)) {
			
			// if the flightplan does not have a correlated radar target
			if (!flightPlan.GetCorrelatedRadarTarget().IsValid()
				&& flightPlan.GetFPState() == FLIGHT_PLAN_STATE_SIMULATED) {

				// convert the predicted position to a point on the screen
				POINT p = ConvertCoordFromPositionToPixel(flightPlan.GetFPTrackPosition().GetPosition());

				// draw the orange airplane symbol (credits andrewogden1678)
				GraphicsContainer gCont;
				gCont =  g.BeginContainer();

				// Airplane icon 

				Point points[19] = {
					Point(0,-6),
					Point(-1,-5),
					Point(-1,-2),
					Point(-8,3),
					Point(-8,4),
					Point(-1,2),
					Point(-1,6),
					Point(-4,8),
					Point(-4,9),
					Point(0,8),
					Point(4,9),
					Point(4,8),
					Point(1,6),
					Point(1,2),
					Point(8,4),
					Point(8,3),
					Point(1,-2),
					Point(1,-5),
					Point(0,-6)
				};

				g.RotateTransform((REAL)flightPlan.GetFPTrackPosition().GetReportedHeading());
				g.TranslateTransform((REAL)p.x, (REAL)p.y, MatrixOrderAppend);

				// Fill the shape

				SolidBrush orangeBrush(Color(255, 242, 120, 57));
				COLORREF targetPenColor;
				targetPenColor = RGB(242, 120, 57); // PPS orange color

				g.FillPolygon(&orangeBrush, points, 19);
				g.EndContainer(gCont);

				DeleteObject(&orangeBrush);

				
			}

		}


		// Draw the CSiT Tools Menu; starts at rad area top left then moves right
		// this point moves to the origin of each subsequent area
		POINT menutopleft = CPoint(radarea.left, radarea.top); 

		TopMenu::DrawBackground(dc, menutopleft, radarea.right, 60);
		RECT but;

		// small amount of padding;
		menutopleft.y += 6;
		menutopleft.x += 10;

		// screen range, dummy buttons, not really necessary in ES.
		TopMenu::DrawButton(dc, menutopleft, 70, 23, "Relocate", 0);
		menutopleft.y += 25;

		TopMenu::DrawButton(dc, menutopleft, 35, 23, "Zoom", 0); 
		menutopleft.x += 35;
		TopMenu::DrawButton(dc, menutopleft, 35, 23, "Pan", 0);
		menutopleft.y -= 25;
		menutopleft.x += 55;
		
		// horizontal range calculation
		int range = (int)round(RadRange());
		string rng = to_string(range);
		TopMenu::MakeText(dc, menutopleft, 50, 15, "Range");
		menutopleft.y += 15;

		// 109 pix per in on my monitor
		int nmIn = 109 / pixnm;
		string nmtext = "1\" = " + to_string(nmIn) + "nm";
		TopMenu::MakeText(dc, menutopleft, 50, 15, nmtext.c_str());
		menutopleft.y += 17;

		TopMenu::MakeDropDown(dc, menutopleft, 40, 15, rng.c_str());

		menutopleft.x += 80;
		menutopleft.y -= 32;

		// altitude filters

		but = TopMenu::DrawButton(dc, menutopleft, 50, 23, "Alt Filter", altFilterOpts);
		ButtonToScreen(this, but, "Alt Filt Opts", BUTTON_MENU_ALT_FILT_OPT);
		
		menutopleft.y += 25;

		string altFilterLowFL = to_string(altFilterLow);
		altFilterLowFL.insert(altFilterLowFL.begin(), 3 - altFilterLowFL.size(), '0');
		string altFilterHighFL = to_string(altFilterHigh);
		altFilterHighFL.insert(altFilterHighFL.begin(), 3 - altFilterHighFL.size(), '0');

		string filtText = altFilterLowFL + string(" - ") + altFilterHighFL;
		but = TopMenu::DrawButton(dc, menutopleft, 50, 23, filtText.c_str(), altFilterOn);
		ButtonToScreen(this, but, "", BUTTON_MENU_ALT_FILT_ON);
		menutopleft.y -= 25;
		menutopleft.x += 65; 

		// separation tools
		string haloText = "Halo " + halooptions[haloidx];
		but = TopMenu::DrawButton(dc, menutopleft, 45, 23, haloText.c_str(), halotool);
		ButtonToScreen(this, but, "Halo", BUTTON_MENU_HALO_OPTIONS);

		menutopleft.y = menutopleft.y + 25;
		but = TopMenu::DrawButton(dc, menutopleft, 45, 23, "PTL 3", 0);
		ButtonToScreen(this, but, "PTL", BUTTON_MENU_HALO_OPTIONS);

		menutopleft.y = menutopleft.y - 25;
		menutopleft.x = menutopleft.x + 47;
		TopMenu::DrawButton(dc, menutopleft, 35, 23, "RBL", 0);

		menutopleft.y = menutopleft.y + 25;
		TopMenu::DrawButton(dc, menutopleft, 35, 23, "PIV", 0);

		menutopleft.y = menutopleft.y - 25;
		menutopleft.x = menutopleft.x + 37;
		TopMenu::DrawButton(dc, menutopleft, 50, 23, "Rings 20", 0);

		menutopleft.y = menutopleft.y + 25;
		TopMenu::DrawButton(dc, menutopleft, 50, 23, "Grid", 0);

		// get the controller position ID and display it (aesthetics :) )
		if (GetPlugIn()->ControllerMyself().IsValid())
		{
			controllerID = GetPlugIn()->ControllerMyself().GetPositionId();
		}

		menutopleft.y -= 25;
		menutopleft.x += 60;
		string cid = "CJS - " + controllerID;

		RECT r = TopMenu::DrawButton2(dc, menutopleft, 50, 23, cid.c_str(), 0);

		menutopleft.y += 25;
		TopMenu::DrawButton(dc, menutopleft, 50, 23, "Qck Look", 0);
		menutopleft.y -= 25;

		menutopleft.x = menutopleft.x + 100;

		// options for halo radius
		if (halotool) {
			TopMenu::DrawHaloRadOptions(dc, menutopleft, halorad, halooptions);
			RECT rect;
			RECT r;

			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "End", FALSE);
			ButtonToScreen(this, r, "End", BUTTON_MENU_HALO_OPTIONS);
			menutopleft.x += 35;

			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "All On", FALSE);
			ButtonToScreen(this, r, "All On", BUTTON_MENU_HALO_OPTIONS);
			menutopleft.x += 35;
			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "Clr All", FALSE);
			ButtonToScreen(this, r, "Clr All", BUTTON_MENU_HALO_OPTIONS);
			menutopleft.x += 35;

			for (int idx = 0; idx < 9; idx++) {

				rect.left = menutopleft.x;
				rect.top = menutopleft.y + 31;
				rect.right = menutopleft.x + 127;
				rect.bottom = menutopleft.y + 46;
				string key = to_string(idx);
				AddScreenObject(BUTTON_MENU_HALO_OPTIONS, key.c_str(), rect, 0, "");
				menutopleft.x += 22;
			}
			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "Mouse", mousehalo);
			ButtonToScreen(this, r, "Mouse", BUTTON_MENU_HALO_OPTIONS);
		}

		// options for the altitude filter sub menu
		
		if (altFilterOpts) {
			
			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "End", FALSE);
			ButtonToScreen(this, r, "End", BUTTON_MENU_ALT_FILT_OPT);
			menutopleft.x += 45;
			menutopleft.y += 5;
			
			r = TopMenu::MakeText(dc, menutopleft, 55, 15, "High Lim");
			menutopleft.x += 55;
			rHLim = TopMenu::MakeField(dc, menutopleft, 55, 15, altFilterHighFL.c_str());
			AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "HLim", rHLim, 0, "");

			menutopleft.x -= 55; menutopleft.y += 20;
			
			TopMenu::MakeText(dc, menutopleft, 55, 15, "Low Lim");
			menutopleft.x += 55;
			rLLim = TopMenu::MakeField(dc, menutopleft, 55, 15, altFilterLowFL.c_str());
			AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "LLim", rLLim, 0, "");

			menutopleft.x += 75;
			menutopleft.y -= 25;
			r = TopMenu::DrawButton(dc, menutopleft, 35, 46, "Save", FALSE);
			AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "Save", r, 0, "");

		}
/*
		// Ground Radar Tags WIP

		for (CRadarTarget rt = GetPlugIn()->RadarTargetSelectFirst(); rt.IsValid(); rt = GetPlugIn()->RadarTargetSelectNext(rt))
		{
			if (!rt.IsValid())
				continue;

			if (strcmp(rt.GetCorrelatedFlightPlan().GetFlightPlanData().GetDestination(), "CYYZ")) {

				POINT p = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
				GndRadar::DrawGndTag(dc, p, 0, rt, rt.GetCallsign());
			}
		}

*/
	}
	g.ReleaseHDC(hdc);
	dc.Detach();
}

void CSiTRadar::OnClickScreenObject(int ObjectType,
	const char* sObjectId,
	POINT Pt,
	RECT Area,
	int Button)
{
	if (ObjectType == AIRCRAFT_SYMBOL && halotool == TRUE) {
		
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		string callsign = rt.GetCallsign();

		if (hashalo.find(callsign) != hashalo.end()) {
			hashalo.erase(callsign);
		}
		else {
			hashalo[callsign] = TRUE;
		}
	}

	if (ObjectType == BUTTON_MENU_HALO_OPTIONS) {
		if (!strcmp(sObjectId, "0")) { halorad = 0.5; haloidx = 0; }
		if (!strcmp(sObjectId, "1")) { halorad = 3; haloidx = 1; }
		if (!strcmp(sObjectId, "2")) { halorad = 5; haloidx = 2; }
		if (!strcmp(sObjectId, "3")) { halorad = 10; haloidx = 3; }
		if (!strcmp(sObjectId, "4")) { halorad = 15; haloidx = 4; }
		if (!strcmp(sObjectId, "5")) { halorad = 20; haloidx = 5; }
		if (!strcmp(sObjectId, "6")) { halorad = 30; haloidx = 6; }
		if (!strcmp(sObjectId, "7")) { halorad = 60; haloidx = 7; }
		if (!strcmp(sObjectId, "8")) { halorad = 80; haloidx = 8; }
		if (!strcmp(sObjectId, "Clr All")) { hashalo.clear(); }
		if (!strcmp(sObjectId, "End")) { halotool = !halotool; }
		if (!strcmp(sObjectId, "Mouse")) { mousehalo = !mousehalo; }
		if (!strcmp(sObjectId, "Halo")) { halotool = !halotool; }
	}

	if (ObjectType == BUTTON_MENU_ALT_FILT_OPT) {
		if (!strcmp(sObjectId, "Alt Filt Opts")) { altFilterOpts = !altFilterOpts; }
		if (!strcmp(sObjectId, "End")) { altFilterOpts = 0; }
		if (!strcmp(sObjectId, "LLim")) {
			string altFilterLowFL = to_string(altFilterLow);
			altFilterLowFL.insert(altFilterLowFL.begin(), 3 - altFilterLowFL.size(), '0');
			GetPlugIn()->OpenPopupEdit(rLLim, FUNCTION_ALT_FILT_LOW, altFilterLowFL.c_str());
		}
		if (!strcmp(sObjectId, "HLim")) {
			string altFilterHighFL = to_string(altFilterHigh);
			altFilterHighFL.insert(altFilterHighFL.begin(), 3 - altFilterHighFL.size(), '0');
			GetPlugIn()->OpenPopupEdit(rHLim, FUNCTION_ALT_FILT_HIGH, altFilterHighFL.c_str());
		}
		if (!strcmp(sObjectId, "Save")) {
			string s = to_string(altFilterHigh);
			SaveDataToAsr("altFilterHigh", "Alt Filter High Limit", s.c_str());
			s = to_string(altFilterLow);
			SaveDataToAsr("altFilterLow", "Alt Filter Low Limit", s.c_str());
			altFilterOpts = 0;
		}
	}

	if (ObjectType == BUTTON_MENU_ALT_FILT_ON) {
		altFilterOn = !altFilterOn;
	}

	
	
	if (Button == BUTTON_MIDDLE) {
		// open Free Text menu

		RECT freeTextPopUp;
		freeTextPopUp.left = Pt.x;
		freeTextPopUp.top = Pt.y;
		freeTextPopUp.right = Pt.x + 20;
		freeTextPopUp.bottom = Pt.y + 10;

		GetPlugIn()->OpenPopupList(freeTextPopUp, "Free Text", 1);

		GetPlugIn()->AddPopupListElement("ADD FREE TEXT", "", ADD_FREE_TEXT);
		GetPlugIn()->AddPopupListElement("DELETE", "", DELETE_FREE_TEXT, FALSE, POPUP_ELEMENT_NO_CHECKBOX, true, false);
		GetPlugIn()->AddPopupListElement("DELETE ALL", "", DELETE_ALL_FREE_TEXT);

	}


}

void CSiTRadar::OnFunctionCall(int FunctionId,
	const char* sItemString,
	POINT Pt,
	RECT Area) {
	if (FunctionId == FUNCTION_ALT_FILT_LOW) {
		try {
			altFilterLow = stoi(sItemString);
		}
		catch (...) {}
	}
	if (FunctionId == FUNCTION_ALT_FILT_HIGH) {
		try {
			altFilterHigh = stoi(sItemString);
		}
		catch (...) {}
	}
}

void CSiTRadar::ButtonToScreen(CSiTRadar* radscr, RECT rect, string btext, int itemtype) {
	AddScreenObject(itemtype, btext.c_str(), rect, 0, "");
}

void CSiTRadar::OnAsrContentLoaded(bool Loaded) {
	const char* filt = nullptr;

	// if (GetDataFromAsr("tagfamily")) { radtype = GetDataFromAsr("tagfamily"); }

	// getting altitude filter information
	if ((filt = GetDataFromAsr("altFilterHigh")) != NULL) {
		altFilterHigh = atoi(filt);
	}
	if ((filt = GetDataFromAsr("altFilterLow")) != NULL) {
		altFilterLow = atoi(filt);
	}
}

void CSiTRadar::OnAsrContentToBeSaved() {

}