// OrbiterCrew - skinned crew member meshes (see Skin.h).
#include "Skin.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace ocrew
{
	namespace
	{
		void Nlerp(const float* a, const float* b, float t, float* out)
		{
			const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
			const float s = d < 0 ? -1.0f : 1.0f;
			float n = 0;
			for (int i = 0; i < 4; ++i) { out[i] = a[i] * (1 - t) + s * b[i] * t; n += out[i] * out[i]; }
			n = 1.0f / std::sqrt((std::max)(n, 1e-12f));
			for (int i = 0; i < 4; ++i) out[i] *= n;
		}

		void QuatToMat(const float* q, float* R)
		{
			const float w = q[0], x = q[1], y = q[2], z = q[3];
			R[0] = 1 - 2 * (y * y + z * z); R[1] = 2 * (x * y - z * w);     R[2] = 2 * (x * z + y * w);
			R[3] = 2 * (x * y + z * w);     R[4] = 1 - 2 * (x * x + z * z); R[5] = 2 * (y * z - x * w);
			R[6] = 2 * (x * z - y * w);     R[7] = 2 * (y * z + x * w);     R[8] = 1 - 2 * (x * x + y * y);
		}

		// q = r * q, r = rotation by angle about unit axis
		void PreRotate(float* q, const float* r)
		{
			const float w = q[0], x = q[1], y = q[2], z = q[3];
			q[0] = r[0] * w - r[1] * x - r[2] * y - r[3] * z;
			q[1] = r[0] * x + r[1] * w + r[2] * z - r[3] * y;
			q[2] = r[0] * y - r[1] * z + r[2] * w + r[3] * x;
			q[3] = r[0] * z + r[1] * y - r[2] * x + r[3] * w;
		}

		std::string ConfigPath(const std::string& rel) { return "Config\\" + rel; }
	}

	bool Clip::Load(const std::string& path)
	{
		std::ifstream f(path);
		if (!f) return false;
		std::string tok;
		while (f >> tok)
		{
			if (tok == "NAME") f >> name;
			else if (tok == "FPS") f >> fps;
			else if (tok == "STRIDE") f >> stride;
			else if (tok == "SPEED") f >> speed;
			else if (tok == "LOOP") { int l; f >> l; loop = l != 0; }
			else if (tok == "BONES") f >> nb;
			else if (tok == "FRAME")
			{
				int k; f >> k;
				for (int i = 0; i < nb * 7; ++i) { float v; f >> v; data.push_back(v); }
			}
		}
		return Frames() > 0;
	}

	void Clip::Sample(double ph, Pose& out) const
	{
		const int n = Frames();
		out.q.resize(nb * 4); out.t.resize(nb * 3);
		// a loop wraps over its frames; a one-shot clip (sit down, stand up) runs from the first frame to the last
		const double x = n < 2 ? 0 : loop ? (ph - std::floor(ph)) * n : std::clamp(ph, 0.0, 1.0) * (n - 1);
		const int f1 = (std::min)(static_cast<int>(x), n - 1) % n, f2 = loop ? (f1 + 1) % n : (std::min)(f1 + 1, n - 1);
		const float t = static_cast<float>(x - std::floor(x));
		const float* a = &data[f1 * nb * 7]; const float* b = &data[f2 * nb * 7];
		if (!cubic || !loop || n < 4)
		{
			for (int i = 0; i < nb; ++i)
			{
				Nlerp(a + i * 7, b + i * 7, t, &out.q[i * 4]);
				for (int k = 0; k < 3; ++k) out.t[i * 3 + k] = a[i * 7 + 4 + k] * (1 - t) + b[i * 7 + 4 + k] * t;
			}
			return;
		}
		// Catmull-Rom through the neighbouring frames; quaternions sign-aligned to the segment start, then normalised
		const float* p0 = &data[((f1 - 1 + n) % n) * nb * 7]; const float* p3 = &data[((f1 + 2) % n) * nb * 7];
		const float t2 = t * t, t3 = t2 * t;
		const float w0 = -0.5f * t3 + t2 - 0.5f * t, w1 = 1.5f * t3 - 2.5f * t2 + 1, w2 = -1.5f * t3 + 2 * t2 + 0.5f * t, w3 = 0.5f * t3 - 0.5f * t2;
		for (int i = 0; i < nb; ++i)
		{
			const float* q[4] = { p0 + i * 7, a + i * 7, b + i * 7, p3 + i * 7 };
			float r[4] = {}, n2 = 0;
			const float w[4] = { w0, w1, w2, w3 };
			for (int j = 0; j < 4; ++j)
			{
				const float d = q[j][0] * q[1][0] + q[j][1] * q[1][1] + q[j][2] * q[1][2] + q[j][3] * q[1][3];
				const float s = d < 0 ? -w[j] : w[j];
				for (int c = 0; c < 4; ++c) r[c] += s * q[j][c];
			}
			for (int c = 0; c < 4; ++c) n2 += r[c] * r[c];
			n2 = 1.0f / std::sqrt((std::max)(n2, 1e-12f));
			for (int c = 0; c < 4; ++c) out.q[i * 4 + c] = r[c] * n2;
			for (int k = 0; k < 3; ++k) out.t[i * 3 + k] = w0 * q[0][4 + k] + w1 * q[1][4 + k] + w2 * q[2][4 + k] + w3 * q[3][4 + k];
		}
	}

	void BlendPose(const Pose& a, const Pose& b, float t, Pose& out)
	{
		const size_t nb = a.t.size() / 3;
		out.q.resize(nb * 4); out.t.resize(nb * 3);
		for (size_t i = 0; i < nb; ++i)
		{
			Nlerp(&a.q[i * 4], &b.q[i * 4], t, &out.q[i * 4]);
			for (int k = 0; k < 3; ++k) out.t[i * 3 + k] = a.t[i * 3 + k] * (1 - t) + b.t[i * 3 + k] * t;
		}
	}

	bool Skin::Load(const std::string& skinFile)
	{
		std::ifstream f(ConfigPath(skinFile + ".skin"));
		if (!f) { oapiWriteLogV("OrbiterCrew: cannot open Config\\%s.skin", skinFile.c_str()); return false; }
		std::string tok;
		while (f >> tok)
		{
			if (tok == "MESH") f >> meshName;
			else if (tok == "LIDREST") f >> lidRest;
			else if (tok == "BONES")
			{
				int n; f >> n; bones.resize(n);
				for (auto& b : bones)
				{
					f >> b.name >> b.parent;
					for (float& v : b.R0) f >> v;
					for (float& v : b.T0) f >> v;
				}
			}
			else if (tok == "LABEL")
			{
				int gi; std::string name; f >> gi >> name;
				if (static_cast<int>(groups.size()) <= gi) groups.resize(gi + 1);
				groups[gi].label = name;
			}
			else if (tok == "MORPH")
			{
				Morph m; int n; f >> m.name >> m.group >> n; m.idx.resize(n); m.d.resize(n * 6);
				for (int k = 0; k < n; ++k) { f >> m.idx[k]; for (int c = 0; c < 6; ++c) f >> m.d[k * 6 + c]; }
				morphs.push_back(std::move(m));
			}
			else if (tok == "GROUP")
			{
				int gi, nv; f >> gi >> nv;
				if (static_cast<int>(groups.size()) <= gi) groups.resize(gi + 1);
				auto& g = groups[gi]; g.b.resize(nv * 4); g.w.resize(nv * 4);
				for (int v = 0; v < nv; ++v)
				{
					for (int k = 0; k < 4; ++k) f >> g.b[v * 4 + k];
					for (int k = 0; k < 4; ++k) f >> g.w[v * 4 + k];
				}
			}
		}
		if (bones.empty()) { oapiWriteLogV("OrbiterCrew: no skeleton in %s.skin", skinFile.c_str()); return false; }

		for (int i = 0; i < static_cast<int>(bones.size()); ++i)
			for (int p = i; p >= 0; p = bones[p].parent) bones[p].subtree.push_back(i);
		headBone = Bone("Head");
		SetupFingers();
		const int neck = Bone("Neck1");
		for (auto& g : groups)
		{
			const size_t nv = g.w.size() / 4; g.head.assign(nv, 0);
			for (size_t v = 0; v < nv; ++v)
			{
				float hw = 0;
				for (int k = 0; k < 4; ++k) if (g.b[v * 4 + k] == headBone || g.b[v * 4 + k] == neck) hw += g.w[v * 4 + k];
				g.head[v] = hw > 0.5f;
			}
		}
		return true;
	}

	void Skin::SetMorphSide(const char* name, float left, float right)
	{
		for (auto& m : morphs)
			if (m.name == name && (std::abs(m.wl - left) > 0.01f || std::abs(m.wr - right) > 0.01f)) { m.wl = left; m.wr = right; ApplyMorphs(m.group); }
	}

	void Skin::ApplyMorphs(int gi)
	{
		if (gi < 0 || gi >= static_cast<int>(groups.size())) return;
		auto& g = groups[gi];
		if (g.bind.empty()) return;
		g.base = g.bind;
		for (const auto& m : morphs)
		{
			if (m.group != gi || (m.wl <= 0 && m.wr <= 0)) continue;
			for (size_t k = 0; k < m.idx.size(); ++k)
			{
				const int v = m.idx[k]; if (v < 0 || v >= static_cast<int>(g.base.size())) continue;
				const float w = k < m.left.size() && m.left[k] ? m.wl : m.wr;
				if (w <= 0) continue;
				const float* d = &m.d[k * 6]; NTVERTEX& o = g.base[v];
				o.x += w * d[0]; o.y += w * d[1]; o.z += w * d[2];
				o.nx += w * d[3]; o.ny += w * d[4]; o.nz += w * d[5];
			}
		}
	}

	int Skin::Bone(const char* name) const
	{
		for (int i = 0; i < static_cast<int>(bones.size()); ++i) if (bones[i].name == name) return i;
		return -1;
	}

	void Skin::Attach(VESSEL* vessel, VISHANDLE vis, UINT meshIdx)
	{
		if (!Loaded()) return;
		dev = vessel->GetDevMesh(vis, meshIdx);
		MESHHANDLE tmpl = oapiLoadMeshGlobal(meshName.c_str());
		if (!dev || !tmpl) { dev = nullptr; oapiWriteLogV("OrbiterCrew: no device mesh for %s", meshName.c_str()); return; }
		for (DWORD gi = 0; gi < groups.size(); ++gi)
		{
			MESHGROUP* mg = oapiMeshGroup(tmpl, gi);
			auto& g = groups[gi];
			if (!mg || mg->nVtx * 4 != g.b.size()) { oapiWriteLogV("OrbiterCrew: %s group %d does not match the skin", meshName.c_str(), static_cast<int>(gi)); dev = nullptr; return; }
			g.bind.assign(mg->Vtx, mg->Vtx + mg->nVtx); g.base = g.bind; g.work = g.base;
			for (auto& m : morphs)   // which side of the body each of its vertices is on (one hand can close alone)
				if (m.group == static_cast<int>(gi))
				{
					m.left.assign(m.idx.size(), 0);
					for (size_t k = 0; k < m.idx.size(); ++k) m.left[k] = m.idx[k] >= 0 && m.idx[k] < static_cast<int>(g.bind.size()) && g.bind[m.idx[k]].x < 0;
				}
			ApplyMorphs(static_cast<int>(gi));
		}
		SetupHair();
		FindHands();
		MeasureFingers();
	}

	// the hands at rest, for the arms that reach the ship's controls (SeatArms): the palm's normal is where the fingers
	// close to (the "fist" shape moves the finger vertices toward the palm), its middle is on the palm's surface over the
	// hand bone's own vertices, the fingers run from the wrist to the knuckles
	void Skin::FindHands()
	{
		static const char* HB[2] = { "LeftHand", "RightHand" }, *KB[2] = { "LeftHandFinger1", "RightHandFinger1" }, *TB[2] = { "LThumb", "RThumb" };
		for (int s = 0; s < 2; ++s)
		{
			HandRest& h = hands[s]; h = HandRest{};
			const int hb = Bone(HB[s]), kb = Bone(KB[s]), tb = Bone(TB[s]);
			if (hb < 0 || kb < 0) continue;
			const VECTOR3 wrist = _V(bones[hb].T0[0], bones[hb].T0[1], bones[hb].T0[2]);
			VECTOR3 f = _V(bones[kb].T0[0], bones[kb].T0[1], bones[kb].T0[2]) - wrist;
			if (length(f) < 1e-4) continue;
			f = f / length(f);
			std::vector<char> finger(bones.size(), 0);
			for (int i : bones[kb].subtree) finger[i] = 1;
			auto onSide = [&](const NTVERTEX& v) { return s == 0 ? v.x < 0 : v.x >= 0; };
			VECTOR3 curlN = _V(0, 0, 0); bool curlOk = false;
			auto dominant = [&](const Group& g, size_t v) { int b = g.b[v * 4]; float w = g.w[v * 4]; for (int k = 1; k < 4; ++k) if (g.w[v * 4 + k] > w) { w = g.w[v * 4 + k]; b = g.b[v * 4 + k]; } return b; };
			// the palm's normal: the side the (relaxed, baked) fingers curl to - from the knuckles' part of the fingers to their
			// tips, across the proximal line (2026-10-05: the fist's mean move is mostly the fingers drawn together, sideways)
			{
				std::vector<std::pair<double, VECTOR3>> fv;
				for (const auto& g : groups)
					for (size_t v = 0; v < g.bind.size() && v * 4 + 3 < g.w.size(); ++v)
					{
						if (!onSide(g.bind[v])) continue;
						float wf = 0; for (int k = 0; k < 4; ++k) if (finger[g.b[v * 4 + k]]) wf += g.w[v * 4 + k];   // the phalanges too
						if (wf < 0.5f) continue;
						const VECTOR3 p = _V(g.bind[v].x, g.bind[v].y, g.bind[v].z);
						fv.push_back({ length(p - wrist), p });
					}
				if (fv.size() > 40)
				{
					std::sort(fv.begin(), fv.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
					auto mean = [&](double q0, double q1) { VECTOR3 c = _V(0, 0, 0); size_t a = size_t(q0 * fv.size()), b = size_t(q1 * fv.size()); for (size_t i = a; i < b; ++i) c = c + fv[i].second; return c / double(b - a); };
					const VECTOR3 Kp = mean(0.0, 0.15), mid = mean(0.40, 0.55), tip = mean(0.90, 1.0);
					const VECTOR3 prox = (mid - Kp) / length(mid - Kp);
					VECTOR3 cu = tip - mid; cu = cu - prox * dotp(cu, prox);
					if (length(cu) > 1e-5) { curlN = cu / length(cu); curlOk = true; }
				}
			}
			VECTOR3 m = _V(0, 0, 0);
			for (const auto& mo : morphs)
			{
				if (mo.name != "fist" || mo.group < 0 || mo.group >= static_cast<int>(groups.size())) continue;
				const Group& g = groups[mo.group];
				for (size_t k = 0; k < mo.idx.size(); ++k)
				{
					const int v = mo.idx[k];
					if (v < 0 || v >= static_cast<int>(g.bind.size()) || !onSide(g.bind[v]) || !finger[dominant(g, v)]) continue;
					m = m + _V(mo.d[k * 6], mo.d[k * 6 + 1], mo.d[k * 6 + 2]);
				}
			}
			VECTOR3 n = curlOk ? curlN - f * dotp(curlN, f) : m - f * dotp(m, f);
			if (length(n) < 1e-6 && tb >= 0)   // no fist shape: the thumb is on the palm's side of the hand's plane
			{
				VECTOR3 t = _V(bones[tb].T0[0], bones[tb].T0[1], bones[tb].T0[2]) - wrist;
				t = t - f * dotp(t, f);
				n = s == 1 ? crossp(f, t) : crossp(t, f);
			}
			if (length(n) < 1e-6) continue;
			n = n / length(n);
			// the middle of the palm: over the hand bone's own vertices (not the fingers), on the palm's side
			std::vector<VECTOR3> P;
			for (const auto& g : groups)
				for (size_t v = 0; v < g.bind.size() && v * 4 + 3 < g.w.size(); ++v)
				{
					if (!onSide(g.bind[v])) continue;
					float w = 0;
					for (int k = 0; k < 4; ++k) if (g.b[v * 4 + k] == hb) w += g.w[v * 4 + k];
					if (w >= 0.6f) P.push_back(_V(g.bind[v].x, g.bind[v].y, g.bind[v].z));
				}
			if (P.size() < 8) continue;
			VECTOR3 c = _V(0, 0, 0);
			for (const VECTOR3& p : P) c = c + p;
			c = c / static_cast<double>(P.size());
			std::vector<double> o; o.reserve(P.size());
			for (const VECTOR3& p : P) o.push_back(dotp(p - c, n));
			std::sort(o.begin(), o.end());
			h.palm = c + n * o[static_cast<size_t>(0.85 * (o.size() - 1))];
			h.normal = n; h.fingers = f; h.ok = true;
			oapiWriteLogV("OrbiterCrew: %s hand at rest: palm (%.3f %.3f %.3f) normal (%.2f %.2f %.2f) fingers (%.2f %.2f %.2f), %d vertices",
				s == 0 ? "left" : "right", h.palm.x, h.palm.y, h.palm.z, n.x, n.y, n.z, f.x, f.y, f.z, static_cast<int>(P.size()));
		}
	}

	void Skin::Apply(const Pose& pose)
	{
		last = pose;
		const size_t nb = bones.size(), np = (std::min)(nb, pose.q.size() / 4);
		S.assign(nb * 12, 0.0f);   // per bone: 3x3 rotation + translation of (pose * rest^-1)
		if (curl.size() != nb) curl.assign(nb, 0.0f);
		for (size_t i = 0; i < np; ++i)
		{
			float R[9]; QuatToMat(&pose.q[i * 4], R);
			const float* R0 = bones[i].R0; const float* T0 = bones[i].T0; float* s = &S[i * 12];
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					s[r * 3 + c] = R[r * 3 + 0] * R0[c * 3 + 0] + R[r * 3 + 1] * R0[c * 3 + 1] + R[r * 3 + 2] * R0[c * 3 + 2];   // R * R0^T
			for (int r = 0; r < 3; ++r)
				s[9 + r] = pose.t[i * 3 + r] - (s[r * 3] * T0[0] + s[r * 3 + 1] * T0[1] + s[r * 3 + 2] * T0[2]);
			if (i + 1 == np)   // the clip bones are done: now the ones that hang on them (in file order, parents first)
				for (size_t k = np; k < nb; ++k)
				{
					const int p = bones[k].parent; float* sk = &S[k * 12];
					if (p < 0 || p >= static_cast<int>(k)) { sk[0] = sk[4] = sk[8] = 1; continue; }
					const float* sp = &S[p * 12]; const float* R0 = bones[k].R0; const float* T0k = bones[k].T0;
					const float h = 0.5f * curl[k], sh = std::sin(h);
					const float r[4] = { std::cos(h), R0[0] * sh, R0[3] * sh, R0[6] * sh };
					float M[9]; QuatToMat(r, M);
					const float mt[3] = { T0k[0] - (M[0] * T0k[0] + M[1] * T0k[1] + M[2] * T0k[2]), T0k[1] - (M[3] * T0k[0] + M[4] * T0k[1] + M[5] * T0k[2]), T0k[2] - (M[6] * T0k[0] + M[7] * T0k[1] + M[8] * T0k[2]) };
					for (int a = 0; a < 3; ++a)
					{
						for (int c = 0; c < 3; ++c) sk[a * 3 + c] = sp[a * 3] * M[c] + sp[a * 3 + 1] * M[3 + c] + sp[a * 3 + 2] * M[6 + c];
						sk[9 + a] = sp[a * 3] * mt[0] + sp[a * 3 + 1] * mt[1] + sp[a * 3 + 2] * mt[2] + sp[9 + a];
					}
				}
		}
		if (!dev) return;
		if (hair.driven && headBone >= 0) StepHair(&S[headBone * 12]);
		for (DWORD gi = 0; gi < groups.size(); ++gi)
		{
			auto& g = groups[gi];
			if (g.base.empty()) continue;
			if (g.hidden)   // a part not worn (e.g. the jet pack): all its vertices collapse to one point, nothing is drawn
			{
				if (g.hiddenDone) continue;
				for (auto& o : g.work) { o.x = o.y = o.z = 0; }
				GROUPEDITSPEC hs{}; hs.flags = GRPEDIT_VTXCRD; hs.Vtx = g.work.data(); hs.nVtx = static_cast<DWORD>(g.work.size());
				oapiEditMeshGroup(dev, gi, &hs); g.hiddenDone = true; continue;
			}
			g.hiddenDone = false;
			for (size_t v = 0; v < g.base.size(); ++v)
			{
				NTVERTEX moved;
				if (g.moved)   // a movable part (e.g. the sun shade): turned in the rest pose first, then skinned
				{
					const NTVERTEX& r = g.base[v]; const float* M = g.move; moved = r;   // p' = R p + T
					moved.x = M[0] * r.x + M[1] * r.y + M[2] * r.z + M[9]; moved.y = M[3] * r.x + M[4] * r.y + M[5] * r.z + M[10]; moved.z = M[6] * r.x + M[7] * r.y + M[8] * r.z + M[11];
					moved.nx = M[0] * r.nx + M[1] * r.ny + M[2] * r.nz; moved.ny = M[3] * r.nx + M[4] * r.ny + M[5] * r.nz; moved.nz = M[6] * r.nx + M[7] * r.ny + M[8] * r.nz;
				}
				const NTVERTEX& b = g.moved ? moved : g.base[v]; NTVERTEX& o = g.work[v];
				float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
				for (int k = 0; k < 4; ++k)
				{
					const float w = g.w[v * 4 + k];
					if (w <= 0) continue;
					const float* s = &S[g.b[v * 4 + k] * 12];
					px += w * (s[0] * b.x + s[1] * b.y + s[2] * b.z + s[9]);
					py += w * (s[3] * b.x + s[4] * b.y + s[5] * b.z + s[10]);
					pz += w * (s[6] * b.x + s[7] * b.y + s[8] * b.z + s[11]);
					nx += w * (s[0] * b.nx + s[1] * b.ny + s[2] * b.nz);
					ny += w * (s[3] * b.nx + s[4] * b.ny + s[5] * b.nz);
					nz += w * (s[6] * b.nx + s[7] * b.ny + s[8] * b.nz);
				}
				if (hideHead && g.head[v] && headBone >= 0)
				{
					const float* s = &S[headBone * 12]; const float* T0 = bones[headBone].T0;
					px = s[0] * T0[0] + s[1] * T0[1] + s[2] * T0[2] + s[9];
					py = s[3] * T0[0] + s[4] * T0[1] + s[5] * T0[2] + s[10];
					pz = s[6] * T0[0] + s[7] * T0[1] + s[8] * T0[2] + s[11];
				}
				if (static_cast<int>(gi) == hair.group && hair.driven && headBone >= 0 && !hideHead)
				{
					// displacement in the head frame: the nodes' offsets, weighted, scaled by how free this vertex is;
					// nothing that would push into the head
					const float f = hair.free[v]; const float* wv = &hair.w[v * 4];
					double d[3] = { 0, 0, 0 };
					for (int k = 0; k < 4; ++k) for (int c = 0; c < 3; ++c) d[c] += f * wv[k] * hair.o[k][c];
					double r[3] = { b.x - hair.c[0], b.y - hair.c[1], b.z - hair.c[2] };
					const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]) + 1e-9, dr = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / rl;
					if (dr < 0) for (int c = 0; c < 3; ++c) d[c] -= 0.85 * dr * r[c] / rl;
					const float* s = &S[headBone * 12];
					px += static_cast<float>(s[0] * d[0] + s[1] * d[1] + s[2] * d[2]);
					py += static_cast<float>(s[3] * d[0] + s[4] * d[1] + s[5] * d[2]);
					pz += static_cast<float>(s[6] * d[0] + s[7] * d[1] + s[8] * d[2]);
				}
				const float nl = 1.0f / std::sqrt((std::max)(nx * nx + ny * ny + nz * nz, 1e-12f));
				o.x = px; o.y = py; o.z = pz; o.nx = nx * nl; o.ny = ny * nl; o.nz = nz * nl;
			}
			GROUPEDITSPEC ges{};
			ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = g.work.data(); ges.nVtx = static_cast<DWORD>(g.work.size());
			oapiEditMeshGroup(dev, gi, &ges);
		}
	}

	void Skin::SetupHair()
	{
		hair.group = -1;
		for (int gi = 0; gi < static_cast<int>(groups.size()); ++gi) if (groups[gi].label == "Hair" && !groups[gi].bind.empty()) hair.group = gi;
		if (hair.group < 0) return;
		const auto& B = groups[hair.group].bind; const size_t n = B.size();
		float ymin = 1e9f, ymax = -1e9f, cx = 0, cz = 0;
		for (const auto& p : B) { ymin = (std::min)(ymin, p.y); ymax = (std::max)(ymax, p.y); cx += p.x; cz += p.z; }
		cx /= n; cz /= n;
		hair.c[0] = cx; hair.c[1] = ymax - 0.11f; hair.c[2] = cz;          // about the middle of the skull
		hair.free.assign(n, 0); hair.w.assign(n * 4, 0);
		const float span = (std::max)(0.05f, ymax - ymin - 0.03f);
		static const float DX[4] = { 0, -1, 1, 0 }, DZ[4] = { -1, 0, 0, 1 };   // back, left, right, front
		for (size_t v = 0; v < n; ++v)
		{
			float t = std::clamp((ymax - 0.03f - B[v].y) / span, 0.0f, 1.0f);
			hair.free[v] = std::pow(t * t * (3 - 2 * t), 1.2f);                 // roots at the crown fixed, curls free
			const float x = B[v].x - cx, z = B[v].z - cz, l = std::sqrt(x * x + z * z) + 1e-6f;
			float sum = 0;
			for (int k = 0; k < 4; ++k) { float d = (std::max)(0.0f, (x * DX[k] + z * DZ[k]) / l); d *= d; hair.w[v * 4 + k] = d; sum += d; }
			for (int k = 0; k < 4; ++k) hair.w[v * 4 + k] = sum > 0 ? hair.w[v * 4 + k] / sum : 0.25f;
		}
		hair.primed = false;
	}

	void Skin::SetHairDrive(double dt, const VECTOR3& a, double g)
	{
		hair.driven = hair.group >= 0 && dt > 0;
		hair.dt = (std::min)(dt, 0.1);   // visual only: a frame of seconds (time acceleration) would be 10^5 substeps hair.g = g; hair.ax = a.x; hair.ay = a.y; hair.az = a.z;
	}

	void Skin::StepHair(const float* s)
	{
		const double dt = hair.dt;
		// the centre of the hair in the model frame, and its acceleration from the animation (bob, nods, turns)
		double p[3] = { s[0] * hair.c[0] + s[1] * hair.c[1] + s[2] * hair.c[2] + s[9],
		                s[3] * hair.c[0] + s[4] * hair.c[1] + s[5] * hair.c[2] + s[10],
		                s[6] * hair.c[0] + s[7] * hair.c[1] + s[8] * hair.c[2] + s[11] };
		double acc[3] = { 0, 0, 0 };
		if (hair.primed)
		{
			for (int c = 0; c < 3; ++c)
			{
				const double v = (p[c] - hair.p[c]) / dt;
				acc[c] = std::clamp((v - hair.v[c]) / dt, -60.0, 60.0);
				hair.v[c] = v;
			}
		}
		else { for (int c = 0; c < 3; ++c) hair.v[c] = 0; hair.primed = true; }
		for (int c = 0; c < 3; ++c) hair.p[c] = p[c];
		// what the hair feels: gravity minus acceleration, in the head frame, relative to standing still upright
		const double f[3] = { -(acc[0] + hair.ax), -hair.g - (acc[1] + hair.ay), -(acc[2] + hair.az) };
		const double fh[3] = { s[0] * f[0] + s[3] * f[1] + s[6] * f[2], s[1] * f[0] + s[4] * f[1] + s[7] * f[2], s[2] * f[0] + s[5] * f[1] + s[8] * f[2] };
		const double F[3] = { fh[0], fh[1] + hair.g, fh[2] };
		// four springs, slightly different so the curls do not move as one block; underdamped: a bounce, then rest
		static const double W[4] = { 14.0, 15.5, 16.0, 17.5 };
		const double zeta = 0.2, gain = 0.4, LIM = 0.03;
		const int n = (std::max)(1, static_cast<int>(std::ceil(dt / 0.004))); const double h = dt / n;
		for (int k = 0; k < 4; ++k)
			for (int i = 0; i < n; ++i)
				for (int c = 0; c < 3; ++c)
				{
					double& o = hair.o[k][c]; double& v = hair.ov[k][c];
					v += (gain * F[c] - W[k] * W[k] * o - 2 * zeta * W[k] * v) * h;
					o += v * h;
					if (o > LIM) { o = LIM; v = (std::min)(v, 0.0); } else if (o < -LIM) { o = -LIM; v = (std::max)(v, 0.0); }
				}
	}

	static bool HasPrefix(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

	bool Skin::SetPartTransform(const char* prefix, const float R[9], const float T[3])
	{
		bool found = false;
		for (auto& g : groups)
		{
			if (!HasPrefix(g.label, prefix)) continue;
			found = true; g.moved = true;
			for (int i = 0; i < 9; ++i) g.move[i] = R[i];
			for (int i = 0; i < 3; ++i) g.move[9 + i] = T[i];
		}
		return found;
	}

	bool Skin::SetPartHidden(const char* prefix, bool hide)
	{
		bool found = false;
		for (auto& g : groups) if (HasPrefix(g.label, prefix)) { found = true; g.hidden = hide; }
		return found;
	}

	bool Skin::MovePart(const char* label, const VECTOR3& pivot, const VECTOR3& axis, float angle)
	{
		const float h = 0.5f * angle, sh = std::sin(h);
		const float q[4] = { std::cos(h), static_cast<float>(axis.x) * sh, static_cast<float>(axis.y) * sh, static_cast<float>(axis.z) * sh };
		float R[9]; QuatToMat(q, R);
		const float px = static_cast<float>(pivot.x), py = static_cast<float>(pivot.y), pz = static_cast<float>(pivot.z);
		const float T[3] = { px - (R[0] * px + R[1] * py + R[2] * pz), py - (R[3] * px + R[4] * py + R[5] * pz), pz - (R[6] * px + R[7] * py + R[8] * pz) };
		return SetPartTransform(label, R, T);
	}

	VECTOR3 Skin::Point(int b, const VECTOR3& r) const
	{
		if (b < 0 || S.size() < (b + 1) * 12u) return r;
		const float* s = &S[b * 12];
		return _V(s[0] * r.x + s[1] * r.y + s[2] * r.z + s[9], s[3] * r.x + s[4] * r.y + s[5] * r.z + s[10], s[6] * r.x + s[7] * r.y + s[8] * r.z + s[11]);
	}

	void Skin::Turn(Pose& pose, int bone, const VECTOR3& axis, float angle) const
	{
		if (bone < 0 || angle == 0) return;
		const float* p = &pose.t[bone * 3];
		Rotate(pose, &bones[bone].subtree, _V(p[0], p[1], p[2]), axis, angle);
	}

	void Skin::TurnAll(Pose& pose, const VECTOR3& pivot, const VECTOR3& axis, float angle) const
	{
		Rotate(pose, nullptr, pivot, axis, angle);
	}

	void Skin::Rotate(Pose& pose, const std::vector<int>* set, VECTOR3 pivot, const VECTOR3& axis, float angle) const
	{
		if (angle == 0) return;
		const float h = 0.5f * angle, sh = std::sin(h);
		const float r[4] = { std::cos(h), static_cast<float>(axis.x) * sh, static_cast<float>(axis.y) * sh, static_cast<float>(axis.z) * sh };
		float R[9]; QuatToMat(r, R);
		const float px = static_cast<float>(pivot.x), py = static_cast<float>(pivot.y), pz = static_cast<float>(pivot.z);
		auto rot = [&](int i)
		{
			float* t = &pose.t[i * 3];
			const float x = t[0] - px, y = t[1] - py, z = t[2] - pz;
			t[0] = px + R[0] * x + R[1] * y + R[2] * z;
			t[1] = py + R[3] * x + R[4] * y + R[5] * z;
			t[2] = pz + R[6] * x + R[7] * y + R[8] * z;
			PreRotate(&pose.q[i * 4], r);
		};
		const int np = static_cast<int>((std::min)(bones.size(), pose.q.size() / 4));   // the fingers' phalanges are not in poses
		if (set) { for (int i : *set) if (i < np) rot(i); }
		else for (int i = 0; i < np; ++i) rot(i);
	}

	void Skin::Reattach(Pose& pose, const Pose& ref, int parent, int bone, float weight) const
	{
		if (parent < 0 || bone < 0 || weight <= 0) return;
		// D = q_parent(now) * conj(q_parent(ref)): carries the reference relation onto the current parent
		const float* a = &pose.q[parent * 4]; const float* r = &ref.q[parent * 4];
		const float rc[4] = { r[0], -r[1], -r[2], -r[3] };
		float D[4] = { a[0], a[1], a[2], a[3] };
		{ const float w = D[0], x = D[1], y = D[2], z = D[3];   // D = a * rc
		  D[0] = w * rc[0] - x * rc[1] - y * rc[2] - z * rc[3]; D[1] = w * rc[1] + x * rc[0] + y * rc[3] - z * rc[2];
		  D[2] = w * rc[2] - x * rc[3] + y * rc[0] + z * rc[1]; D[3] = w * rc[3] + x * rc[2] - y * rc[1] + z * rc[0]; }
		float M[9]; QuatToMat(D, M);
		const float* pa = &pose.t[parent * 3]; const float* pr = &ref.t[parent * 3];
		const int np = static_cast<int>((std::min)(pose.q.size(), ref.q.size()) / 4);
		for (int i : bones[bone].subtree)
		{
			if (i >= np) continue;
			float q[4] = { ref.q[i * 4], ref.q[i * 4 + 1], ref.q[i * 4 + 2], ref.q[i * 4 + 3] };
			PreRotate(q, D);
			const float dx = ref.t[i * 3] - pr[0], dy = ref.t[i * 3 + 1] - pr[1], dz = ref.t[i * 3 + 2] - pr[2];
			const float t[3] = { pa[0] + M[0] * dx + M[1] * dy + M[2] * dz, pa[1] + M[3] * dx + M[4] * dy + M[5] * dz, pa[2] + M[6] * dx + M[7] * dy + M[8] * dz };
			if (weight >= 1) { for (int c = 0; c < 4; ++c) pose.q[i * 4 + c] = q[c]; for (int c = 0; c < 3; ++c) pose.t[i * 3 + c] = t[c]; }
			else
			{
				float out[4]; Nlerp(&pose.q[i * 4], q, weight, out);
				for (int c = 0; c < 4; ++c) pose.q[i * 4 + c] = out[c];
				for (int c = 0; c < 3; ++c) pose.t[i * 3 + c] += (t[c] - pose.t[i * 3 + c]) * weight;
			}
		}
	}

	void Skin::TurnLocal(Pose& pose, int bone, const VECTOR3& a, float angle) const
	{
		if (bone < 0 || angle == 0) return;
		float R[9]; QuatToMat(&pose.q[bone * 4], R);
		VECTOR3 m = _V(R[0] * a.x + R[1] * a.y + R[2] * a.z, R[3] * a.x + R[4] * a.y + R[5] * a.z, R[6] * a.x + R[7] * a.y + R[8] * a.z);
		const double l = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
		if (l < 1e-9) return;
		Turn(pose, bone, m / l, angle);
	}

	// ---- the fingers (skins with phalanx bones, finger_rig.py) ----
	namespace
	{
		struct Xf { double R[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }, T[3]{}; };
		Xf Compose(const Xf& a, const Xf& b)
		{
			Xf r;
			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j) r.R[i * 3 + j] = a.R[i * 3] * b.R[j] + a.R[i * 3 + 1] * b.R[3 + j] + a.R[i * 3 + 2] * b.R[6 + j];
				r.T[i] = a.R[i * 3] * b.T[0] + a.R[i * 3 + 1] * b.T[1] + a.R[i * 3 + 2] * b.T[2] + a.T[i];
			}
			return r;
		}
		VECTOR3 XP(const Xf& a, const VECTOR3& p) { return _V(a.R[0] * p.x + a.R[1] * p.y + a.R[2] * p.z + a.T[0], a.R[3] * p.x + a.R[4] * p.y + a.R[5] * p.z + a.T[1], a.R[6] * p.x + a.R[7] * p.y + a.R[8] * p.z + a.T[2]); }
		VECTOR3 XD(const Xf& a, const VECTOR3& v) { return _V(a.R[0] * v.x + a.R[1] * v.y + a.R[2] * v.z, a.R[3] * v.x + a.R[4] * v.y + a.R[5] * v.z, a.R[6] * v.x + a.R[7] * v.y + a.R[8] * v.z); }
		Xf TurnAbout(const VECTOR3& ax, const VECTOR3& p, double th)   // about the line (p, ax), ax unit
		{
			Xf r; const double c = std::cos(th), s = std::sin(th), k = 1 - c, x = ax.x, y = ax.y, z = ax.z;
			const double R[9] = { c + x * x * k, x * y * k - z * s, x * z * k + y * s, y * x * k + z * s, c + y * y * k, y * z * k - x * s, z * x * k - y * s, z * y * k + x * s, c + z * z * k };
			for (int i = 0; i < 9; ++i) r.R[i] = R[i];
			r.T[0] = p.x - (R[0] * p.x + R[1] * p.y + R[2] * p.z); r.T[1] = p.y - (R[3] * p.x + R[4] * p.y + R[5] * p.z); r.T[2] = p.z - (R[6] * p.x + R[7] * p.y + R[8] * p.z);
			return r;
		}
		VECTOR3 Col(const float* R0, int c) { return _V(R0[c], R0[3 + c], R0[6 + c]); }
		VECTOR3 V3(const float* t) { return _V(t[0], t[1], t[2]); }
		// how far a point is outside the shape (< 0: inside)
		double Outside(const Skin::Shape& sh, const VECTOR3& p)
		{
			const VECTOR3 v = p - sh.c;
			switch (sh.type)
			{
			case Skin::SHAPE_CYLINDER: return length(v - sh.axis * dotp(v, sh.axis)) - sh.r;
			case Skin::SHAPE_SPHERE: return length(v) - sh.r;
			case Skin::SHAPE_PLANE: return dotp(v, sh.axis);
			}
			return 1e9;
		}
		const double kD = PI / 180;
		// each joint's range about the bound (relaxed) hand: opened .. closed (the bind hand is curled ~22/38/22 deg)
		const double kLo[3] = { -22 * kD, -30 * kD, -18 * kD }, kHi[3] = { 70 * kD, 70 * kD, 55 * kD };
		const double kTLo[2] = { -15 * kD, -15 * kD }, kTHi[2] = { 55 * kD, 70 * kD };
	}

	void Skin::SetupFingers()
	{
		static const char* SD[2] = { "L", "R" };
		static const char* KB[2] = { "LeftHandFinger1", "RightHandFinger1" }, *TB[2] = { "LThumb", "RThumb" };
		bool all = true;
		for (int s = 0; s < 2; ++s)
		{
			for (auto& c : chains[s]) c = Chain{};
			Chain& t = chains[s][0];
			t.parent = Bone(TB[s]); t.n = 2;
			t.bone[0] = Bone((std::string(SD[s]) + "T_2").c_str()); t.bone[1] = Bone((std::string(SD[s]) + "T_3").c_str());
			all = all && t.parent >= 0 && t.bone[0] >= 0 && t.bone[1] >= 0;
			for (int k = 1; k < 5; ++k)
			{
				Chain& c = chains[s][k]; c.parent = Bone(KB[s]); c.n = 3;
				for (int j = 0; j < 3; ++j) { const std::string nm = std::string(SD[s]) + "F" + std::to_string(k + 1) + "_" + std::to_string(j + 1); c.bone[j] = Bone(nm.c_str()); all = all && c.bone[j] >= 0; }
				all = all && c.parent >= 0;
			}
		}
		hasFingers = all;
		curl.assign(bones.size(), 0.0f);
	}

	// each phalanx: its length (to the next joint; the last one's from its vertices) and its pad, how far the palm side's
	// surface is from the bone (the 85th percentile of the vertices' offset along the bone's z)
	void Skin::MeasureFingers()
	{
		if (!hasFingers) return;
		for (auto& side : chains)
			for (Chain& c : side)
				for (int j = 0; j < c.n; ++j)
				{
					const int b = c.bone[j]; const float* R0 = bones[b].R0; const VECTOR3 T0 = V3(bones[b].T0), y = Col(R0, 1), z = Col(R0, 2);
					std::vector<double> along, pad;
					for (const auto& g : groups)
						for (size_t v = 0; v < g.bind.size() && v * 4 + 3 < g.w.size(); ++v)
						{
							float w = 0; for (int k = 0; k < 4; ++k) if (g.b[v * 4 + k] == b) w += g.w[v * 4 + k];
							if (w < 0.5f) continue;
							const VECTOR3 d = _V(g.bind[v].x, g.bind[v].y, g.bind[v].z) - T0;
							along.push_back(dotp(d, y)); pad.push_back(dotp(d, z));
						}
					if (j + 1 < c.n) c.len[j] = static_cast<float>(length(V3(bones[c.bone[j + 1]].T0) - T0));
					else c.len[j] = along.empty() ? 0.02f : static_cast<float>(*std::max_element(along.begin(), along.end()));
					if (pad.size() > 4) { std::sort(pad.begin(), pad.end()); c.pad[j] = static_cast<float>(std::clamp(pad[pad.size() * 85 / 100], 0.003, 0.012)); }
					else c.pad[j] = 0.007f;
				}
	}

	void Skin::Posed(const Pose& pose, int b, double M[12]) const
	{
		float R[9]; QuatToMat(&pose.q[b * 4], R);
		const float* R0 = bones[b].R0; const float* T0 = bones[b].T0;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c) M[r * 3 + c] = R[r * 3] * R0[c * 3] + R[r * 3 + 1] * R0[c * 3 + 1] + R[r * 3 + 2] * R0[c * 3 + 2];
		for (int r = 0; r < 3; ++r) M[9 + r] = pose.t[b * 3 + r] - (M[r * 3] * T0[0] + M[r * 3 + 1] * T0[1] + M[r * 3 + 2] * T0[2]);
	}

	void Skin::ClearCurl(int side)
	{
		if (!hasFingers) return;
		if (curl.size() != bones.size()) curl.assign(bones.size(), 0.0f);
		for (const Chain& c : chains[side & 1]) for (int j = 0; j < c.n; ++j) curl[c.bone[j]] = 0;
	}

	void Skin::Fist(Pose& pose, int side, float amount)
	{
		if (!hasFingers || amount <= 0) return;
		static const float F[3] = { 70 * float(kD), 70 * float(kD), 50 * float(kD) }, T[2] = { 35 * float(kD), 40 * float(kD) };
		if (curl.size() != bones.size()) curl.assign(bones.size(), 0.0f);
		(void)pose;
		for (int k = 1; k < 5; ++k) for (int j = 0; j < 3; ++j) curl[chains[side & 1][k].bone[j]] += amount * F[j];
		for (int j = 0; j < 2; ++j) curl[chains[side & 1][0].bone[j]] += amount * T[j];
	}

	void Skin::Wrap(Pose& pose, int side, const Shape& sh, float weight)
	{
		if (!hasFingers) return;
		if (curl.size() != bones.size()) curl.assign(bones.size(), 0.0f);
		if (weight <= 1e-3f || sh.type == SHAPE_NONE) return;
		side &= 1;
		if (static_cast<int>(pose.q.size() / 4) <= (std::max)(chains[side][0].parent, chains[side][1].parent)) return;
		auto xfOf = [&](int b) { double M[12]; Posed(pose, b, M); Xf x; for (int i = 0; i < 9; ++i) x.R[i] = M[i]; for (int i = 0; i < 3; ++i) x.T[i] = M[9 + i]; return x; };
		// the lowest point of the chain's phalanges j.. under the angles th (the palm side of each, half way and at its end)
		auto reach = [&](const Chain& c, const Xf& base, const double* th, int from)
		{
			Xf x = base; double best = 1e9;
			for (int j = 0; j < c.n; ++j)
			{
				const int b = c.bone[j]; const float* R0 = bones[b].R0; const VECTOR3 T0 = V3(bones[b].T0);
				x = Compose(x, TurnAbout(Col(R0, 0), T0, th[j]));
				if (j < from) continue;
				const VECTOR3 J = XP(x, T0), y = XD(x, Col(R0, 1)), z = XD(x, Col(R0, 2));
				for (double f : { 0.5, 1.0 })
					best = (std::min)(best, Outside(sh, J + y * (c.len[j] * f) + z * (c.pad[j] * (f < 1 ? 1.0 : 0.7))));
			}
			return best;
		};
		// close joint j from 'lo' until its phalanx (or one after it) touches; none: 'miss'
		auto close = [&](const Chain& c, const Xf& base, double* th, int j, double lo, double hi, double miss)
		{
			const double step = 3 * kD, tol = 0.0015;
			th[j] = lo;
			if (reach(c, base, th, j) <= tol) return;   // touching opened already
			for (double a = lo + step; a <= hi + 1e-9; a += step)
			{
				th[j] = a;
				if (reach(c, base, th, j) <= tol)
				{
					double l = a - step, h = a;
					for (int it = 0; it < 5; ++it) { th[j] = 0.5 * (l + h); (reach(c, base, th, j) <= tol ? h : l) = th[j]; }
					th[j] = l; return;
				}
			}
			th[j] = miss;
		};
		const bool flat = sh.type == SHAPE_PLANE;
		// the four fingers, knuckle first; a finger that misses the shape closes as far as it goes (round a handle) or
		// stays as it was (over a flat rest)
		for (int k = 1; k < 5; ++k)
		{
			const Chain& c = chains[side][k]; const Xf base = xfOf(c.parent);
			double th[3] = { kLo[0], kLo[1], kLo[2] };
			for (int j = 0; j < 3; ++j) close(c, base, th, j, kLo[j], kHi[j], flat ? 0.0 : sh.type == SHAPE_SPHERE ? 0.6 * kHi[j] : kHi[j]);
			for (int j = 0; j < 3; ++j) curl[c.bone[j]] += weight * static_cast<float>(th[j]);
		}
		// the thumb: round a handle or a ball it first comes across from the palm's other side (a turn of the whole thumb
		// about the hand's length, toward the palm), then its two joints close
		const Chain& t = chains[side][0];
		const int hb = Bone(side == 0 ? "LeftHand" : "RightHand");
		if (!flat && hb >= 0 && hands[side].ok)
		{
			const Xf H = xfOf(hb);
			const VECTOR3 f = XD(H, hands[side].fingers), n = XD(H, hands[side].normal), J = V3(&pose.t[t.parent * 3]);
			const Xf T0x = xfOf(t.parent);
			const int last = t.bone[t.n - 1];
			const VECTOR3 tip = XP(T0x, V3(bones[last].T0) + Col(bones[last].R0, 1) * t.len[t.n - 1]);
			const double sg = dotp(crossp(f, tip - J), n) >= 0 ? 1.0 : -1.0;
			double th[2] = { 0, 0 }, phi = 0;
			for (double a = 0; a <= 60 * kD + 1e-9; a += 3 * kD)
			{
				const Xf b = Compose(TurnAbout(f, J, sg * a), T0x);
				phi = a;
				if (reach(t, b, th, 0) <= 0.004) break;
			}
			const float turn = static_cast<float>(sg * phi * weight);
			Turn(pose, t.parent, f, turn);
			const Xf base = Compose(TurnAbout(f, J, sg * phi), T0x);
			th[0] = kTLo[0]; th[1] = kTLo[1];
			for (int j = 0; j < 2; ++j) close(t, base, th, j, kTLo[j], kTHi[j], 0.5 * kTHi[j]);
			for (int j = 0; j < 2; ++j) curl[t.bone[j]] += weight * static_cast<float>(th[j]);
		}
		else if (flat)
		{
			const Xf base = xfOf(t.parent);
			double th[2] = { kTLo[0], kTLo[1] };
			for (int j = 0; j < 2; ++j) close(t, base, th, j, kTLo[j], kTHi[j], 0.0);
			for (int j = 0; j < 2; ++j) curl[t.bone[j]] += weight * static_cast<float>(th[j]);
		}
	}

	void Skin::Shift(Pose& pose, const VECTOR3& d) const
	{
		for (size_t i = 0; i < bones.size() && i * 3 + 2 < pose.t.size(); ++i)
		{
			pose.t[i * 3] += static_cast<float>(d.x); pose.t[i * 3 + 1] += static_cast<float>(d.y); pose.t[i * 3 + 2] += static_cast<float>(d.z);
		}
	}
}
