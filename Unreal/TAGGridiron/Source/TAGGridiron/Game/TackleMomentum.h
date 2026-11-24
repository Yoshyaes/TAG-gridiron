// Momentum in the tackle pick (Research/NextLevel/01, action 5): a paired tackle is picked by where its bodies end up
// (StartPairedTackle's slide residual and the tackler's offset) and also by how fast the two men move through the hit.
// A 9 m/s collision then plays a capture filmed at speed, not one filmed at a jog with the glide making up the rest.
// The captures' speeds come from their own root motion just after the contact (no data file needed).
#pragma once

#include "CoreMinimal.h"

class UAnimationAsset;

namespace TackleMomentum
{
	/** A capture's ground speed just after the contact: its root's travel over the 0.15 s from Start (cm/s; cached per
	 *  clip and start). < 0: not a sequence, or too short to measure. */
	float CaptureSpeed(const UAnimationAsset* Clip, float Start);
	/** The pick's momentum term (cm, added to a pair's cost): W x (|the tackler's capture speed - his speed now| + the
	 *  same for the carrier), W = 25 cm per m/s (-TagTackleMomW=<cm per m/s>), so it ranks below the slide residual.
	 *  Game speeds in cm/s. 0 with -TagNoTackleMomentum (A/B) or for a clip that cannot be measured. */
	float Cost(const UAnimationAsset* T, const UAnimationAsset* C, float Start, float GameT, float GameC);
}
