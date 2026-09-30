// OrbiterCrew - skinned crew member meshes.
// A skin is a skeleton plus four bone weights per vertex of an Orbiter mesh; the mesh is deformed on the CPU
// and written into the device mesh every frame. Data files are produced by Tantra_Design/blender/export_skin.py:
//   Config\<skin>.skin      bones (rest model matrices, Orbiter frame) + weights per mesh vertex
//   Config\<clips>\*.clip   model-space bone poses per frame; locomotion clips are in-place loops
// Poses are model-space (quaternion w,x,y,z + joint position per bone), so pose edits act on whole subtrees.
#pragma once
#include <Orbitersdk.h>
#include <string>
#include <vector>

namespace ocrew
{
	struct Pose { std::vector<float> q, t; };

	struct Clip
	{
		std::string name;
		double fps{ 30 }, stride{}, speed{};
		bool loop{ true };
		int nb{};
		bool cubic{};   // Catmull-Rom between frames: no velocity kink at every frame of a 30 fps clip
		std::vector<float> data;   // frames * nb * 7
		int Frames() const { return nb ? static_cast<int>(data.size() / (nb * 7)) : 0; }
		bool Load(const std::string& path);
		void Sample(double phase, Pose& out) const;   // phase 0..1 over the loop
	};

	void BlendPose(const Pose& a, const Pose& b, float t, Pose& out);

	class Skin
	{
	public:
		bool Load(const std::string& skinFile);
		bool Loaded() const { return !bones.empty(); }
		const std::string& MeshName() const { return meshName; }
		int Bone(const char* name) const;
		size_t Bones() const { return bones.size(); }

		void Attach(VESSEL* vessel, VISHANDLE vis, UINT meshIdx);
		void Detach() { dev = nullptr; }
		bool Attached() const { return dev != nullptr; }

		// deform the device mesh into this pose
		void Apply(const Pose& pose);
		// first person: collapse the head (hair, face, lashes) so the camera does not see it from inside
		void SetHideHead(bool hide) { hideHead = hide; }
		// hair on springs (the group labelled "Hair"): call every step with the body's acceleration in the model frame
		// (not counting the animation, which the skin measures itself) and gravity; without calls the hair stays rigid
		void SetHairDrive(double dt, const VECTOR3& bodyAccel, double g);
		// blend a shape from the .skin file (MORPH lines, e.g. "fist") into the bind mesh, weight 0..1
		void SetMorph(const char* name, float weight);
		// a rest-pose point carried by a bone (e.g. the eye point on the head), after the last Apply
		VECTOR3 Point(int bone, const VECTOR3& restPoint) const;
		// turn a labelled mesh group (e.g. "SunShade") about a rest-pose pivot before skinning; false if no such group
		bool MovePart(const char* label, const VECTOR3& pivot, const VECTOR3& axis, float angle);
		// general rest-pose transform p' = R p + T for every group whose label starts with 'prefix'
		bool SetPartTransform(const char* prefix, const float R[9], const float T[3]);
		// hide (collapse) every group whose label starts with 'prefix', e.g. "Jet" while the pack is not worn
		bool SetPartHidden(const char* prefix, bool hide);

		// rotate a bone and everything below it about the bone's joint; axis in model frame, unit length
		void Turn(Pose& pose, int bone, const VECTOR3& axis, float angle) const;
		// rotate the whole pose about a model-frame pivot (e.g. the soles when leaning from the ankles)
		void TurnAll(Pose& pose, const VECTOR3& pivot, const VECTOR3& axis, float angle) const;
		void Shift(Pose& pose, const VECTOR3& d) const;
		// put a bone's subtree back in the relation to 'parent' it has in 'ref' (e.g. hands as in the idle pose), weight 0..1
		void Reattach(Pose& pose, const Pose& ref, int parent, int bone, float weight) const;
		// the same about an axis given in the bone's own frame (e.g. finger curl)
		void TurnLocal(Pose& pose, int bone, const VECTOR3& localAxis, float angle) const;

	private:
		struct BoneInfo { std::string name; int parent{}; float R0[9]{}, T0[3]{}; std::vector<int> subtree; };
		struct Group
		{
			std::vector<unsigned short> b; std::vector<float> w; std::vector<NTVERTEX> bind, base, work; std::vector<char> head;   // base = bind + morphs
			std::string label;                  // mesh group label (LABEL lines in the .skin)
			bool moved{}; float move[12]{};     // rest-pose transform of a movable part: R (3x3) then T (last 3), p' = R p + T
			bool hidden{}, hiddenDone{};        // part not shown (collapsed once)
		};

		std::vector<BoneInfo> bones;
		std::vector<Group> groups;
		struct Morph { std::string name; int group{}; float weight{}; std::vector<int> idx; std::vector<float> d; };   // d: 6 per vertex
		std::vector<Morph> morphs;
		void ApplyMorphs(int group);
		std::string meshName;
		DEVMESHHANDLE dev{};
		bool hideHead{};
		int headBone{ -1 };
		std::vector<float> S;   // last skin matrices, 12 floats per bone
		struct Hair
		{
			int group{ -1 }; bool driven{}, primed{};
			std::vector<float> free, w;          // per vertex: freedom 0 (root) .. 1 (tips); 4 node weights
			float c[3]{};                        // head-local centre of the hair volume (rest pose)
			double dt{}, g{ 9.81 }, ax{}, ay{}, az{};
			double p[3]{}, v[3]{};               // last centre position / velocity (model frame)
			double o[4][3]{}, ov[4][3]{};        // node offsets and velocities (head frame)
		} hair;
		void SetupHair();
		void StepHair(const float* sHead);
		void Rotate(Pose& pose, const std::vector<int>* set, VECTOR3 pivot, const VECTOR3& axis, float angle) const;   // set = nullptr: all bones
	};
}
