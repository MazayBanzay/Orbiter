// OrbiterCrew - the amplifier frame «Усилитель» (see Frame.h, Orbitersdk/OrbiterCrew/AMPLIFIER.md).
#include "Frame.h"
#include "Skin.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ocrew
{
	bool Frame::ApplyClass(const std::string& name)
	{
		const std::string path = "OrbiterCrew\\Frames\\" + name + ".cfg";
		FILEHANDLE f = oapiOpenFile(path.c_str(), FILE_IN_ZEROONFAIL, CONFIG);
		if (!f) { oapiWriteLogV("OrbiterCrew: frame class %s: no Config\\%s - the defaults", name.c_str(), path.c_str()); return false; }
		cls = name;
		char s[64] = "";
		if (oapiReadItem_string(f, const_cast<char*>("Title"), s)) title = s;
		double x;
		auto rd = [&](const char* k, double& out) { if (oapiReadItem_float(f, const_cast<char*>(k), x)) out = x; };
		rd("Mass", mass); rd("KneeCont", kneeCont); rd("KneePeak", kneePeak); rd("ArmsCont", armsCont); rd("ArmsPeak", armsPeak);
		rd("PeakS", peakS); rd("BalanceKg", balanceKg); rd("LiftKg", liftCap); rd("SupplyW", supplyW); rd("IdleW", idleW);
		if (oapiReadItem_float(f, const_cast<char*>("Battery"), x)) battCap = x * 3.6e6;
		oapiCloseFile(f, FILE_IN_ZEROONFAIL);
		batt = (std::min)(batt, battCap);
		return true;
	}

	// her own knee: 60 N*m on the move, 170 at most (a woman of 1.74 m, 62 kg); a stance knee in walking needs ~0.8 N*m per
	// kg of what it carries at 1 g (AMPLIFIER.md, п. 2)
	namespace { constexpr double HER_KNEE_CONT = 60, HER_KNEE_MAX = 170, NM_PER_KG = 0.8; }
	double Frame::LiftKg(double gRel, double ownKg) const
	{
		const double legs = (HER_KNEE_MAX + kneePeak) / NM_PER_KG - ownKg * gRel;   // kg of weight the legs lift on top of her
		return (std::max)(0.0, (std::min)(liftCap, legs));
	}
	double Frame::BalanceKg(double ownKg, double depth) const
	{
		const double ahead = 0.25 + depth / 2 - 0.27;      // the load's centre past the toe's edge
		return ahead <= 0.01 ? balanceKg : (std::min)(balanceKg, ownKg * (0.10 + 0.27) / ahead);
	}
	double Frame::CarryKg(double gRel, double ownKg) const
	{
		return LiftKg(gRel, ownKg);   // one limit: what it lifts it carries (slower and dearer near it - CrewMember::HeldSlow)
	}

	double Frame::Step(double dt, double drawW)
	{
		draw = Live() ? (std::min)(drawW + idleW, supplyW) : 0.0;
		const double need = draw * dt;
		if (batt >= need) { batt -= need; return 0.0; }
		const double rest = need - batt; batt = 0;
		return fromSuit ? rest : 0.0;
	}

	void Frame::LoadGeo()
	{
		grp.clear(); bound = nullptr;
		std::ifstream f("Config\\OrbiterCrew\\Usilitel.geo");
		if (!f || !tpl) { oapiWriteLog(const_cast<char*>("OrbiterCrew: frame: Config\\OrbiterCrew\\Usilitel.geo or its mesh missing - not drawn")); return; }
		std::vector<std::string> bone(oapiMeshGroupCount(tpl));
		std::string line;
		while (std::getline(f, line))
		{
			std::istringstream ss(line); std::string k; int i; std::string b;
			if ((ss >> k) && k == "GROUP" && (ss >> i >> b) && i >= 0 && i < static_cast<int>(bone.size())) bone[i] = b;
		}
		for (DWORD gi = 0; gi < bone.size(); ++gi)
		{
			Grp g; g.boneName = bone[gi];
			if (const MESHGROUP* m = oapiMeshGroup(tpl, gi); m && m->Vtx && m->nVtx)
			{
				g.rest.assign(m->Vtx, m->Vtx + m->nVtx);
				VECTOR3 c = _V(0, 0, 0);
				for (const NTVERTEX& q : g.rest) c += _V(q.x, q.y, q.z);
				g.c = c / static_cast<double>(g.rest.size());
			}
			grp.push_back(g);
		}
	}

	void Frame::Attach(VESSEL* vessel)
	{
		v = vessel; mesh = static_cast<UINT>(-1); shown = false;
		tpl = oapiLoadMeshGlobal("OrbiterCrew\\Usilitel");
		if (!tpl) { oapiWriteLog(const_cast<char*>("OrbiterCrew: frame: Meshes\\OrbiterCrew\\Usilitel.msh missing")); return; }
		LoadGeo();
		mesh = v->AddMesh(tpl);
		v->SetMeshVisibilityMode(mesh, MESHVIS_NEVER);
	}
	void Frame::Detach() { v = nullptr; mesh = static_cast<UINT>(-1); grp.clear(); bound = nullptr; shown = false; }

	void Frame::Pose(const Skin& skin, VISHANDLE vis)
	{
		if (!v || mesh == static_cast<UINT>(-1)) return;
		if (worn != shown) { v->SetMeshVisibilityMode(mesh, worn ? MESHVIS_ALWAYS : MESHVIS_NEVER); shown = worn; }
		if (!worn || !vis) return;
		DEVMESHHANDLE dev = v->GetDevMesh(vis, mesh);
		if (!dev) return;
		if (bound != &skin) { for (Grp& g : grp) g.bone = skin.Bone(g.boneName.c_str()); bound = &skin; }
		std::vector<NTVERTEX> out;
		for (DWORD gi = 0; gi < grp.size(); ++gi)
		{
			const Grp& g = grp[gi];
			if (g.rest.empty() || g.bone < 0) continue;
			// the bone's frame now: three of its rest points round the group's centre
			const VECTOR3 P0 = skin.Point(g.bone, g.c);
			const VECTOR3 xr = skin.Point(g.bone, g.c + _V(0.1, 0, 0)) - P0, yr = skin.Point(g.bone, g.c + _V(0, 0.1, 0)) - P0;
			if (length(xr) < 1e-6 || length(yr) < 1e-6) continue;
			const VECTOR3 X = unit(xr), Z = unit(crossp(X, yr)), Y = crossp(Z, X);
			out = g.rest;
			for (NTVERTEX& q : out)
			{
				const VECTOR3 d = _V(q.x, q.y, q.z) - g.c, n = _V(q.nx, q.ny, q.nz);
				const VECTOR3 p = P0 + X * d.x + Y * d.y + Z * d.z, m = X * n.x + Y * n.y + Z * n.z;
				q.x = static_cast<float>(p.x); q.y = static_cast<float>(p.y); q.z = static_cast<float>(p.z);
				q.nx = static_cast<float>(m.x); q.ny = static_cast<float>(m.y); q.nz = static_cast<float>(m.z);
			}
			GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; e.Vtx = out.data(); e.nVtx = static_cast<DWORD>(out.size());
			oapiEditMeshGroup(dev, gi, &e);
		}
	}
}
