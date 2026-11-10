// Play art: the called play drawn on the field before the snap (routes, blocks, the run path, zones, blitzes).
// One actor, three procedural mesh layers drawn in order (soft glows and zone fills, dark outlines, the colour),
// rebuilt from lists of world-space strokes whenever the art changes. Material: M_TagPlayArt (unlit translucent,
// vertex colour; Tools/Art/ue_build_playart_mat.py).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlayArt.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EArtEnd : uint8 { None, Arrow, Block, Dot };

UCLASS()
class TAGGRIDIRON_API ATagPlayArt : public AActor
{
	GENERATED_BODY()

public:
	ATagPlayArt();
	virtual void Tick(float DeltaSeconds) override;

	/** Start a new picture (the strokes added after this replace the old ones at Commit). */
	void Begin();
	/** A stroke along world points (z ignored: it lies on the turf). Width in cm. bGlow: a soft halo under it. */
	void Line(const TArray<FVector>& Pts, const FLinearColor& Color, float Width, EArtEnd End, bool bGlow = false);
	/** A zone: a filled ellipse with a bright rim (radii in cm, along the world axes). */
	void Zone(const FVector& Center, float RadiusX, float RadiusY, const FLinearColor& Color);
	void Commit();
	/** Shown or hidden: the art fades in and out over a few frames. */
	void SetShown(bool bShow) { bShown = bShow; }
	bool IsShown() const { return bShown; }
	float Alpha() const { return Fade; }

private:
	struct FLayer
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FLinearColor> C;
		void Reset() { V.Reset(); T.Reset(); C.Reset(); }
		void Tri(const FVector& A, const FVector& B, const FVector& D, const FLinearColor& Col);
		void Quad(const FVector& A, const FVector& B, const FVector& D, const FVector& E, const FLinearColor& Col);
	};
	void Ribbon(FLayer& L, const TArray<FVector2D>& P, float W, const FLinearColor& Col, float Z);
	void Disc(FLayer& L, const FVector2D& C, float RX, float RY, const FLinearColor& Col, float Z);
	void Ring(FLayer& L, const FVector2D& C, float RX, float RY, float W, const FLinearColor& Col, float Z);
	void EndCap(FLayer& L, const FVector2D& At, const FVector2D& Dir, EArtEnd End, float W, float Grow, const FLinearColor& Col, float Z);
	FLayer Layers[3];

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Mat;
	bool bShown = false;
	float Fade = 0.f;
};
