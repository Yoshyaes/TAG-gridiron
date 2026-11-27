// The tackle pick's momentum term (TackleMomentum.h).
#include "Game/TackleMomentum.h"

#include "Animation/AnimSequence.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ObjectKey.h"

float TackleMomentum::CaptureSpeed(const UAnimationAsset* Clip, float Start)
{
	const UAnimSequence* Seq = Cast<UAnimSequence>(Clip);
	if (!Seq) return -1.f;
	// (keyed by the object, not its address: the pack library is drawn again each quarter and a freed clip's address
	// can come back as another capture)
	static TMap<TPair<FObjectKey, int32>, float> Cache;
	const TPair<FObjectKey, int32> Key(FObjectKey(Seq), FMath::RoundToInt(Start * 100.f));
	if (const float* Found = Cache.Find(Key)) return *Found;
	const double Len = Seq->GetPlayLength();
	const double T0 = FMath::Clamp<double>(Start, 0.0, Len), T1 = FMath::Min(Len, T0 + 0.15);
	float V = -1.f;
	if (T1 - T0 > 0.05)
	{
		const FVector D = Seq->ExtractRootMotionFromRange(T0, T1, FAnimExtractContext(0.0, true)).GetTranslation();
		V = static_cast<float>(FVector(D.X, D.Y, 0.0).Size() / (T1 - T0));
	}
	Cache.Add(Key, V);
	return V;
}

float TackleMomentum::Cost(const UAnimationAsset* T, const UAnimationAsset* C, float Start, float GameT, float GameC)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoTackleMomentum"));
	static float W = FParse::Value(FCommandLine::Get(), TEXT("TagTackleMomW="), W) ? W : 25.f;
	if (bOff) return 0.f;
	const float VT = CaptureSpeed(T, Start), VC = CaptureSpeed(C, Start);
	if (VT < 0.f || VC < 0.f) return 0.f;
	return W * 0.01f * (FMath::Abs(VT - GameT) + FMath::Abs(VC - GameC));
}
