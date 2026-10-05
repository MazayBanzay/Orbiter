// OrbiterCrew - the arms of a seated person on the ship's controls (see SeatArms.h).
#include "SeatArms.h"
#include "Motion.h"
#include <algorithm>
#include <cmath>

namespace ocrew
{
	namespace
	{
		struct M3 { double m[9]; };   // row-major

		M3 FromQuat(const double* q)   // w, x, y, z
		{
			const double w = q[0], x = q[1], y = q[2], z = q[3];
			return { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
			           2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
			           2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
		}
		M3 FromQuatF(const float* q) { const double d[4] = { q[0], q[1], q[2], q[3] }; return FromQuat(d); }

		void ToQuat(const M3& R, double* q)
		{
			const double* m = R.m;
			const double tr = m[0] + m[4] + m[8];
			if (tr > 0) { const double s = std::sqrt(tr + 1) * 2; q[0] = 0.25 * s; q[1] = (m[7] - m[5]) / s; q[2] = (m[2] - m[6]) / s; q[3] = (m[3] - m[1]) / s; }
			else if (m[0] > m[4] && m[0] > m[8]) { const double s = std::sqrt(1 + m[0] - m[4] - m[8]) * 2; q[0] = (m[7] - m[5]) / s; q[1] = 0.25 * s; q[2] = (m[1] + m[3]) / s; q[3] = (m[2] + m[6]) / s; }
			else if (m[4] > m[8]) { const double s = std::sqrt(1 + m[4] - m[0] - m[8]) * 2; q[0] = (m[2] - m[6]) / s; q[1] = (m[1] + m[3]) / s; q[2] = 0.25 * s; q[3] = (m[5] + m[7]) / s; }
			else { const double s = std::sqrt(1 + m[8] - m[0] - m[4]) * 2; q[0] = (m[3] - m[1]) / s; q[1] = (m[2] + m[6]) / s; q[2] = (m[5] + m[7]) / s; q[3] = 0.25 * s; }
			const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
			for (int i = 0; i < 4; ++i) q[i] /= n;
		}

		M3 Mul(const M3& a, const M3& b)
		{
			M3 r{};
			for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
			return r;
		}
		M3 Tr(const M3& a) { return { { a.m[0], a.m[3], a.m[6], a.m[1], a.m[4], a.m[7], a.m[2], a.m[5], a.m[8] } }; }
		VECTOR3 Mul(const M3& a, const VECTOR3& v) { return _V(a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z, a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z); }
		M3 Cols(const VECTOR3& a, const VECTOR3& b, const VECTOR3& c) { return { { a.x, b.x, c.x, a.y, b.y, c.y, a.z, b.z, c.z } }; }

		VECTOR3 Jt(const Pose& p, int b) { return _V(p.t[b * 3], p.t[b * 3 + 1], p.t[b * 3 + 2]); }
		VECTOR3 RestT(const Skin& s, int b) { const float* t = s.RestT(b); return _V(t[0], t[1], t[2]); }
		M3 RestR(const Skin& s, int b) { const float* r = s.RestR(b); return { { r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] } }; }
		// the rotation that takes the bone's rest pose to its pose now (as Skin::Apply: pose * rest^-1)
		M3 Posed(const Skin& s, const Pose& p, int b) { return Mul(FromQuatF(&p.q[b * 4]), Tr(RestR(s, b))); }

		VECTOR3 Unit(const VECTOR3& v) { const double l = length(v); return l > 1e-12 ? v / l : _V(0, 0, 0); }
		double Smooth(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }
		double MinJerk(double s) { s = std::clamp(s, 0.0, 1.0); return s * s * s * (10 - 15 * s + 6 * s * s); }
		double SignedAngle(const VECTOR3& a, const VECTOR3& b, const VECTOR3& axis) { return std::atan2(dotp(crossp(a, b), axis), dotp(a, b)); }

		void Slerp(const double* a, const double* b, double t, double* out)
		{
			double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
			double bb[4] = { b[0], b[1], b[2], b[3] };
			if (d < 0) { d = -d; for (double& x : bb) x = -x; }
			double k0 = 1 - t, k1 = t;
			if (d < 0.9995) { const double th = std::acos(std::clamp(d, -1.0, 1.0)), s = std::sin(th); k0 = std::sin((1 - t) * th) / s; k1 = std::sin(t * th) / s; }
			double n = 0;
			for (int i = 0; i < 4; ++i) { out[i] = k0 * a[i] + k1 * bb[i]; n += out[i] * out[i]; }
			n = std::sqrt(n);
			for (int i = 0; i < 4; ++i) out[i] /= n;
		}
		// the turn of q about 'axis' (swing-twist), in (-pi, pi]
		double TwistAngle(const double* q, const VECTOR3& axis)
		{
			const double s = q[0] < 0 ? -1 : 1;
			return 2 * std::atan2(s * (q[1] * axis.x + q[2] * axis.y + q[3] * axis.z), s * q[0]);
		}
		void AxisAngle(const double* q, VECTOR3& axis, double& ang)
		{
			const double s = q[0] < 0 ? -1 : 1;
			const VECTOR3 v = _V(s * q[1], s * q[2], s * q[3]);
			const double l = length(v);
			ang = 2 * std::atan2(l, s * q[0]);
			axis = l > 1e-12 ? v / l : _V(1, 0, 0);
		}
		// turn a bone (and what hangs on it) about its joint so that the direction a becomes b
		void TurnTo(const Skin& skin, Pose& pose, int bone, const VECTOR3& a, const VECTOR3& b)
		{
			const VECTOR3 ax = crossp(a, b);
			const double s = length(ax), c = dotp(a, b);
			if (s < 1e-12) return;
			skin.Turn(pose, bone, ax / s, static_cast<float>(std::atan2(s, c)));
		}

		// how far each grip closes the hand (the "fist" shape): flat on a rest, cupped over a ball, round a handle
		const double kGrip[3] = { 0.08, 0.40, 0.90 };
	}

	void SeatArms::Reset()
	{
		for (Arm& a : arm) { a.what = -2; a.moving = false; a.s = a.beta = a.fromBeta = 0; a.grip = a.handle = a.cup = 0; a.diag = Diag{}; a.foreOk = false; a.shape = a.fromShape = Skin::Shape{}; a.wTo = a.wFrom = 0; }
	}

	// the bones, and the elbow's hinge: its axis in the upper arm's own frame, from a pose with the elbows well bent (the
	// seated clip; else the walk's most bent frame; else across the body, as for a forearm bending forward)
	void SeatArms::Bind(const Skin& skin, const ClipSet& clips)
	{
		bound = true;
		static const char* N[2][4] = { { "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand" }, { "RightShoulder", "RightArm", "RightForeArm", "RightHand" } };
		for (int s = 0; s < 2; ++s)
		{
			Arm& a = arm[s];
			a.C = skin.Bone(N[s][0]); a.U = skin.Bone(N[s][1]); a.F = skin.Bone(N[s][2]); a.H = skin.Bone(N[s][3]);
			if (a.U < 0 || a.F < 0 || a.H < 0) continue;
			// the hand at rest; the palm's side made sure of in her standing pose: a hanging hand's palm faces the thigh
			const Skin::HandRest& hr = skin.Hand(s);
			a.palm = hr.palm; a.fingers = hr.fingers; a.normal = Unit(hr.normal - hr.fingers * dotp(hr.normal, hr.fingers));
			{
				const int kb = skin.Bone(s == 0 ? "LeftHandFinger1" : "RightHandFinger1");
				const VECTOR3 w0 = RestT(skin, a.H), k0 = kb >= 0 ? RestT(skin, kb) : w0 + a.fingers * 0.10;
				a.gripPt = a.palm + a.fingers * (dotp(k0 - w0, a.fingers) - 0.015 - dotp(a.palm - w0, a.fingers));
			}
			if (hr.ok && clips.idle.Frames() > 0 && clips.idle.nb <= static_cast<int>(skin.Bones()) && clips.idle.nb > a.H)
			{
				Pose p; clips.idle.Sample(0, p);
				const VECTOR3 n = Mul(Posed(skin, p, a.H), a.normal), medial = _V(s == 0 ? 1.0 : -1.0, 0, 0);
				(void)n; (void)medial;   // the palm's side is the fingers' own curl (Skin::FindHands), not the capture's standing hands
			}
			double best = 0;
			auto take = [&](const Clip& c, double ph)
			{
				if (c.Frames() < 1 || c.nb > static_cast<int>(skin.Bones()) || c.nb <= a.H) return;   // (a skin may have finger bones past the clip's)
				Pose p; c.Sample(ph, p);
				const VECTOR3 S = Jt(p, a.U), E = Jt(p, a.F), W = Jt(p, a.H);
				const VECTOR3 h = crossp(E - S, W - E);
				const double bend = std::atan2(length(h), dotp(E - S, W - E));   // 0 straight
				if (bend <= best) return;
				best = bend;
				a.hinge = Mul(Tr(FromQuatF(&p.q[a.U * 4])), Unit(h));
			};
			if (clips.seats) take(clips.sit, 0);
			if (best < 15 * RAD) for (int k = 0; k < 24; ++k) take(clips.walk, k / 24.0);
			if (best < 15 * RAD)   // across the body (forearm bending forward), taken in the idle pose
			{
				Pose p; clips.idle.Sample(0, p);
				if (static_cast<int>(p.q.size()) > a.U * 4 + 3) a.hinge = Mul(Tr(FromQuatF(&p.q[a.U * 4])), _V(-1, 0, 0));
			}
		}
	}

	void SeatArms::Update(double dt, const HandTarget in[2], double heading, const ClipSet& clips, const Skin& skin, Pose& pose)
	{
		if (!bound) Bind(skin, clips);
		Frame nat[2], tgt[2];
		bool on[2]{}, started[2]{};
		for (int s = 0; s < 2; ++s)
		{
			Arm& a = arm[s];
			const Skin::HandRest& hr = skin.Hand(s);
			if (a.U < 0 || a.F < 0 || a.H < 0 || !hr.ok) { a.grip = a.handle = a.cup = 0; a.wTo = a.wFrom = 0; a.diag = Diag{}; continue; }
			Skin::Shape shape;   // what the fingers will close round
			// her own hand now (the pose as the other layers left it)
			const M3 Mh = Posed(skin, pose, a.H);
			nat[s].p = Jt(pose, a.H) + Mul(Mh, a.palm - RestT(skin, a.H));
			ToQuat(Mh, nat[s].q); nat[s].grip = nat[s].handle = nat[s].cup = 0;
			on[s] = in[s].on;
			a.floor = in[s].on && in[s].floor; a.elbowMinY = in[s].elbowMinY;
			if (on[s])   // the target: the palm's middle on the point, its normal and the fingers as the ship asks
			{
				VECTOR3 f = Unit(in[s].fwd), n = Unit(in[s].palm - f * dotp(in[s].palm, f));
				VECTOR3 pos = in[s].pos;
				// the thing itself, as the ship gave it (the palm's normal goes into it)
				if (in[s].grip == 2 && in[s].radius > 0) { shape.type = Skin::SHAPE_CYLINDER; shape.axis = Unit(crossp(f, n)); shape.c = pos + n * in[s].radius; shape.r = in[s].radius; }
				else if (in[s].grip == 1 && in[s].radius > 0) { shape.type = Skin::SHAPE_SPHERE; shape.c = pos + n * in[s].radius; shape.r = in[s].radius; }
				else { shape.type = Skin::SHAPE_PLANE; shape.c = pos; shape.axis = -n; }
				// a handle: the hand round its axis so that the straight fingers go on from her forearm (the wrist neither
				// bent nor turned), within 25 deg of what the ship gave (the thumb stays on the ship's side of it), and slanted
				// across the palm toward the forearm (a power grip holds a handle obliquely, up to ~25 deg)
				if (in[s].grip == 2 && in[s].radius > 0 && a.foreOk && length(f) > 0.5 && length(n) > 0.5)
				{
					const VECTOR3 ax = Unit(crossp(f, n)), c = pos + n * in[s].radius;
					VECTOR3 d = a.fore - ax * dotp(a.fore, ax);
					if (length(d) > 1e-3)
					{
						const double th = std::clamp(SignedAngle(f, Unit(d), ax), -25 * RAD, 25 * RAD), cs = std::cos(th), sn = std::sin(th);
						f = Unit(f * cs + crossp(ax, f) * sn); n = Unit(n * cs + crossp(ax, n) * sn);
					}
					VECTOR3 dn = a.fore - n * dotp(a.fore, n);
					if (length(dn) > 1e-3)
					{
						const double sl = std::clamp(SignedAngle(f, Unit(dn), n), -25 * RAD, 25 * RAD), cl = std::cos(sl), sw = std::sin(sl);
						f = Unit(f * cl + crossp(n, f) * sw);
					}
					pos = c - n * in[s].radius;
				}
				const VECTOR3 fr = a.fingers, nr = a.normal;
				if (length(f) < 0.5 || length(n) < 0.5) on[s] = false;
				else
				{
					const M3 R = Mul(Cols(f, n, crossp(f, n)), Tr(Cols(fr, nr, crossp(fr, nr))));
					tgt[s].p = pos; ToQuat(R, tgt[s].q);
					tgt[s].grip = kGrip[std::clamp(in[s].grip, 0, 2)]; tgt[s].handle = in[s].grip == 2 ? 1 : 0; tgt[s].cup = in[s].grip == 1 ? 1 : 0;
					tgt[s].closed = 1;
				}
			}
			if (!on[s]) { tgt[s] = nat[s]; shape = Skin::Shape{}; }
			const int what = on[s] ? in[s].what : -1;
			if (a.what == -2) { a.eff = nat[s]; a.beta = 0; a.what = -1; }   // just seated: from her own pose
			if (what != a.what)   // a new thing (or back to her own pose): reach there
			{
				a.from = a.eff; a.fromBeta = a.beta; a.what = what;
				a.fromShape = a.shape;
				a.moving = true; a.s = 0; a.wait = 0;
				a.dur = std::clamp(0.30 + 0.75 * length(tgt[s].p - a.from.p), 0.35, 0.90);
				started[s] = true;
			}
			a.shape = shape;   // followed as it moves (the yoke turns)
		}
		if (started[0] && started[1]) arm[1].wait = 0.07;   // both hands at once: the right a beat later
		for (int s = 0; s < 2; ++s)
		{
			Arm& a = arm[s];
			const Skin::HandRest& hr = skin.Hand(s);
			if (a.U < 0 || a.F < 0 || a.H < 0 || !hr.ok) continue;
			const double bT = on[s] ? 1.0 : 0.0;
			Frame e;
			if (a.moving)
			{
				if (a.wait > 0) a.wait -= dt; else a.s = (std::min)(1.0, a.s + dt / a.dur);
				const double k = MinJerk(a.s), kr = MinJerk(1.2 * a.s);   // the hand is turned a little ahead of its travel
				e.p = a.from.p + (tgt[s].p - a.from.p) * k;
				// the arc: off the surface it leaves, onto the one it comes to from its free side, and a little up
				const VECTOR3 n0 = Mul(FromQuat(a.from.q), a.normal), n1 = Mul(FromQuat(tgt[s].q), a.normal);
				const VECTOR3 lift = Unit(n0 * -(1 - a.s) + n1 * -a.s + _V(0, 0.6, 0));
				const double h = std::clamp(0.30 * length(tgt[s].p - a.from.p), 0.02, 0.08);
				e.p = e.p + lift * (h * std::sin(PI * a.s));
				Slerp(a.from.q, tgt[s].q, kr, e.q);
				// the fingers open as it leaves, close round the new thing as it arrives
				e.grip = a.from.grip * (1 - Smooth(0.0, 0.3, a.s)) + tgt[s].grip * Smooth(0.7, 1.0, a.s);
				e.handle = a.from.handle * (1 - Smooth(0.0, 0.3, a.s)) + tgt[s].handle * Smooth(0.7, 1.0, a.s);
				e.cup = a.from.cup * (1 - Smooth(0.0, 0.3, a.s)) + tgt[s].cup * Smooth(0.7, 1.0, a.s);
				a.wFrom = static_cast<float>(a.from.closed * (1 - Smooth(0.0, 0.3, a.s)));
				a.wTo = static_cast<float>(tgt[s].closed * Smooth(0.7, 1.0, a.s));
				e.closed = a.wFrom + a.wTo;
				a.beta = a.fromBeta + (bT - a.fromBeta) * k;
				if (a.s >= 1) a.moving = false;
			}
			else { e = tgt[s]; a.beta = bT; a.wFrom = 0; a.wTo = static_cast<float>(tgt[s].closed); }
			a.eff = e;
			a.grip = static_cast<float>(e.grip); a.handle = static_cast<float>(e.handle); a.cup = static_cast<float>(e.cup);
			a.diag = Diag{}; a.diag.beta = a.beta;
			if (a.beta < 1e-4 && !on[s]) continue;   // her own arm: nothing to do
			Solve(a, s, e, a.beta, heading, skin, pose);
		}
	}

	void SeatArms::Solve(Arm& a, int side, const Frame& t, double beta, double heading, const Skin& skin, Pose& pose)
	{
		const M3 R = FromQuat(t.q);
		const VECTOR3 T0H = RestT(skin, a.H);
		const VECTOR3 ref = a.palm + (a.gripPt - a.palm) * t.handle;   // a handle lies at the fingers' base, not mid-palm
		const VECTOR3 Wt = t.p - Mul(R, ref - T0H);       // where the wrist must be for that point to be there
		VECTOR3 S = Jt(pose, a.U), E = Jt(pose, a.F), W = Jt(pose, a.H);
		const double L1 = length(E - S), L2 = length(W - E), Lmax = L1 + L2;
		// 1. the shoulder girdle: a far reach brings the shoulder toward it (up to 4 cm)
		if (a.C >= 0)
		{
			const VECTOR3 C = Jt(pose, a.C);
			const double need = (length(Wt - S) - 0.88 * Lmax) * beta, r = length(S - C);
			const VECTOR3 ax = crossp(S - C, Wt - C);
			if (need > 1e-4 && r > 1e-3 && length(ax) > 1e-9)
			{
				skin.Turn(pose, a.C, ax / length(ax), static_cast<float>(std::asin((std::min)((std::min)(need, 0.04) / r, 0.35))));
				S = Jt(pose, a.U); E = Jt(pose, a.F); W = Jt(pose, a.H);
			}
		}
		// 2. the wrist as far as the arm reaches
		const VECTOR3 dv = Wt - S;
		double d = length(dv);
		const VECTOR3 u = d > 1e-9 ? dv / d : Unit(W - S);
		a.diag.shortM = (std::max)(0.0, d - 0.999 * Lmax);
		d = std::clamp(d, std::abs(L1 - L2) + 1e-3, 0.999 * Lmax);
		const VECTOR3 Wr = S + u * d;
		const double ac = (L1 * L1 - L2 * L2 + d * d) / (2 * d), rc = std::sqrt((std::max)(0.0, L1 * L1 - ac * ac));
		const VECTOR3 Cc = S + u * ac;
		// 3. the elbow on its circle: toward her own elbow while her own pose drives the arm, toward down, out and a little
		//    back as the target takes over (elbows hang; they neither rise nor turn in)
		const VECTOR3 fw = _V(std::sin(heading), 0, std::cos(heading)), rt = _V(std::cos(heading), 0, -std::sin(heading));
		const VECTOR3 out = side == 0 ? -rt : rt;
		const VECTOR3 Pa = S + _V(0, -0.70, 0) + out * 0.30 - fw * 0.25;
		const VECTOR3 Pp = E + (Pa - E) * beta;
		VECTOR3 v = Pp - Cc;
		v = v - u * dotp(v, u);
		if (length(v) < 1e-6) v = out - u * dotp(out, u);
		v = Unit(v);
		// not down into the seat: an elbow below its floor (the armrest's top) goes round its circle to the nearest point
		// over it (or the circle's highest), as the target takes over
		if (a.floor && rc > 1e-4 && Cc.y + v.y * rc < a.elbowMinY)
		{
			const VECTOR3 w = Unit(crossp(u, v));
			const double A = v.y, Bc = w.y, k = (a.elbowMinY - Cc.y) / rc, m = std::sqrt(A * A + Bc * Bc);
			double ph = std::atan2(Bc, A);                                   // the highest point
			if (m > 1e-9 && std::abs(k) <= m)
			{
				const double d0 = std::acos(std::clamp(k / m, -1.0, 1.0)), p1 = ph - d0, p2 = ph + d0;
				ph = std::abs(std::remainder(p1, 2 * PI)) < std::abs(std::remainder(p2, 2 * PI)) ? p1 : p2;
			}
			ph *= beta;
			v = Unit(v * std::cos(ph) + w * std::sin(ph));
		}
		const VECTOR3 Et = Cc + v * rc;
		// 4. the upper arm to the elbow, rolled so that the elbow's hinge lies across the arm's plane (it bends in it)
		TurnTo(skin, pose, a.U, E - S, Et - S);
		{
			const VECTOR3 u1 = Unit(Et - S);
			VECTOR3 hNow = Mul(FromQuatF(&pose.q[a.U * 4]), a.hinge), hWant = Unit(crossp(v, u));
			hNow = hNow - u1 * dotp(hNow, u1); hWant = hWant - u1 * dotp(hWant, u1);
			if (length(hNow) > 1e-6 && length(hWant) > 1e-6)
			{
				const double roll = SignedAngle(Unit(hNow), Unit(hWant), u1) * Smooth(0.0, 0.3, beta);
				if (std::abs(roll) > 1e-7) skin.Turn(pose, a.U, u1, static_cast<float>(roll));
			}
		}
		// 5. the forearm to the wrist (a bend about the hinge)
		{
			const VECTOR3 E2 = Jt(pose, a.F);
			TurnTo(skin, pose, a.F, Jt(pose, a.H) - E2, Wr - E2);
		}
		// 6. the forearm's twist: most of the hand's turn about the forearm (pronation, supination)
		{
			const VECTOR3 E2 = Jt(pose, a.F), ax = Unit(Jt(pose, a.H) - E2);
			double q[4]; ToQuat(Mul(R, Tr(Posed(skin, pose, a.H))), q);
			const double tw = 0.90 * TwistAngle(q, ax);   // the turn is the forearm's (radius over ulna), the wrist only bends
			a.diag.twistDeg = tw * DEG;
			if (std::abs(tw) > 1e-7) skin.Turn(pose, a.F, ax, static_cast<float>(tw));
		}
		// 7. the wrist: the rest of the hand's turn, within its range
		{
			double q[4]; ToQuat(Mul(R, Tr(Posed(skin, pose, a.H))), q);
			VECTOR3 ax; double ang;
			AxisAngle(q, ax, ang);
			a.diag.wristDeg = ang * DEG;
			if (ang > 1e-7) skin.Turn(pose, a.H, ax, static_cast<float>((std::min)(ang, 45 * RAD)));   // a wrist bends ~45 deg at most
		}
		const VECTOR3 S3 = Jt(pose, a.U), E3 = Jt(pose, a.F), W3 = Jt(pose, a.H);
		a.diag.offM = length(W3 + Mul(Posed(skin, pose, a.H), ref - T0H) - t.p);
		a.diag.elbowDeg = std::atan2(length(crossp(S3 - E3, W3 - E3)), dotp(S3 - E3, W3 - E3)) * DEG;
		a.diag.elbow = E3; a.diag.active = true;
		a.fore = Unit(W3 - E3); a.foreOk = true;
	}
}
