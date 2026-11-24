// The frame's contacts (Game/TagContact.h): this frame's list and the last 64, for the readers.
#include "Game/TagContact.h"

FTagContacts& FTagContacts::Get()
{
	static FTagContacts S;
	return S;
}

TArrayView<const FTagContact> FTagContacts::ThisFrame() const
{
	if (FrameNo != GFrameCounter) return TArrayView<const FTagContact>();
	return TArrayView<const FTagContact>(FrameList.GetData(), FrameList.Num());
}

void FTagContacts::BeginFrame(uint64 InFrame)
{
	if (FrameNo == InFrame) return; // (a second pass in one frame adds to it)
	FrameNo = InFrame;
	FrameList.Reset();
}

const FTagContact& FTagContacts::Add(const FTagContact& C)
{
	FTagContact S = C;
	S.Serial = NextSerial++;
	S.Frame = FrameNo;
	if (Ring.Num() < RingSize) Ring.Add(S);
	else
	{
		Ring[Head] = S;
		Head = (Head + 1) % RingSize;
	}
	return FrameList.Add_GetRef(S);
}

void FTagContacts::Reset()
{
	FrameList.Reset();
	Ring.Reset();
	Head = 0;
	FrameNo = 0;
}
