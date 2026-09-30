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
		// a rest-pose point carried by a bone (e.g. the eye point on the head), after the last Apply
		VECTOR3 Point(int bone, const VECTOR3& restPoint) const;

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
		struct Group { std::vector<unsigned short> b; std::vector<float> w; std::vector<NTVERTEX> base, work; std::vector<char> head; };

		std::vector<BoneInfo> bones;
		std::vector<Group> groups;
		std::string meshName;
		DEVMESHHANDLE dev{};
		bool hideHead{};
		int headBone{ -1 };
		std::vector<float> S;   // last skin matrices, 12 floats per bone
		void Rotate(Pose& pose, const std::vector<int>* set, VECTOR3 pivot, const VECTOR3& axis, float angle) const;   // set = nullptr: all bones
	};
}
