#pragma once

namespace HashUtil
{
	// Calc CRC32 of null terminated string
	UInt32 CRC32(const char* str, UInt32 start = 0);
}

SInt32 HashItemId(const char * name, UInt32 formID);
