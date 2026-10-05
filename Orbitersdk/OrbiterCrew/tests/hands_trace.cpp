// OrbiterCrew - hands trace: the seated figure's own code (Skin, Motion, SeatArms) run without Orbiter, in the commander's
// seat of «Тантра» at the desk, with the hand targets the ship gives (OcInteriorExt::SeatHands): the trackball under her
// left palm, the right armrest's end, the yoke's horns (its hub moved as TantraYoke.cpp moves it). Each snapshot writes
// the skinned mesh as the skin writes it into the device mesh (<out>\<name>.f32, and <name>_fp.f32 with the head hidden
// as through her eyes) and a line of scenes.txt for tests\seat_view.py; the arms' diagnostics go to hands_trace.log.
// Build: tests\build_hands.cmd. Run from the Orbiter root: Orbitersdk\OrbiterCrew\tests\hands_trace.exe <out dir> [hub ahead]
#include "../src/Motion.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ocrew;

// ---- the SDK's mesh functions, as the skin uses them ----
namespace
{
	struct Mesh { std::vector<std::vector<NTVERTEX>> v; std::vector<MESHGROUP> g; };
	std::map<std::string, Mesh> meshes;
	std::vector<std::vector<NTVERTEX>> captured;   // what the skin wrote, per group
	FILE* logOut = stdout;
}
void oapiWriteLogV(const char* fmt, ...) { va_list a; va_start(a, fmt); vfprintf(logOut, fmt, a); va_end(a); fputc('\n', logOut); }
MESHHANDLE oapiLoadMeshGlobal(const char* name)
{
	Mesh& m = meshes[name];
	if (m.v.empty())
	{
		std::ifstream f(std::string("Meshes\\") + name + ".msh");
		std::string line;
		while (std::getline(f, line))
		{
			std::istringstream ss(line); std::string tok; ss >> tok;
			if (tok != "GEOM") continue;
			int nv = 0, nt = 0; ss >> nv >> nt;
			std::vector<NTVERTEX> vs(nv);
			for (int i = 0; i < nv; ++i) { std::getline(f, line); std::istringstream vl(line); NTVERTEX& p = vs[i]; p = {}; vl >> p.x >> p.y >> p.z >> p.nx >> p.ny >> p.nz >> p.tu >> p.tv; }
			for (int i = 0; i < nt; ++i) std::getline(f, line);
			m.v.push_back(std::move(vs));
		}
		if (m.v.empty()) { meshes.erase(name); return nullptr; }
		m.g.resize(m.v.size());
		for (size_t i = 0; i < m.v.size(); ++i) { m.g[i] = {}; m.g[i].Vtx = m.v[i].data(); m.g[i].nVtx = static_cast<DWORD>(m.v[i].size()); }
	}
	return &m;
}
MESHGROUP* oapiMeshGroup(MESHHANDLE h, DWORD i) { Mesh* m = static_cast<Mesh*>(h); return m && i < m->g.size() ? &m->g[i] : nullptr; }
int oapiEditMeshGroup(DEVMESHHANDLE, DWORD i, GROUPEDITSPEC* e) { if (captured.size() <= i) captured.resize(i + 1); captured[i].assign(e->Vtx, e->Vtx + e->nVtx); return 0; }

// ---- the commander's place on «Тантра»'s bridge, as built (InteriorLayout.h, TantraInterior.cpp, TantraYoke.cpp) ----
namespace ship
{
	const double floorY = 1.0, kHips = 0.72, seatZ0 = 78.55, travel = 1.0;
	const double ball[4] = { -0.3000, 1.8810, 78.7600, 0.0230 };
	const VECTOR3 yB = _V(0, 1.40, 80.66), yH0 = _V(0, 1.98, 79.88), yU0 = _V(0, 0.5967, -0.8025), yY0 = _V(0, 0.7960, 0.6053), yZ0 = _V(0, 0.6053, -0.7960);   // InteriorLayout.h kYoke*
	double yHub[3] = { 0.330, 1.080, 0.480 };
	const double yEye[2] = { 0.687, -0.100 }, yStow = 0.120;
	const double hornR = 0.016;   // the grips' radius (the mesh's bridge_yoke_grip)

	struct State { double hc{ -0.10 }, adj{ 0 }, roll{ 0 }, pitch{ 0 }, e{ 1 }; };
	VECTOR3 Hips(const State& s) { return _V(0, floorY + kHips + s.hc, seatZ0 + travel + s.adj); }

	struct M { double m[9]; };
	VECTOR3 Mul(const M& a, const VECTOR3& v) { return _V(a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z, a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z); }
	M Mul(const M& a, const M& b) { M r{}; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j]; return r; }
	M RotTo(const VECTOR3& a, const VECTOR3& b)
	{
		const VECTOR3 v = crossp(a, b); const double c = dotp(a, b);
		if (c < -0.9999) return { { 1, 0, 0, 0, -1, 0, 0, 0, -1 } };
		const double k = 1.0 / (1.0 + c);
		return { { v.x * v.x * k + c, v.x * v.y * k - v.z, v.x * v.z * k + v.y, v.y * v.x * k + v.z, v.y * v.y * k + c, v.y * v.z * k - v.x, v.z * v.x * k - v.y, v.z * v.y * k + v.x, v.z * v.z * k + c } };
	}
	M AxisAngle(const VECTOR3& k, double t)
	{
		const double c = std::cos(t), s = std::sin(t), C = 1 - c;
		return { { c + k.x * k.x * C, k.x * k.y * C - k.z * s, k.x * k.z * C + k.y * s, k.y * k.x * C + k.z * s, c + k.y * k.y * C, k.y * k.z * C - k.x * s, k.z * k.x * C - k.y * s, k.z * k.y * C + k.x * s, c + k.z * k.z * C } };
	}
	// TantraInterior::YokeStep: the hub and its frame
	struct Hub { VECTOR3 H, X, Y, Z, u; M Rc, Rh; double wheel; };
	Hub YokeHub(const State& s)
	{
		Hub h{};
		const double e = s.e * s.e * (3 - 2 * s.e), hipZ = Hips(s).z;
		const double zst = yB.z - yStow, z = zst - e * (zst - (hipZ + yHub[0]));
		h.H = _V(yB.x, floorY + yHub[2] + e * (yHub[1] + s.hc - yHub[2]), z - s.pitch * 0.04 * e);
		h.u = unit(h.H - yB);
		VECTOR3 n = _V(yB.x, floorY + 0.72 + s.hc + yEye[0], hipZ + yEye[1]) - h.H; n.x = 0; n = unit(n);
		h.Rc = RotTo(yU0, h.u); const M Rt = RotTo(yZ0, n);
		h.wheel = s.roll * 0.7 * e;
		M Rr = AxisAngle(n, h.wheel);
		if (h.wheel != 0.0 && Mul(Rr, Mul(Rt, yY0)).x * h.wheel < 0.0) Rr = AxisAngle(n, -h.wheel);
		h.Rh = Mul(Rr, Rt);
		h.X = Mul(h.Rh, _V(1, 0, 0)); h.Y = Mul(h.Rh, yY0); h.Z = Mul(h.Rh, yZ0);
		return h;
	}
	// the targets, interior frame (what the ship's SeatHands gives)
	struct Hand { bool on{}; int what{}; VECTOR3 pos{}, palm{}, fwd{}; int grip{}; double radius{}; };
	Hand Ball(const State& s)   // the left palm on the trackball, the fingers forward
	{
		const VECTOR3 B = _V(ball[0], ball[1] + s.hc, ball[2] + travel + s.adj);
		return { true, 1, B + _V(0, ball[3], 0), _V(0, -1, 0), _V(0, 0, 1), 1 };
	}
	Hand Armrest(const State& s)   // the right hand on the armrest's end, ahead of the ХОД / ВЫСОТА strips
	{
		const VECTOR3 hp = Hips(s);
		return { true, 2, _V(0.37, floorY + 0.765 + s.hc, hp.z + 0.20), _V(0, -1, 0), _V(0, 0, 1), 0 };
	}
	Hand Horn(const State& s, int side)   // a yoke's horn in hand: the palm on its rear, outer side, the thumb up
	{
		const Hub h = YokeHub(s);
		const double sx = side == 0 ? -1 : 1;
		const VECTOR3 gt = h.H + h.X * (sx * 0.243) + h.Y * -0.058, gb = h.H + h.X * (sx * 0.268) + h.Y * -0.168;
		const VECTOR3 a = unit(gb - gt), gm = gt + (gb - gt) * 0.35;
		// the straight fingers along her reach (from a seated person's shoulder), across the horn; the palm on its side
		VECTOR3 n = h.Z * -0.95 + h.X * (-sx * 0.3);   // the user's grip (his video): palm forward and in, thumb on top
		n = unit(n - a * dotp(n, a));
		const VECTOR3 f = side == 0 ? crossp(a, n) : crossp(n, a);
		return { true, 10 + side, gm - n * hornR, n, f, 2, hornR };
	}
}

// ---- the figure in the seat, driven as CrewMember drives it ----
struct Rig
{
	Skin skin; ClipSet clips; Motion motion;
	double height{ 0.93 }, sitHipY{ 0.5 };
	VECTOR3 eye{ _V(0, 0.69, 0.17) };
	VECTOR3 Origin(const ship::State& s) const { const VECTOR3 hips = ship::Hips(s); return hips - _V(0, sitHipY, 0) + _V(0, height, 0); }
	void Step(double dt, const ship::State& s, const ship::Hand& L, const ship::Hand& R)
	{
		MotionInput in;
		in.dt = dt; in.seat = 2; in.seatT = 1; in.grounded = true; in.g = 9.81; in.breathRate = 13; in.walkTop = 1.45;
		const VECTOR3 o = Origin(s);
		auto put = [&](HandTarget& h, const ship::Hand& t) { if (!t.on) return; h.on = true; h.what = t.what; h.pos = t.pos - o; h.palm = t.palm; h.fwd = t.fwd; h.grip = t.grip; h.radius = t.radius; };
		put(in.hand[0], L); put(in.hand[1], R);
		motion.Update(in, clips, skin);
	}
};

static std::string outDir;
static std::ofstream scenes;
static void Dump(const std::string& name)
{
	std::ofstream f(outDir + "\\" + name + ".f32", std::ios::binary);
	for (auto& g : captured) for (auto& v : g) { const float p[3] = { v.x, v.y, v.z }; f.write(reinterpret_cast<const char*>(p), sizeof p); }
}
static void Snap(Rig& r, const std::string& name, const ship::State& s, const ship::Hand& L, const ship::Hand& R)
{
	Dump(name);
	r.skin.SetHideHead(true); r.skin.Apply(r.skin.LastPose()); Dump(name + "_fp"); r.skin.SetHideHead(false); r.skin.Apply(r.skin.LastPose());
	const VECTOR3 o = r.Origin(s), e = r.skin.Point(r.skin.Bone("Head"), r.eye) + o;
	const ship::Hub h = ship::YokeHub(s);
	char b[1024];
	std::snprintf(b, sizeof b, "%s origin %.5f %.5f %.5f eye %.5f %.5f %.5f hc %.3f adj %.3f yokeE %.3f H %.5f %.5f %.5f u %.5f %.5f %.5f Rh %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f",
		name.c_str(), o.x, o.y, o.z, e.x, e.y, e.z, s.hc, s.adj, s.e, h.H.x, h.H.y, h.H.z, h.u.x, h.u.y, h.u.z,
		h.Rh.m[0], h.Rh.m[1], h.Rh.m[2], h.Rh.m[3], h.Rh.m[4], h.Rh.m[5], h.Rh.m[6], h.Rh.m[7], h.Rh.m[8]);
	scenes << b;
	for (const ship::Hand* t : { &L, &R })
	{
		std::snprintf(b, sizeof b, " T %d %.5f %.5f %.5f %.4f %.4f %.4f %.4f %.4f %.4f", t->on ? 1 : 0, t->pos.x, t->pos.y, t->pos.z, t->palm.x, t->palm.y, t->palm.z, t->fwd.x, t->fwd.y, t->fwd.z);
		scenes << b;
	}
	scenes << "\n";
	for (int sd = 0; sd < 2; ++sd)
	{
		const SeatArms::Diag& d = r.motion.Arms().Last(sd);
		std::printf("  %-14s %s: %s beta %.2f short %.3f m, palm off %.3f m, elbow %.0f deg, wrist %.0f deg, twist %.0f deg, grip %.2f\n", name.c_str(), sd ? "right" : "left ",
			d.active ? "IK " : "own", d.beta, d.shortM, d.offM, d.elbowDeg, d.wristDeg, d.twistDeg, r.motion.Arms().Grip(sd));
	}
}

int main(int argc, char** argv)
{
	outDir = argc > 1 ? argv[1] : ".";
	if (argc > 2) ship::yHub[0] = std::atof(argv[2]);   // the hub ahead of the hip (a test of the yoke's place)
	scenes.open(outDir + "\\scenes.txt");
	Rig r;
	if (!r.skin.Load("Tantra\\Astronavigator") || !r.clips.Load("Tantra\\anim")) { std::printf("cannot load the figure\n"); return 1; }
	r.clips.walk.cubic = r.clips.run.cubic = true;
	VESSEL v; r.skin.Attach(&v, nullptr, 0);
	const int hb = r.skin.Bone("Hips");
	r.sitHipY = r.clips.sit.data[hb * 7 + 5] + r.height;
	std::printf("seated hips %.3f m over the feet; the hub %.3f m ahead of the hip\n", r.sitHipY, ship::yHub[0]);
	const double dt = 1.0 / 60;
	ship::State s;
	const ship::Hand none{};
	auto run = [&](double sec, const ship::Hand& L, const ship::Hand& R, const std::vector<std::pair<double, std::string>>& snaps = {})
	{
		double t = 0; size_t k = 0;
		for (int i = 0, n = static_cast<int>(sec / dt + 0.5); i < n; ++i)
		{
			r.Step(dt, s, L, R); t += dt;
			while (k < snaps.size() && t + 1e-9 >= snaps[k].first) { Snap(r, snaps[k].second, s, L, R); ++k; }
		}
	};
	// her own seated pose
	run(0.5, none, none, { { 0.5, "natural" } });
	Pose idle; r.clips.idle.Sample(0, idle);
	for (int sd = 0; sd < 4; ++sd)   // her own hands seated, and standing (idle): where the palm faces and the fingers point
	{
		const Pose& p = sd < 2 ? r.skin.LastPose() : idle; const Skin::HandRest& hr = r.skin.Hand(sd & 1);
		if (sd == 2) std::printf("  (standing, idle:)\n");
		const int b = r.skin.Bone((sd & 1) ? "RightHand" : "LeftHand"); const float* q = &p.q[b * 4]; const float* R0 = r.skin.RestR(b);
		const double w = q[0], x = q[1], y = q[2], z = q[3];
		const double R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		auto M = [&](const VECTOR3& v) { VECTOR3 a = _V(R0[0] * v.x + R0[3] * v.y + R0[6] * v.z, R0[1] * v.x + R0[4] * v.y + R0[7] * v.z, R0[2] * v.x + R0[5] * v.y + R0[8] * v.z); return _V(R[0] * a.x + R[1] * a.y + R[2] * a.z, R[3] * a.x + R[4] * a.y + R[5] * a.z, R[6] * a.x + R[7] * a.y + R[8] * a.z); };
		const VECTOR3 n = M(hr.normal), f = M(hr.fingers);
		std::printf("  seated %s hand: palm normal (%.2f %.2f %.2f), fingers (%.2f %.2f %.2f), wrist (%.3f %.3f %.3f)\n", (sd & 1) ? "right" : "left ", n.x, n.y, n.z, f.x, f.y, f.z, p.t[b * 3], p.t[b * 3 + 1], p.t[b * 3 + 2]);
	}
	// sat down: the hands go to their places
	run(1.0, ship::Ball(s), ship::Armrest(s), { { 0.12, "rest_012" }, { 0.25, "rest_025" }, { 0.40, "rest_040" }, { 0.60, "rest_060" }, { 1.0, "rest" } });
	// the yoke out: both hands onto its horns
	run(1.2, ship::Horn(s, 0), ship::Horn(s, 1), { { 0.12, "yoke_012" }, { 0.25, "yoke_025" }, { 0.40, "yoke_040" }, { 0.60, "yoke_060" }, { 1.2, "yoke" } });
	// the wheel turned right, left; the column pulled
	for (int i = 0; i < 30; ++i) { s.roll = (i + 1) / 30.0; run(dt, ship::Horn(s, 0), ship::Horn(s, 1)); }
	run(0.2, ship::Horn(s, 0), ship::Horn(s, 1), { { 0.2, "roll_right" } });
	for (int i = 0; i < 60; ++i) { s.roll = 1 - (i + 1) / 30.0; run(dt, ship::Horn(s, 0), ship::Horn(s, 1)); }
	run(0.2, ship::Horn(s, 0), ship::Horn(s, 1), { { 0.2, "roll_left" } });
	for (int i = 0; i < 30; ++i) { s.roll = -1 + (i + 1) / 30.0; s.pitch = (i + 1) / 30.0; run(dt, ship::Horn(s, 0), ship::Horn(s, 1)); }
	run(0.2, ship::Horn(s, 0), ship::Horn(s, 1), { { 0.2, "pull" } });
	s.pitch = 0;
	// the yoke away: back to the rests
	run(1.0, ship::Ball(s), ship::Armrest(s), { { 0.25, "back_025" }, { 1.0, "back" } });

	// ---- the controls where they fit her (the recommendation): the rests 0.15 m over the seat's hips point, 0.30 out,
	//      0.22 ahead; the yoke's hub 0.30 m ahead of the hip ----
	{
		ship::State s1; Rig q; q.skin.Load("Tantra\\Astronavigator"); q.clips.Load("Tantra\\anim"); VESSEL v1; q.skin.Attach(&v1, nullptr, 0);
		q.sitHipY = r.sitHipY;
		const VECTOR3 hp = ship::Hips(s1);
		const ship::Hand L{ true, 1, hp + _V(-0.30, 0.184, 0.21), _V(0, -1, 0), _V(0, 0, 1), 1 }, R{ true, 2, hp + _V(0.30, 0.145, 0.21), _V(0, -1, 0), _V(0, 0, 1), 0 };
		ship::yHub[0] = 0.33; ship::yHub[1] = 1.08;
		auto runq = [&](double sec, const ship::Hand& a, const ship::Hand& b, const std::vector<std::pair<double, std::string>>& snaps)
		{
			double t = 0; size_t k = 0;
			for (int i = 0, n = static_cast<int>(sec / dt + 0.5); i < n; ++i)
			{
				q.Step(dt, s1, a, b); t += dt;
				while (k < snaps.size() && t + 1e-9 >= snaps[k].first) { Snap(q, snaps[k].second, s1, a, b); ++k; }
			}
		};
		runq(1.0, L, R, { { 1.0, "rec_rest" } });
		runq(1.2, ship::Horn(s1, 0), ship::Horn(s1, 1), { { 0.25, "rec_reach" }, { 1.2, "rec_yoke" } });
		for (int i = 0; i < 30; ++i) { s1.roll = (i + 1) / 30.0; q.Step(dt, s1, ship::Horn(s1, 0), ship::Horn(s1, 1)); }
		runq(0.2, ship::Horn(s1, 0), ship::Horn(s1, 1), { { 0.2, "rec_roll" } });
		ship::yHub[0] = 0.62; ship::yHub[1] = 0.92;
	}

	// ---- where the controls fit her: her shoulders seated, and the places that give a resting / holding arm ----
	{
		ship::State s0; Rig q; q.skin.Load("Tantra\\Astronavigator"); q.clips.Load("Tantra\\anim"); VESSEL v2; q.skin.Attach(&v2, nullptr, 0);
		q.sitHipY = r.sitHipY;
		for (int i = 0; i < 30; ++i) q.Step(dt, s0, none, none);
		const Pose& p = q.skin.LastPose(); const VECTOR3 o = q.Origin(s0), hip = ship::Hips(s0);
		for (int sd = 0; sd < 2; ++sd)
		{
			const int U = q.skin.Bone(sd ? "RightArm" : "LeftArm"), F = q.skin.Bone(sd ? "RightForeArm" : "LeftForeArm"), H = q.skin.Bone(sd ? "RightHand" : "LeftHand");
			const VECTOR3 S = _V(p.t[U * 3], p.t[U * 3 + 1], p.t[U * 3 + 2]), E = _V(p.t[F * 3], p.t[F * 3 + 1], p.t[F * 3 + 2]), W = _V(p.t[H * 3], p.t[H * 3 + 1], p.t[H * 3 + 2]);
			const VECTOR3 Si = S + o - hip;
			std::printf("  %s shoulder: %.3f right, %.3f up, %.3f ahead of the seat's hips point; upper arm %.3f, forearm %.3f m\n", sd ? "right" : "left ", Si.x, Si.y, Si.z, length(E - S), length(W - E));
		}
		// the yoke ahead of the hip by a: the grips' reach and the elbows
		for (double a : { 0.30, 0.34, 0.38, 0.42, 0.46, 0.50 })
		{
			ship::yHub[0] = a; Rig y; y.skin.Load("Tantra\\Astronavigator"); y.clips.Load("Tantra\\anim"); VESSEL v3; y.skin.Attach(&v3, nullptr, 0); y.sitHipY = r.sitHipY;
			for (int i = 0; i < 90; ++i) y.Step(dt, s0, ship::Horn(s0, 0), ship::Horn(s0, 1));
			const SeatArms::Diag& dl = y.motion.Arms().Last(0); const SeatArms::Diag& dr = y.motion.Arms().Last(1);
			std::printf("  hub %.2f m ahead of the hip: short %.3f / %.3f m, elbows %.0f / %.0f deg, wrists %.0f / %.0f deg, twists %.0f / %.0f deg\n",
				a, dl.shortM, dr.shortM, dl.elbowDeg, dr.elbowDeg, dl.wristDeg, dr.wristDeg, dl.twistDeg, dr.twistDeg);
		}
		ship::yHub[0] = 0.62;
		// a hand resting palm down, fingers forward: where on the armrest (right hand; the left mirrors it on the ball)
		std::printf("  right hand resting (palm point from the seat's hips point: out, up, ahead):\n");
		for (double x : { 0.22, 0.27, 0.32, 0.37 })
			for (double yy : { 0.045, 0.10, 0.15 })
				for (double z : { 0.15, 0.22, 0.29 })
				{
					Rig y; y.skin.Load("Tantra\\Astronavigator"); y.clips.Load("Tantra\\anim"); VESSEL v3; y.skin.Attach(&v3, nullptr, 0); y.sitHipY = r.sitHipY;
					const VECTOR3 hp = ship::Hips(s0);
					const ship::Hand R{ true, 2, hp + _V(x, yy, z), _V(0, -1, 0), _V(0, 0, 1), 0 };
					for (int i = 0; i < 90; ++i) y.Step(dt, s0, none, R);
					const SeatArms::Diag& d = y.motion.Arms().Last(1);
					std::printf("    %.2f %.3f %.2f: short %.3f m, elbow %.0f deg, wrist %.0f deg, twist %.0f deg, elbow at %.3f up %.3f ahead\n",
						x, yy, z, d.shortM, d.elbowDeg, d.wristDeg, d.twistDeg, d.elbow.y + y.Origin(s0).y - hp.y, d.elbow.z + y.Origin(s0).z - hp.z);
				}
	}
	std::printf("done\n");
	return 0;
}
