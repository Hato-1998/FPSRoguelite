// Copyright Epic Games, Inc. All Rights Reserved.

#include "Anim/FPSRGunMotionCurveNames.h"

namespace FPSRGunMotionCurveNames
{
	FPartCurveNames MakePartCurveNames(FName AttachSocket)
	{
		FPartCurveNames Names;
		if (AttachSocket.IsNone())
		{
			return Names;
		}

		const FString SocketStr = AttachSocket.ToString();
		Names.TX = FName(*(PartCurvePrefix + SocketStr + TEXT("_TX")));
		Names.TY = FName(*(PartCurvePrefix + SocketStr + TEXT("_TY")));
		Names.TZ = FName(*(PartCurvePrefix + SocketStr + TEXT("_TZ")));
		Names.RP = FName(*(PartCurvePrefix + SocketStr + TEXT("_RP")));
		Names.RY = FName(*(PartCurvePrefix + SocketStr + TEXT("_RY")));
		Names.RR = FName(*(PartCurvePrefix + SocketStr + TEXT("_RR")));
		return Names;
	}
}
