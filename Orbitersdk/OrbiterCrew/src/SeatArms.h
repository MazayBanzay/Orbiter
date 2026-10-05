// OrbiterCrew - the arms of a seated person on the ship's controls (the user, 2026-10-04: the hands placed right, and on
// the yoke taken in hand without bugs and beautifully; seen through her eyes first). The ship says where each hand
// rests or what it holds (OcInteriorExt::SeatHands); the arm reaches it as a person's does:
//   - the shoulder girdle comes forward a little for a far reach (the clavicle);
//   - two-bone IK, the elbow a hinge: the upper arm is rolled so the elbow bends in the arm's own plane, never sideways,
//     the elbow kept down, out and a little back;
//   - the forearm takes most of the hand's turn about its own axis (pronation, supination), the wrist the rest, within
//     its range;
//   - a new thing is reached along an arc: the hand lifts off, travels, opens, comes onto it from its free side and
//     closes round it (minimum-jerk timing, the other hand a beat later); the same thing is followed rigidly.
// All in the model frame (her vessel's), after every other layer of the pose.
#pragma once
#include "Skin.h"

namespace ocrew
{
	struct ClipSet;

	// a hand's target in her model frame (OcHand from the ship, moved into it)
	struct HandTarget { bool on{}; int what{}; VECTOR3 pos{}, palm{}, fwd{}; int grip{}; double radius{}; bool floor{}; double elbowMinY{}; };

	class SeatArms
	{
	public:
		// the arms to their targets (in[0] left, in[1] right); heading: her facing about the vertical (rad, + right)
		void Update(double dt, const HandTarget in[2], double heading, const ClipSet& clips, const Skin& skin, Pose& pose);
		void Reset();                                  // not seated: her own arms, at once
		float Grip(int side) const { return arm[side & 1].grip; }
		float Handle(int side) const { return arm[side & 1].handle; }   // that hand round a handle ("grip" 0..1)
		float Cup(int side) const { return arm[side & 1].cup; }         // that hand over a ball ("cup" 0..1)   // how far that hand is closed ("fist" 0..1)
		// what that hand's fingers close round (a skin with finger bones): the thing it reaches for, and the one it leaves,
		// each with how far the hand is closed on it now (0..1)
		void Hold(int side, Skin::Shape& to, float& wTo, Skin::Shape& from, float& wFrom) const
		{ const Arm& a = arm[side & 1]; to = a.shape; wTo = a.wTo; from = a.fromShape; wFrom = a.wFrom; }
		// what the last step did, for tests: the wrist short of the target (m), the palm off it (m), the elbow, the wrist
		struct Diag { bool active{}; double beta{}, shortM{}, offM{}, elbowDeg{}, wristDeg{}, twistDeg{}; VECTOR3 elbow{}; };
		const Diag& Last(int side) const { return arm[side & 1].diag; }

	private:
		struct Frame { VECTOR3 p{}; double q[4]{ 1, 0, 0, 0 }; double grip{}, handle{}, cup{}, closed{}; };   // palm centre; hand rotation (rest -> posed)
		struct Arm
		{
			int C{ -1 }, U{ -1 }, F{ -1 }, H{ -1 };    // clavicle, upper arm, forearm, hand
			VECTOR3 hinge{};                            // the elbow's axis in the upper arm's own frame
			VECTOR3 normal{}, fingers{}, palm{};        // the hand at rest (Skin::Hand), the palm's normal made sure of
			VECTOR3 gripPt{};                           // where a handle lies in a power grip: on the palm at the fingers' base
			VECTOR3 fore{}; bool foreOk{};              // the forearm's direction last step (a handle is gripped along it)
			bool floor{}; double elbowMinY{};          // the elbow kept above this height (model frame)
			int what{ -2 };                             // the thing the hand is on (-1: her own pose, -2: not started)
			bool moving{};
			double s{}, dur{}, wait{}, beta{}, fromBeta{};
			Frame from, eff;
			float grip{}, handle{}, cup{};
			Skin::Shape shape, fromShape;               // what the fingers close round now, and what they left
			float wTo{}, wFrom{};
			Diag diag;
		} arm[2];
		bool bound{};
		void Bind(const Skin& skin, const ClipSet& clips);
		void Solve(Arm& a, int side, const Frame& t, double beta, double heading, const Skin& skin, Pose& pose);
	};
}
