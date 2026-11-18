// Simulation (yards, offense frame) <-> Unreal world (cm) conversions.
#pragma once

#include "CoreMinimal.h"
#include "Sim/Playbook.h"
#include "Sim/Vec.h"

namespace TagCoords
{
	constexpr double YD = 91.44; // cm per yard

	/** Offense-frame (x downfield 0..100, y 0..53.3 with +y = offense left) -> world. dir = +1 if offense attacks world +X. */
	inline FVector ToWorld(double X, double Y, int Dir, double Z = 0.0)
	{
		const double W = tag::FIELD_W;
		return Dir > 0
			? FVector(X * YD, (W * 0.5 - Y) * YD, Z * YD)
			: FVector((100.0 - X) * YD, (Y - W * 0.5) * YD, Z * YD);
	}

	/** World -> offense frame (the inverse of ToWorld, ignoring height). */
	inline tag::V2 FromWorld(const FVector& W, int Dir)
	{
		const double HalfW = tag::FIELD_W * 0.5;
		return Dir > 0 ? tag::V2{W.X / YD, HalfW - W.Y / YD} : tag::V2{100.0 - W.X / YD, HalfW + W.Y / YD};
	}

	/** Offense-frame direction vector -> world yaw (degrees). */
	inline float FaceToYaw(double FaceRad, int Dir)
	{
		const double FX = FMath::Cos(FaceRad);
		const double FY = FMath::Sin(FaceRad);
		const double WX = Dir > 0 ? FX : -FX;
		const double WY = Dir > 0 ? -FY : FY;
		return FMath::RadiansToDegrees(FMath::Atan2(WY, WX));
	}

	inline FLinearColor Hex(const std::string& H)
	{
		const FString S(H.c_str());
		return FLinearColor(FColor::FromHex(S));
	}
}
