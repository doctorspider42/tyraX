#ifndef __OPENVCL_VUTOKENRESOURCEACCESS_H__
#define __OPENVCL_VUTOKENRESOURCEACCESS_H__

/*
 * VuTokenResourceAccess.h
 *
 * Table-driven resource descriptors for parsed VU tokens.
 */

#include "Token.h"
#include "VuInstructionInfo.h"

#include <list>
#include <string>

namespace vcl
{

struct VuTokenResourceAccess
{
	VuTokenResourceAccess();

	std::list<std::string> registerReads;
	std::list<std::string> registerWrites;
	unsigned int instructionFlags;
	unsigned int implicitReads;
	unsigned int implicitWrites;
	VuMemoryKind memoryKind;
	unsigned int memoryFlags;
	bool hasMemoryBase;
	std::string memoryBaseRegister;
	bool hasMemoryOffset;
	long memoryOffset;
	unsigned int branchDelaySlots;
	unsigned int bypassFlags;
};

bool buildVuTokenResourceAccess( const Token& token, VuTokenResourceAccess& access );
bool vuRegisterKey( const Token::Argument& arg, std::string& key );
unsigned int vuReadFieldMask( const Token& token, const Token::Argument& arg );
unsigned int vuWriteFieldMask( const Token& token, const Token::Argument& arg );

}

#endif
