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
		const double x = (n > 1 && loop) ? (ph - std::floor(ph)) * n : 0;
		const int f1 = static_cast<int>(x) % n, f2 = loop ? (f1 + 1) % n : f1;
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

	void Skin::SetMorph(const char* name, float weight)
	{
		for (auto& m : morphs)
			if (m.name == name && std::abs(m.weight - weight) > 0.01f) { m.weight = weight; ApplyMorphs(m.group); }
	}

	void Skin::ApplyMorphs(int gi)
	{
		if (gi < 0 || gi >= static_cast<int>(groups.size())) return;
		auto& g = groups[gi];
		if (g.bind.empty()) return;
		g.base = g.bind;
		for (const auto& m : morphs)
		{
			if (m.group != gi || m.weight <= 0) continue;
			for (size_t k = 0; k < m.idx.size(); ++k)
			{
				const int v = m.idx[k]; if (v < 0 || v >= static_cast<int>(g.base.size())) continue;
				const float* d = &m.d[k * 6]; NTVERTEX& o = g.base[v];
				o.x += m.weight * d[0]; o.y += m.weight * d[1]; o.z += m.weight * d[2];
				o.nx += m.weight * d[3]; o.ny += m.weight * d[4]; o.nz += m.weight * d[5];
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
			ApplyMorphs(static_cast<int>(gi));
		}
	}

	void Skin::Apply(const Pose& pose)
	{
		const size_t nb = bones.size();
		S.assign(nb * 12, 0.0f);   // per bone: 3x3 rotation + translation of (pose * rest^-1)
		for (size_t i = 0; i < nb; ++i)
		{
			float R[9]; QuatToMat(&pose.q[i * 4], R);
			const float* R0 = bones[i].R0; const float* T0 = bones[i].T0; float* s = &S[i * 12];
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					s[r * 3 + c] = R[r * 3 + 0] * R0[c * 3 + 0] + R[r * 3 + 1] * R0[c * 3 + 1] + R[r * 3 + 2] * R0[c * 3 + 2];   // R * R0^T
			for (int r = 0; r < 3; ++r)
				s[9 + r] = pose.t[i * 3 + r] - (s[r * 3] * T0[0] + s[r * 3 + 1] * T0[1] + s[r * 3 + 2] * T0[2]);
		}
		if (!dev) return;
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
				const float nl = 1.0f / std::sqrt((std::max)(nx * nx + ny * ny + nz * nz, 1e-12f));
				o.x = px; o.y = py; o.z = pz; o.nx = nx * nl; o.ny = ny * nl; o.nz = nz * nl;
			}
			GROUPEDITSPEC ges{};
			ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = g.work.data(); ges.nVtx = static_cast<DWORD>(g.work.size());
			oapiEditMeshGroup(dev, gi, &ges);
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
		if (set) for (int i : *set) rot(i);
		else for (int i = 0; i < static_cast<int>(bones.size()); ++i) rot(i);
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
		for (int i : bones[bone].subtree)
		{
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

	void Skin::Shift(Pose& pose, const VECTOR3& d) const
	{
		for (size_t i = 0; i < bones.size(); ++i)
		{
			pose.t[i * 3] += static_cast<float>(d.x); pose.t[i * 3 + 1] += static_cast<float>(d.y); pose.t[i * 3 + 2] += static_cast<float>(d.z);
		}
	}
}
