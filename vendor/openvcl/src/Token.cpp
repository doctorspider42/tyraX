/*
 * Token.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Token.h"
#include "Error.h"
#include "stlhelp.h"

#include <iostream>
#include <stdlib.h>
#include <assert.h>

namespace vcl
{

bool Token::process( bool newSyntax )
{
	Token::Argument* error = NULL;

	if( operand()->alternatives().size() && newSyntax )
	{
		const Operand* simplified = operand(); // operand->simplified();
		for( std::list<const Operand*>::const_iterator i = simplified->alternatives().begin(); i != simplified->alternatives().end(); ++i )
		{
			if( processOperand( *i, newSyntax, error ) )
			{
				setOperand( *i );
				return true;
			}
		}
	}
	else if( processOperand( operand(), newSyntax, error ) )
		return true;

    // Guard against null error details; ensure we don't dereference a null pointer
    if( error )
        Error::Display( Error( "Invalid argument", *this, *error ) );
    else
        Error::Display( Error( "Invalid argument", *this ) );
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Token::processOperand( const Operand* operand, bool newSyntax, Token::Argument*& error )
{
	assert( operand );

	const std::string& pattern = operand->pattern();
	std::string::size_type ofs = 0;
	std::string::size_type next = 0;
	std::string::size_type last = 0;

	unsigned int activeFields = fields();
	unsigned int activeBroadcast = broadcast();

	if( !activeFields && (operand->flags() & Operand::DEST) )
	{
		if( (operand->flags() & Operand::XYZ) == Operand::XYZ )
			activeFields = X|Y|Z;
	}

	std::list<Argument>::iterator i;
	unsigned int argumentIndex;
	for( argumentIndex = 0, i = m_arguments.begin(); (i != m_arguments.end()) && (ofs < pattern.length()); argumentIndex++,i++ )
	{
		// reset argument for use

		(*i).resetResult();

		// get current pattern

		const std::string& arg = (*i).text();

		std::string curr;

		// restart pattern if needed

		if( !ncasecompare( 0, std::string::npos, "...", pattern.substr(ofs) ) )
			ofs = last;

		next = pattern.find(',',ofs);
		if( std::string::npos == next )
			next = pattern.length();

		curr = pattern.substr(ofs,next-ofs);

		while( 1 )
		{
			// extract sub-pattern

			std::string::size_type subofs = curr.find('|');

			std::string temp = curr.substr(0,subofs);

			std::string::size_type modofs = temp.find(':');
			std::string buffer = temp.substr(0,modofs);

			unsigned int modifiers = 0;
			if( std::string::npos != modofs )
				modifiers = extractModifiers( temp.substr(modofs) );

			// process argument

			std::string::size_type j = 0;
			std::string::size_type ai = 0;
			std::string curr_arg;
			bool match = true;
			int parentheses = 0;

			while( ((j < buffer.length()) || ((ai < arg.length()) || curr_arg.length())) && match )
			{
				std::string::size_type next = j+1;

				// grab next length of argument to compare

				if( !curr_arg.length() )
				{
					std::string::size_type aj = ai;
					char lc = '\0';
					int argparen = 0;
					while( aj < arg.length() )
					{
						if( ('(' == arg[aj]) )
						{
							if( !(('+' == lc)||('-' == lc)||('*' == lc)||('/' == lc)||('|' == lc)||('&' == lc)||('(' == lc)||('\0' == lc)) )
								break;
							argparen++;
						}

						if( ')' == arg[aj] )
						{
							if( --argparen < 0 )
								match = false;
						}

						if( !isBlank(arg[aj]) )
							lc = arg[aj];

						aj++;
					}

					curr_arg = trim(arg.substr(ai,aj-ai));
					ai = aj;
				}

				// match string

				if( '\'' == buffer[j] )
				{
					// match text-string

					std::string::size_type ofs = j+1;
					std::string::size_type eot = buffer.find('\'',ofs);

					if( casecompare( arg, buffer.substr( ofs, eot-ofs ) ) )
						match = false;

					(*i).setType( Argument::STRING );
					(*i).setImmediate( arg );

					curr_arg.clear();

					next = eot+1;
				}
				else if( '(' == buffer[j] )
				{
					parentheses++;
				}
				else if( ')' == buffer[j] )
				{
					if( --parentheses < 0 )
						match = false;
				}
				else
				{
					// get next element in subpattern

					std::string::size_type k = j+1;
					while( (k < buffer.length()) && ('(' != buffer[k])  && (')' != buffer[k]) ) k++;
					std::string name = buffer.substr( j, k-j );

					unsigned int fields = 0;

					switch( classifyArgument( name ) )
					{
						case Argument::FLOAT_REGISTER:
						{
							(*i).setType( Argument::FLOAT_REGISTER );

							if( !extractRegister( curr_arg, *i, modifiers, parentheses > 0, fields, newSyntax ) )
								match = false;
						}
						break;

						case Argument::INTEGER_REGISTER:
						{
							(*i).setType( Argument::INTEGER_REGISTER );

							if( !extractRegister( curr_arg, *i, modifiers, parentheses > 0, fields, newSyntax ) )
								match = false;
						}
						break;

						case Argument::ACCUMULATOR:
						{
							if( ncasecompare( 0, std::string::npos, "acc", curr_arg ) )
								match = false;

							if( curr_arg.length() > 3 )
							{
								if( !newSyntax )
								{
									// old syntax: ACCxyzw
									match &= extractFields( curr_arg.substr( 3 ), fields );
								}
								else
								{
									// new syntax: not allowed
									match = false;
								}
							}

							(*i).setType( Argument::ACCUMULATOR );
						}
						break;

						case Argument::Q:
						{
							if( casecompare( curr_arg, "q" ) )
								match = false;

							(*i).setType( Argument::Q );
						}
						break;

						case Argument::P:
						{
							if( casecompare( curr_arg, "p" ) )
								match = false;

							(*i).setType( Argument::P );
						}
						break;

						case Argument::R:
						{
							if( casecompare( curr_arg, "r" ) )
								match = false;

							(*i).setType( Argument::R );
						}
						break;

						case Argument::I:
						{
							if( casecompare( curr_arg, "i" ) )
								match = false;

							(*i).setType( Argument::I );
						}
						break;

						case Argument::IMMEDIATE:
						{
							(*i).setType( Argument::IMMEDIATE );

							if( hasModifier( RAW, modifiers ) )
							{
								(*i).setImmediate( arg );
								ai = arg.length();
							}
							else
								(*i).setImmediate( curr_arg );
						}
						break;

						default:
							// Try to parse as a register alias
							// This allows custom variable names in old syntax mode (e.g., next_index, buffer_top)
							{
								// Try float register alias first
								(*i).setType( Argument::FLOAT_REGISTER );
								if( extractRegister( curr_arg, *i, modifiers, parentheses > 0, fields, newSyntax ) )
									break; // Successfully parsed as float register alias

								// Try integer register alias
								(*i).setType( Argument::INTEGER_REGISTER );
								if( extractRegister( curr_arg, *i, modifiers, parentheses > 0, fields, newSyntax ) )
									break; // Successfully parsed as integer register alias

								// Not a valid alias either
								match = false;
							}
						break;
					}

					// verify argument field

					if( fields )
					{
						if( newSyntax && (countFields( fields ) > 1) && !((*i).flags()&Token::Argument::INDIRECT) )
							match = false;

						if( (operand->flags() & Operand::DEST) )
						{
							if( hasModifier( BROADCAST, modifiers ) )
							{
								if( (operand->flags() & Operand::BROADCAST) == Operand::BROADCAST )
								{
									// verify only on set bit
									if( 1 != countFields( fields ) )
										match = false;

									if( !activeBroadcast )
										activeBroadcast = fields;
									else if( activeBroadcast != fields )
										match = false;
								}
								else
									match = false;
							}
							else if( hasModifier( FLAG, modifiers ) )
							{
								if( 1 != countFields( fields ) )
									match = false;
							}
							else if( hasModifier( DEST, modifiers ) )
							{
								if( !activeFields )
									activeFields = fields;
								else if( (activeFields != fields) )
									match = false;
							}
							else
								match = false;

							(*i).setFields( fields );
						}
						else
						{
							if( hasModifier( FLAG, modifiers ) )
							{
								if( 1 != countFields( fields ) )
									match = false;
							}
							else
								match = false;
							(*i).setFields( fields );
						}
					}
					else if( (*i).type() != Argument::IMMEDIATE )
					{
						if( ((operand->flags() & Operand::XYZ) == Operand::XYZ) && hasModifier( DEST, modifiers ) )
							fields = X|Y|Z;
						else if( (operand->flags() & Operand::DEST) && hasModifier( DEST, modifiers ) )
							fields = activeFields;

						if( !activeFields )
							activeFields = fields;

						(*i).setFields( fields );

						// A 'wcomp' operand (CLIPw's second source, and the
						// other ...w forms) can only ever mean the w component -
						// the ISA offers no alternative - so an omitted selector
						// is not ambiguous. Default it to w and accept, which is
						// what the original VCL does: sources in the wild write
						// `clipw.xyz vertex, vertex`, and rejecting that form
						// dropped the whole instruction while the fcand reading
						// its clip flags stayed, i.e. a frustum test against
						// flags nobody set.

						if( hasModifier( WCOMP, modifiers ) && !countFields( fields ) )
						{
							fields = W;
							(*i).setFields( fields );
						}

						// if flag hasn't been specified, fail

						if( hasModifier( FLAG, modifiers ) && !( hasModifier( WCOMP, modifiers ) && fields == W ) )
							match = false;

						if( hasModifier( BROADCAST, modifiers ) && !activeBroadcast )
							match = false;
					}

					// flag argument accordingly

					if( hasModifier( BROADCAST, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::BROADCAST );
					if( hasModifier( FLAG, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::FLAG );
					if( hasModifier( WRITE, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::WRITE );
					if( hasModifier( BRANCH, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::BRANCH );
					if( hasModifier( ADDRESS, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::ADDRESS );
					if( hasModifier( DEST, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::DEST );
					if( hasModifier( EVALUATE, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::EVALUATE );
					if( hasModifier( ROTATE, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::ROTATE );
					if( hasModifier( RAW, modifiers ) )
						(*i).setFlags( (*i).flags() | Argument::RAW );

					if( (parentheses > 0) && (!compareParentheses( curr_arg, parentheses ) || (parentheses > 1)) )
						match = false;

					curr_arg.clear();

					next = k;
				}

				j = next;
			}

			// insert a zero-immediate to arguments that need it

			if( hasModifier( ZERO, modifiers ) && ((*i).immediate().length() > 0) )
				match = false;
			else if( hasModifier( ZERO, modifiers ) )
				(*i).setImmediate("0");

			if( !parentheses && match )
				break;

			if( !match )
				error = &(*i);

			// next sub-pattern (fail if we passed them all)

			if( std::string::npos == subofs )
				return false;

			curr = curr.substr(subofs+1);
		}

		last = ofs;	
		ofs = next+1;
	}

	// validate results

	if( i != m_arguments.end() )
		return false;

	if( !activeBroadcast && ((operand->flags() & Operand::BROADCAST) == Operand::BROADCAST) )
		return false;

	// validate fields

	if( (operand->flags() & Operand::DEST) )
	{
		if( !activeFields )
			activeFields = X|Y|Z|W;

		std::list<Argument>::iterator i;
		unsigned int argumentIndex;
		for( argumentIndex = 0, i = m_arguments.begin(); (i != m_arguments.end()); argumentIndex++,i++ )
		{
			if( (*i).flags() & Argument::DEST )
			{
				if( !(*i).fields() )
				{
					(*i).setFields( activeFields );
				}
				else if( (*i).fields() != activeFields )
				{
					error = &(*i);
					return false;
				}
			}

			// support for MR32 field rotation
			if( (*i).flags() & Argument::ROTATE )
				(*i).setFields( (((*i).fields()<<1)&(Y|Z|W)) | (((*i).fields()>>3)&X) );
		}
	}

	// opcode matches completely, store fields in token

	if( (X|Y|Z|W) == activeFields )
	{
		if( (operand->flags() & Operand::DEST) )
			setFields( 0 );
		else
			setFields( activeFields );
	}
	else
	{
		setFields( activeFields );
	}

	setBroadcast( activeBroadcast );

	// Debug print disabled by default; re-enable locally when diagnosing operand parsing.
	// printInformation( operand, std::cerr );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Token::compareParentheses( const std::string& argument, int parentheses )
{
	bool limit_hit = false;
	int limit = 0;

	for( std::string::const_iterator i = argument.begin(); i != argument.end(); ++i )
	{
		if( (limit == parentheses) && !limit_hit )
			limit_hit = true;

		if( '(' == *i )
		{
			if( limit_hit )
				return false;
			limit++;
		}
		else if( ')' == *i )
		{
			if( !limit_hit )
				return false;

			limit--;
		}

		if( limit < 0 )
			return false;
	}

	return true;
}

unsigned int Token::extractModifiers( const std::string& list )
{
	std::string::size_type i = 0;
	unsigned int modifiers = 0;

	while(1)
	{
		std::string::size_type next = list.find(':',i);
		std::string name = list.substr( i, next-i );

		if( name == "dest" )
			modifiers |= (1<<DEST);
		else if( name == "flag" )
			modifiers |= (1<<FLAG);
		else if( name == "postinc" )
			modifiers |= (1<<POSTINC);
		else if( name == "predec" )
			modifiers |= (1<<PREDEC);
		else if( name == "bc" )
			modifiers |= (1<<BROADCAST);
		else if( name == "wcomp" )
			// treat 'wcomp' like a single-component selector requirement, but
			// keep it distinguishable from a plain 'flag': the component is not
			// a free choice for these operands, it is always w, so an omitted
			// selector can be defaulted instead of rejected (see process()).
			modifiers |= (1<<FLAG) | (1<<WCOMP);
		else if( name == "noalias" )
			modifiers |= (1<<NOALIAS);
		else if( name == "write" )
			modifiers |= (1<<WRITE);
		else if( name == "branch" )
			modifiers |= (1<<BRANCH);
		else if( name == "address" )
			modifiers |= (1<<ADDRESS);
		else if( name == "zero" )
			modifiers |= (1<<ZERO);
		else if( name == "evaluate" )
			modifiers |= (1<<EVALUATE);
		else if( name == "rotate" )
			modifiers |= (1<<ROTATE);
		else if( name == "raw" )
			modifiers |= (1<<RAW);
		else if( name == "range" )
			modifiers |= (1<<RANGE);

		if( std::string::npos == next )
			break;

		i = next+1;
	}

	return modifiers;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Token::extractRegister( std::string name, Argument& argument, unsigned int modifiers, bool indirect, unsigned int& fields, bool newSyntax )
{
	const char* prefix = NULL;
	int regnum = -1;
	int maxreg;

	switch( argument.type() )
	{
		case Argument::FLOAT_REGISTER:		prefix = "vf"; maxreg = 32; break;
		case Argument::INTEGER_REGISTER:	prefix = "vi"; maxreg = 16; break;
		default: return false;
	}

	// Register-range shorthand: when the operand template carries the
	// 'range' modifier, accept "vfXX-vfYY" (or "viXX-viYY") and store the
	// pair as a RANGE argument.  Consumed by Tokenizer for .init_vf etc.
	// We require both halves to use the same prefix as the argument type,
	// and the lower bound must come first.
	if( !indirect && hasModifier( RANGE, modifiers ) )
	{
		std::string::size_type dash = name.find('-');
		if( std::string::npos != dash )
		{
			std::string left  = name.substr(0, dash);
			std::string right = name.substr(dash+1);

			if( !ncasecompare(0, 2, left, prefix) && !ncasecompare(0, 2, right, prefix)
			    && left.length() >= 3 && right.length() >= 3
			    && isdigit(left[2]) && isdigit(right[2]) )
			{
				int lo = atoi(left.c_str()  + 2);
				int hi = atoi(right.c_str() + 2);

				if( lo >= maxreg || hi >= maxreg || lo > hi )
					return false;

				argument.setRange(lo, hi);
				return true;
			}
		}
	}

	// if this is a indirect read, parse it properly

	if( indirect )
	{
		argument.setFlags( argument.flags() | Argument::INDIRECT );

		std::string::size_type start = name.find('(');
		if( 0 != start )
			return false;
		start++;

		std::string::size_type stop = name.find(')');
		if( std::string::npos == stop )
			return false;

		if( hasModifier( POSTINC, modifiers ) )
		{
			if( name.compare( stop-2, 2, "++" ) )
				return false;

			stop -= 2;
			argument.setFlags( argument.flags() | Argument::POSTINC );			
		}
		else if( hasModifier( PREDEC, modifiers ) )
		{
			if( name.compare( start, 2, "--" ) )
				return false;

			start += 2;
			// PREDEC, not POSTINC.  `Argument::PREDEC` is the flag the emitter
			// tests to write `(--VI02)` back out (CodeGenerator::emitArgument);
			// tagging a pre-decrement as a post-increment meant the flag was
			// never set on any argument in the program, the emitter's `--`
			// branch was dead, and every `lqd`/`sqd` came out as
			// `lqd VF01,(VI02++)` - which dvp-as rejects, so no source using
			// pre-decrement addressing could be built at all.
			argument.setFlags( argument.flags() | Argument::PREDEC );
		}

		std::string extra = name.substr(name.find(')',stop)+1);
		name = name.substr( start, stop-start );

			// Accept and strip VCL memory-group suffixes; scheduling now uses
			// explicit memory descriptors derived from the parsed register/offset.
			extra = extra.substr( 0, extra.find(':') );

		// parse any attached fields (placed after the terminating ')' on indirect reads)

		if( extra.length() )
		{
			if( extractFields( extra, fields ) )
			{
				if( !hasModifier( DEST, modifiers ) )
					return false;
			}
			else return false;
		}
	}

	if( !ncasecompare( 0, 2, name, prefix ) && (name.length() >= 3) )
	{
		if( isdigit(name[2]) )
		{
			regnum = atoi(&name.c_str()[2]);

			if( regnum >= maxreg )
				return false;

			std::string::size_type offset = name.find_first_not_of( "0123456789", 2 );

			if( name.length() > offset )
			{
				// cannot have fields directly attached when indirect

				if( indirect )
					return false;

				// choose proper parse-method

				if( newSyntax )
				{
					std::string::size_type start = name.rfind( '[' );
					if( offset == start )
					{
						start++;

						std::string::size_type stop = name.find( ']', start );
						if( std::string::npos != stop )
						{
							if( extractFields( name.substr( start, stop-start ), fields ) )
							{
								if( !(hasModifier( BROADCAST, modifiers ) || hasModifier( FLAG, modifiers )) )
									return false;
							}
						}
					}
					else
						return false;
				}
				else
				{
					if( extractFields( name.substr(offset), fields ) )
					{
						if( !( hasModifier( DEST, modifiers ) || hasModifier( BROADCAST, modifiers ) || hasModifier( FLAG, modifiers ) ) )
							return false;
					}
					else
						return false;
				}
			}
		}
	}

	std::string alias;
	if( regnum < 0 )
	{
		if( hasModifier( NOALIAS, modifiers ) )
			return false;

		std::string::size_type start = name.rfind( '[' );
		if( std::string::npos != start )
		{
			start++;

			std::string::size_type stop = name.find( ']', start );
			if( std::string::npos != stop )
			{
				if( extractFields( name.substr( start, stop-start ), fields ) )
				{
					if( indirect )
						return false;

					if( !newSyntax || !(hasModifier( BROADCAST, modifiers ) || hasModifier( FLAG, modifiers )) )
						return false;

					alias = name.substr( 0, start-1 );
				}
			}
		}

		// Support old-syntax component selection appended directly to aliases,
		// e.g. "gif_tagx" or "xformed_vertw" -> treat trailing xyzw as fields.
		//
		// WHETHER A NAME CAN CARRY A FIELD IS A PROPERTY OF THE ARGUMENT, NOT OF THE
		// SPELLING. Only an argument the operand pattern marks :dest, :bc or :flag
		// takes one; the destination of an `iaddiu`, the value operand of an `isw`
		// and a branch's condition never do. This used to strip first and then
		// reject the WHOLE argument when no such modifier was present, so an
		// integer alias whose name happened to end in x, y, z or w was
		// "Invalid argument" - and the list of names that survived it was a
		// hardcoded set of English trigrams ("dex", "tex", "lex", "rex", "sex"),
		// which is how `next_index` compiled and `next_matrix` did not. Sony's vcl
		// accepts both and reads neither as a field, so the trigrams were also a
		// silent divergence in the other direction: a FLOAT alias called `vertex`
		// is `verte` field x to Sony's vcl and a whole register here.
		//
		// Asking the modifier first answers both: an argument that cannot take a
		// field keeps its name whole whatever it is spelled, and one that can is
		// stripped whatever it is spelled - which is what the reference does.
		if( alias.empty() )
		{
			if( !newSyntax && !indirect
			    && ( hasModifier( DEST, modifiers ) || hasModifier( BROADCAST, modifiers )
			         || hasModifier( FLAG, modifiers ) ) )
			{
				// Find contiguous trailing xyzw characters
				std::string::size_type end = name.size();
				std::string::size_type pos = end;
				while( pos > 0 )
				{
					char c = name[pos-1];
					if( c == 'x' || c == 'y' || c == 'z' || c == 'w' )
						pos--;
					else
						break;
				}

				// A name that is NOTHING but field letters is a field selection with
				// no alias in front of it, which is not a register reference at all.
				if( pos < end && pos > 0 )
				{
					std::string fieldStr = name.substr( pos );
					unsigned int parsedFields = 0;
					if( extractFields( fieldStr, parsedFields ) )
					{
						fields |= parsedFields;
						alias = name.substr( 0, pos );
					}
				}
			}
		}

		if( !alias.length() )
			alias = name;
	}

	if( regnum >= 0 )
		argument.setRegNumber( regnum );
	else
	{
		if( !validateAlias( alias ) )
			return false;
		argument.setAlias( alias );
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Token::extractFields( const std::string& fields, unsigned int& result )
{
	static const char* fieldnames = "xyzw";

	result = 0;
	for( unsigned int t = 0, fieldofs = 0; (t < fields.length()); ++t )
	{
		while( fieldnames[fieldofs] && fieldnames[fieldofs] != fields[t] )
			fieldofs++;

		if( fieldnames[fieldofs] == fields[t] )
			result |= 1<<fieldofs;
		else
		{
			result = 0;
			return false;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int Token::countFields( unsigned int fields )
{
	unsigned int count = 0;
	while( fields > 0 )
	{
		if( fields & 1 )
			count++;
		fields >>= 1;
	}
	return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Token::validateAlias( const std::string& alias )
{
	static const char* reserved[] =
	{
		"q",
		"p",
		"acc",
		"i"
	};

	if( !alias.length() )
		return false;

	// make sure we're not using any reserved registers

	for( unsigned int t = 0; t < sizeof(reserved) / sizeof(const char*); t++ )
	{
		if( !casecompare( reserved[t], alias ) )
			return false;
	}

	if( !alias.compare( 0, 2, "vi" ) || !alias.compare( 0, 2, "vf" ) )
	{
		if( alias.length() >= 3 )
		{
			if( isdigit(alias[2]) )
				return false;
		}
	}

	// cannot start with a number

	if( (alias[0] >= '0') && (alias[0] <= '9') )
		return false;

	// verify string content

	for( std::string::const_iterator i = alias.begin(); i != alias.end(); ++i )
	{
		if( ((*i) >= 'a') && ((*i) <= 'z') )
			continue;

		if( ((*i) >= 'A') && ((*i) <= 'Z') )
			continue;

		if( ((*i) >= '0') && ((*i) <= '9') )
			continue;

		if( ((*i) == '[') || ((*i) == ']') )
			continue;

		if( ((*i) == '_') )
			continue;

		if( ((*i) == '.') )
			continue;

		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Token::Argument::Type Token::classifyArgument( const std::string& name )
{
	if( "vf" == name )
		return Argument::FLOAT_REGISTER;
	else if( "vi" == name )
		return Argument::INTEGER_REGISTER;
	else if( "imm" == name )
		return Argument::IMMEDIATE;
	else if( "acc" == name )
		return Argument::ACCUMULATOR;
	else if( "i" == name )
		return Argument::I;
	else if( "q" == name )
		return Argument::Q;
	else if( "r" == name )
		return Argument::R;
	else if( "p" == name )
		return Argument::P;

	return Argument::INVALID_TYPE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Token::printInformation( const Operand* operand, std::ostream& stream )
{
	stream << line().number() << ": " << line().content() << std::endl;

	if( (operand->flags() & Operand::BROADCAST) == Operand::BROADCAST )
		stream << "op: " << operand->name() << " [broadcast]" << std::endl;
	else
		stream << "op: " << operand->name() << std::endl;
	{
		static const char* fieldnames = "xyzw";

		for( std::list<Argument>::const_iterator i = m_arguments.begin(); (i != m_arguments.end()); i++ )
		{
			stream << "argument classified as ";
			switch( (*i).type() )
			{
				case Argument::STRING: stream << "string"; break;
				case Argument::FLOAT_REGISTER: stream << "float register"; break;
				case Argument::INTEGER_REGISTER: stream << "integer register"; break;
				case Argument::IMMEDIATE: stream << "immediate"; break;
				case Argument::Q: stream << "Q"; break;
				case Argument::P: stream << "P"; break;
				case Argument::I: stream << "I"; break;
				case Argument::R: stream << "R"; break;
				case Argument::ACCUMULATOR: stream << "accumulator"; break;
				default: stream << "unknown"; break;
			}

			switch( (*i).content() )
			{
				case Argument::REGISTER: stream << ", content is a register number, " << (*i).regNumber() << " (" << (*i).immediate() << ")"; break;
				case Argument::ALIAS: stream << ", content is an alias, '" << (*i).alias() << "' (" << (*i).immediate() << ")"; break;

				default:
				{
					if( (*i).immediate().length() )
						stream << ", content is " << (*i).immediate();
				}
				break;
			}

			if( (*i).fields() )
			{

				stream << " [";
				for( unsigned int t = 0; t < 4; t++ )
				{
					if( (*i).fields() & (1<<t) )
						stream << fieldnames[t];
					else
						stream << ' ';
				}
				stream << "]";
			}

			if( (*i).flags() & Argument::INDIRECT )
				stream << " [INDIRECT]";

			if( (*i).flags() & Argument::POSTINC )
				stream << " [POSTINC]";

			if( (*i).flags() & Argument::PREDEC )
				stream << " [PREDEC]";

			if( (*i).flags() & Argument::WRITE )
				stream << " [WRITE]";

			stream << std::endl;
		}

		if( operand->flags() & Operand::DEST )
		{
			stream << "dest: [";
			for( unsigned int t = 0; t < 4; t++ )
			{
				if( fields() & (1<<t) )
					stream << fieldnames[t];
				else
					stream << ' ';
			}
			stream << "]" << std::endl;
		}

		if( (operand->flags() & Operand::BROADCAST) == Operand::BROADCAST )
		{
			stream << "bc:   [";
			for( unsigned int t = 0; t < 4; t++ )
			{
				if( broadcast() & (1<<t) )
					stream << fieldnames[t];
				else
					stream << ' ';
			}
			stream << "]" << std::endl;
		}

		if( flags() & (Token::E|Token::D|Token::T) )
		{
			stream << "flags:[";

			if( flags() & Token::E ) stream << "E";
			if( flags() & Token::D ) stream << "D";
			if( flags() & Token::T ) stream << "T";

			stream << "]" << std::endl;
		}
	}
	stream << std::endl;
}

}
