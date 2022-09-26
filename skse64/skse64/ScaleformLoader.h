#pragma once

#include "skse64/ScaleformState.h"

class NiTexture;
class IMenu;
class GImageInfoBase;

class GFxImageLoader : public GFxState
{
public:
	virtual GImageInfoBase*	LoadImage(const char * url) = 0;
};

class BSScaleformImageLoader : public GFxImageLoader
{
public:
	virtual ~BSScaleformImageLoader();
	virtual GImageInfoBase*	LoadImage(const char * url);

	DEFINE_MEMBER_FN_1(AddVirtualImage, UInt8, 0x00FA00C0, NiTexture ** texture);
	DEFINE_MEMBER_FN_1(ReleaseVirtualImage, UInt8, 0x00FA0540, NiTexture ** texture);
};

class GFxLoader
{
public:
	UInt64			unk00;					// 00
	GFxStateBag		* stateBag;				// 08
	UInt64			unk08;					// 10
	UInt64			unk10;					// 18
	BSScaleformImageLoader	* imageLoader;	// 20

	static GFxLoader * GetSingleton();

	MEMBER_FN_PREFIX(GFxLoader);
	DEFINE_MEMBER_FN(ctor, GFxLoader *, 0x00F23110);

	DEFINE_MEMBER_FN(LoadMovie, bool, 0x00F23840, IMenu* menu, GFxMovieView** viewOut, const char* name, int scaleMode, float backGroundAlpha);	// probably in subclass
};
