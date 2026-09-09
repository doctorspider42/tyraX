#include "VuTokenResourceAccess.h"

#include "Dependency.h"
#include "Expression.h"
#include "Math.h"

#include <iomanip>
#include <sstream>

namespace vcl
{

namespace
{
	void appendFieldKeys( const std::string& base, unsigned int fields, std::list<std::string>& keys )
	{
		static const char* names = "xyzw";
		if( fields == 0 )
			fields = Token::X | Token::Y | Token::Z | Token::W;
		for( unsigned int i = 0; i < 4; ++i )
		{
			if( fields & (1 << i) )
			{
				std::string key = base;
				key += ".";
				key += names[i];
				keys.push_back( key );
			}
		}
	}

	unsigned int resourceForArgument( const Token::Argument& arg )
	{
		switch( arg.type() )
		{
			case Token::Argument::ACCUMULATOR: return VU_RESOURCE_ACC;
			case Token::Argument::I: return VU_RESOURCE_I;
			case Token::Argument::Q: return VU_RESOURCE_Q;
			case Token::Argument::P: return VU_RESOURCE_P;
			case Token::Argument::R: return VU_RESOURCE_R;
			default: return VU_RESOURCE_NONE;
		}
	}

	const VuInstructionInfo* tokenInstructionInfo( const Token& token )
	{
		if( !token.operand() )
			return 0;
		return findVuInstructionInfo( normalizeVuMnemonic( token.operand()->name() ) );
	}

	bool evaluateMemoryOffset( const Token::Argument& arg, long& offset )
	{
		Expression e;
		e.setCustomOperators( Math::mathOperators() );
		if( !e.process( arg.immediate() ) || !e.solve() )
			return false;
		offset = static_cast<long>( e.result() );
		return true;
	}
}

VuTokenResourceAccess::VuTokenResourceAccess()
{
	instructionFlags = VU_INSTR_NONE;
	implicitReads = VU_RESOURCE_NONE;
	implicitWrites = VU_RESOURCE_NONE;
	memoryKind = VU_MEMORY_NONE;
	memoryFlags = VU_MEMORY_FLAG_NONE;
	hasMemoryBase = false;
	hasMemoryOffset = false;
	memoryOffset = 0;
	branchDelaySlots = 0;
	bypassFlags = VU_BYPASS_NONE;
}

bool vuRegisterKey( const Token::Argument& arg, std::string& key )
{
	if( arg.type() != Token::Argument::FLOAT_REGISTER
	    && arg.type() != Token::Argument::INTEGER_REGISTER )
		return false;

	if( arg.content() == Token::Argument::ALIAS )
	{
		Dependency* dependency = arg.dependency();
		if( dependency && dependency->alias()
		    && dependency->alias()->allocatedRegister() )
		{
			key = dependency->alias()->allocatedRegister()->name();
			return true;
		}
		if( !arg.alias().empty() )
		{
			key = arg.alias();
			return true;
		}
	}

	std::stringstream s;
	s << ((arg.type() == Token::Argument::FLOAT_REGISTER) ? "VF" : "VI")
	  << std::setw(2) << std::setfill('0') << arg.regNumber();
	key = s.str();
	return true;
}

unsigned int vuReadFieldMask( const Token& token, const Token::Argument& arg )
{
	if( arg.type() != Token::Argument::FLOAT_REGISTER )
		return Token::X | Token::Y | Token::Z | Token::W;

	if( (arg.flags() & Token::Argument::BROADCAST) && token.broadcast() )
		return token.broadcast();

	unsigned int fields = arg.fields();
	if( fields == 0 )
		fields = token.fields();
	if( fields == 0 )
		fields = Token::X | Token::Y | Token::Z | Token::W;
	return fields;
}

unsigned int vuWriteFieldMask( const Token& token, const Token::Argument& arg )
{
	if( arg.type() != Token::Argument::FLOAT_REGISTER )
		return Token::X | Token::Y | Token::Z | Token::W;

	unsigned int fields = token.fields();
	if( fields == 0 )
		fields = arg.fields();
	if( fields == 0 )
		fields = Token::X | Token::Y | Token::Z | Token::W;
	return fields;
}

bool buildVuTokenResourceAccess( const Token& token, VuTokenResourceAccess& access )
{
	access = VuTokenResourceAccess();

	if( !token.operand() )
		return false;

	const VuInstructionInfo* info = tokenInstructionInfo( token );
	if( info )
	{
		access.instructionFlags = info->flags;
		access.implicitReads = info->implicitReads;
		access.implicitWrites = info->implicitWrites;
		access.memoryKind = info->memoryKind;
		access.memoryFlags = info->memoryFlags;
		access.branchDelaySlots = info->branchDelaySlots;
		access.bypassFlags = info->bypassFlags;
	}

	const std::list<Token::Argument>& args = token.arguments();
	for( std::list<Token::Argument>::const_iterator i = args.begin(); i != args.end(); ++i )
	{
		const unsigned int resource = resourceForArgument( *i );
		if( resource != VU_RESOURCE_NONE )
		{
			if( (*i).flags() & Token::Argument::WRITE )
				access.implicitWrites |= resource;
			else
				access.implicitReads |= resource;
		}

		std::string key;
		if( !vuRegisterKey( *i, key ) )
			continue;

		if( ((*i).flags() & Token::Argument::INDIRECT)
		    && (*i).type() == Token::Argument::INTEGER_REGISTER )
		{
			access.hasMemoryBase = true;
			access.memoryBaseRegister = key;
			access.hasMemoryOffset = evaluateMemoryOffset( *i, access.memoryOffset );
		}

		if( (*i).flags() & Token::Argument::WRITE )
		{
			if( (*i).type() == Token::Argument::FLOAT_REGISTER )
				appendFieldKeys( key, vuWriteFieldMask( token, *i ), access.registerWrites );
			else
				access.registerWrites.push_back( key );
		}
		else
		{
			if( (*i).type() == Token::Argument::FLOAT_REGISTER )
				appendFieldKeys( key, vuReadFieldMask( token, *i ), access.registerReads );
			else
				access.registerReads.push_back( key );
		}
	}

	return true;
}

}
