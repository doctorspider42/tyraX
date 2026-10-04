/*
 * CodeGenerator.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "CodeGenerator.h"
#include "stlhelp.h"
#include "Expression.h"
#include "Error.h"
#include "Math.h"
#include "VuSchedulerAnalysis.h"
#include "VuSchedulingRules.h"
#include "VuInstructionInfo.h"
#include "VuTokenResourceAccess.h"
#include "VuKernelLayout.h"
#include <cstdlib>
#include <map>
#include <set>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <assert.h>
#include <cmath>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

struct VuInstructionForm
{
	VuInstructionOpcode opcode;
	unsigned int broadcast;
	unsigned int flags;
	unsigned int fields;

	VuInstructionForm()
	{
		opcode = VU_OP_INVALID;
		broadcast = 0;
		flags = 0;
		fields = 0;
	}
};

struct CodeGenerator::FastNoLightsLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string colorReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string gsReg;
	std::string xformedCopyReg;
	std::string texReg;

	std::string mulaxOp;
	std::string maddayOp;
	std::string maddazOp;
	std::string maddwOp;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;

	FastNoLightsLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
	}
};

struct CodeGenerator::FastLitLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string normalReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string gsReg;
	std::string texInputReg;
	std::string texOutputReg;
	std::string lightDir0Reg;
	std::string lightDir1Reg;
	std::string lightDir2Reg;
	std::string cosinesReg;
	std::string clampedCosinesReg;
	std::string lightColor0Reg;
	std::string lightColor1Reg;
	std::string lightColor2Reg;
	std::string colorReg;
	std::string constantColorReg;
	std::string maxColorReg;
	std::string colorAccumReg;
	std::string colorRawReg;
	std::string xformedCarryReg;
	std::string xformedQReg;

	std::string transformMulaxOp;
	std::string transformMaddayOp;
	std::string transformMaddazOp;
	std::string transformMaddwOp;
	std::string lightDirMulaxOp;
	std::string lightDirMaddayOp;
	std::string lightDirMaddzOp;
	std::string lightColorMulaxOp;
	std::string lightColorMaddayOp;
	std::string lightColorMaddzOp;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;

	FastLitLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
		colorAccumReg = "VF22";
		colorRawReg = "VF23";
		xformedCarryReg = "VF24";
		xformedQReg = "VF25";
	}
};

struct CodeGenerator::SceiLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string normalReg;
	std::string stripAdcReg;
	std::string newAdcReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string xformedQReg;
	std::string gsReg;
	std::string gsOffsetsReg;
	std::string texInputReg;
	std::string texOutputReg;
	std::string clipReg;
	std::string clipScalesReg;
	std::string lightDir0Reg;
	std::string lightDir1Reg;
	std::string lightDir2Reg;
	std::string cosinesReg;
	std::string clampedCosinesReg;
	std::string lightColor0Reg;
	std::string lightColor1Reg;
	std::string lightColor2Reg;
	std::string colorReg;
	std::string colorRawReg;
	std::string constantColorReg;
	std::string maxColorReg;

	VuInstructionForm transformMulaxInstr;
	VuInstructionForm transformMaddayInstr;
	VuInstructionForm transformMaddazInstr;
	VuInstructionForm transformMaddwInstr;
	VuInstructionForm lightDirMulaxInstr;
	VuInstructionForm lightDirMaddayInstr;
	VuInstructionForm lightDirMaddzInstr;
	VuInstructionForm lightColorMulaxInstr;
	VuInstructionForm lightColorMaddayInstr;
	VuInstructionForm lightColorMaddzInstr;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;
	std::string adcImmediate;
	std::string clipImmediate;

	SceiLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		adcImmediate = "0x7fff";
		clipImmediate = "0x003ffff";
		xformedQReg = "VF24";
		colorRawReg = "VF25";
	}
};

struct CodeGenerator::LinearXformLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string vertexReg;
	std::string stripAdcReg;
	std::string stripFlipReg;
	std::string row0Reg;
	std::string row1Reg;
	std::string row2Reg;
	std::string row3Reg;
	std::string xformedReg;
	std::string gsReg;
	std::string gsOffsetsReg;
	std::string oldVertexReg;
	std::string oldDeltaReg;
	std::string deltaReg;
	std::string bfcNormalReg;
	std::string zSignReg;
	std::string zSignMaskReg;
	std::string zSignSwitchReg;
	std::string bfcMultiplierReg;
	std::string clipReg;
	std::string clipScalesReg;
	std::string clipResultReg;
	std::string doClippingReg;
	std::string newAdcReg;
	std::string constantColorReg;
	std::string texReg;

	std::string transformMulaxOp;
	std::string transformMaddayOp;
	std::string transformMaddazOp;
	std::string transformMaddwOp;
	std::string clipImmediate;
	std::string adcImmediate;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long stripOffset;
	long texOffset;
	long texStoreOffset;
	long colorStoreOffset;
	long gsStoreOffset;

	LinearXformLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		stripOffset = 0;
		texOffset = 0;
		texStoreOffset = 0;
		colorStoreOffset = 0;
		gsStoreOffset = 0;
		clipImmediate = "0x003ffff";
		adcImmediate = "0x7fff";
	}
};

struct CodeGenerator::DirLightNoSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologOneLabel;
	std::string prologTwoLabel;
	std::string prologThreeLabel;
	std::string mainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string fallbackThreeLabel;
	std::string fallbackFourLabel;
	std::string drainLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string colorReg;
	std::string lightDirReg;
	std::string productReg;
	std::string dotBaseReg;
	std::string lightColorReg;
	std::string materialMulReg;
	std::string materialAddReg;
	std::string ambientReg;
	std::string scaledReg;
	std::string litReg;
	std::string resultReg;
	std::string dotReg;
	std::string dotNextReg;
	long inputStep;
	long outputStep;
	long normalOffset;
	long colorOffset;
	long storeOffset;

	DirLightNoSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		normalOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
	}
};

struct CodeGenerator::DirLightSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologOneLabel;
	std::string prologTwoLabel;
	std::string mainLabel;
	std::string drainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string fallbackThreeLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string colorReg;
	std::string lightDirReg;
	std::string diffProductReg;
	std::string onesReg;
	std::string lightDiffReg;
	std::string localDiffReg;
	std::string materialDiffReg;
	std::string halfAngleReg;
	std::string specProductReg;
	std::string specSwizzleReg;
	std::string specScratchReg;
	std::string specIntensityReg;
	std::string specPowerReg;
	std::string localSpecReg;
	std::string lightAmbReg;
	std::string materialAmbReg;
	std::string litReg;
	std::string resultReg;

	long inputStep;
	long outputStep;
	long normalOffset;
	long materialDiffOffset;
	long colorOffset;
	long storeOffset;
	bool materialDiffFromInput;
	bool postIncrementStore;

	DirLightSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		normalOffset = 0;
		materialDiffOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
		materialDiffFromInput = false;
		postIncrementStore = false;
	}
};

struct CodeGenerator::PtLightNoSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string drainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string vertexReg;
	std::string currentColorReg;
	std::string lightPosReg;
	std::string toLightReg;
	std::string attenReg;
	std::string attenCoeffReg;
	std::string attenProductReg;
	std::string onesReg;
	std::string normalizedLightReg;
	std::string normalProductReg;
	std::string intensityReg;
	std::string clampedIntensityReg;
	std::string lightDiffReg;
	std::string localDiffuseReg;
	std::string materialDiffReg;
	std::string lightAmbReg;
	std::string materialAmbReg;
	std::string litColorReg;
	std::string attenuatedColorReg;
	std::string resultReg;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long colorOffset;
	long storeOffset;

	PtLightNoSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
	}
};

struct CodeGenerator::PtLightSpecLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologOneLabel;
	std::string prologTwoLabel;
	std::string mainLabel;
	std::string drainLabel;
	std::string fallbackOneLabel;
	std::string fallbackTwoLabel;
	std::string fallbackThreeLabel;
	std::string scalarLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string outputReg;
	std::string normalReg;
	std::string vertexReg;
	std::string currentColorReg;
	std::string lightPosReg;
	std::string toLightReg;
	std::string attenReg;
	std::string attenCoeffReg;
	std::string attenProductReg;
	std::string onesReg;
	std::string normalizedLightReg;
	std::string normalProductReg;
	std::string intensityReg;
	std::string clampedIntensityReg;
	std::string lightDiffReg;
	std::string localDiffuseReg;
	std::string materialDiffReg;
	std::string viewDirReg;
	std::string halfAngleReg;
	std::string specProductReg;
	std::string specIntensityReg;
	std::string specPowerReg;
	std::string localSpecReg;
	std::string lightAmbReg;
	std::string materialAmbReg;
	std::string litColorReg;
	std::string attenuatedColorReg;
	std::string resultReg;

	long inputStep;
	long outputStep;
	long vertexOffset;
	long normalOffset;
	long materialDiffOffset;
	long colorOffset;
	long storeOffset;
	bool materialDiffFromInput;
	bool postIncrementStore;

	PtLightSpecLoopPipelinePattern()
	{
		inputStep = 0;
		outputStep = 0;
		vertexOffset = 0;
		normalOffset = 0;
		materialDiffOffset = 0;
		colorOffset = 0;
		storeOffset = 0;
		materialDiffFromInput = false;
		postIncrementStore = false;
	}
};

struct CodeGenerator::FinalColorLoopPipelinePattern
{
	std::string sourceLabel;
	std::string entryLabel;
	std::string prologLabel;
	std::string mainLabel;
	std::string epilogTwoLabel;
	std::string epilogOneLabel;
	std::string exitLabel;

	std::string inputReg;
	std::string lastInputReg;
	std::string colorReg;
	std::string minReg;
	std::string outputReg;
	long inputStep;
	long loadOffset;
	long storeOffsetAfterIncrement;

	FinalColorLoopPipelinePattern()
	{
		inputStep = 0;
		loadOffset = 0;
		storeOffsetAfterIncrement = 0;
		minReg = "VF24";
		outputReg = "VF25";
	}
};

namespace
{
	bool containsKey( const std::list<std::string>& keys, const std::string& key )
	{
		for( std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i )
		{
			if( *i == key )
				return true;
		}
		return false;
	}

	bool intersectsKeys( const std::list<std::string>& a, const std::list<std::string>& b )
	{
		for( std::list<std::string>::const_iterator i = a.begin(); i != a.end(); ++i )
		{
			if( containsKey( b, *i ) )
				return true;
		}
		return false;
	}

	bool evaluateIntegerExpression( const std::string& text, long& value )
	{
		Expression e;
		e.setCustomOperators( Math::mathOperators() );
		if( !e.process( text ) || !e.solve() )
			return false;
		value = static_cast<long>( e.result() );
		return true;
	}

	std::string integerText( long value )
	{
		std::stringstream stream;
		stream << value;
		return stream.str();
	}

	std::string vuInstr( VuInstructionOpcode opcode )
	{
		const char* mnemonic = vuInstructionMnemonic( opcode );
		return mnemonic ? mnemonic : "";
	}

	std::string vuInstr( VuInstructionOpcode opcode, const std::string& arguments )
	{
		return vuInstr( opcode ) + " " + arguments;
	}

	std::string vuInstrFields( VuInstructionOpcode opcode, const std::string& fields, const std::string& arguments )
	{
		return vuInstr( opcode ) + "." + fields + " " + arguments;
	}

	std::string vuInstrPrefix( VuInstructionOpcode opcode )
	{
		return vuInstr( opcode ) + " ";
	}

	std::string vuInstrPrefixFields( VuInstructionOpcode opcode, const char* fields )
	{
		return vuInstr( opcode ) + "." + fields + " ";
	}

	std::string vuInstrBroadcastName( VuInstructionOpcode opcode, const char* broadcast )
	{
		return vuInstr( opcode ) + broadcast;
	}

	std::string vuInstrPrefixBroadcast( VuInstructionOpcode opcode, const char* broadcast )
	{
		return vuInstrBroadcastName( opcode, broadcast ) + " ";
	}

	std::string vuInstrPrefixBroadcastFields( VuInstructionOpcode opcode, const char* broadcast, const char* fields )
	{
		return vuInstrBroadcastName( opcode, broadcast ) + "." + fields + " ";
	}

	std::string vuInstructionFieldText( unsigned int fields )
	{
		static const char* fieldNames = "xyzw";
		std::string text;
		for( unsigned int i = 0; i < 4; ++i )
		{
			if( fields & (1 << i) )
				text += fieldNames[i];
		}
		return text;
	}

	std::string vuInstrBroadcastFields( VuInstructionOpcode opcode,
	                                    const char* broadcast,
	                                    const char* fields,
	                                    const std::string& arguments )
	{
		return vuInstr( opcode ) + broadcast + "." + fields + " " + arguments;
	}

	std::string vuInstr( const VuInstructionForm& form )
	{
		std::string text = vuInstr( form.opcode );
		if( form.broadcast )
			text += vuInstructionFieldText( form.broadcast );

		if( form.flags & (Token::E | Token::D | Token::T) )
		{
			text += "[";
			if( form.flags & Token::E )
				text += "E";
			if( form.flags & Token::D )
				text += "D";
			if( form.flags & Token::T )
				text += "T";
			text += "]";
		}

		if( form.fields )
			text += "." + vuInstructionFieldText( form.fields );
		return text;
	}

	std::string vuInstr( const VuInstructionForm& form, const std::string& arguments )
	{
		return vuInstr( form ) + " " + arguments;
	}

	bool captureVuInstructionForm( const Token& token, VuInstructionForm& form )
	{
		if( !token.operand() )
			return false;

		VuInstructionOpcode opcode = vuInstructionOpcodeForMnemonic( token.operand()->name() );
		if( opcode == VU_OP_INVALID )
			return false;

		form.opcode = opcode;
		form.broadcast = 0;
		if( token.operand()->flags() & Operand::BROADCAST )
			form.broadcast = token.broadcast();
		form.flags = token.flags() & (Token::E | Token::D | Token::T);

		form.fields = token.fields();
		if( !form.fields
		    && token.operand()->unit() == Operand::FMAC
		    && (token.operand()->flags() & Operand::DEST) )
		{
			form.fields = Token::X | Token::Y | Token::Z | Token::W;
		}
		return true;
	}

	bool vuInstrEquals( const std::string& mnemonic, VuInstructionOpcode opcode )
	{
		const char* expected = vuInstructionMnemonic( opcode );
		return expected && mnemonic == expected;
	}

	std::string formatRawPairedInstructionLine( const std::string& upper, const std::string& lower )
	{
		const int instructionLength = 32;
		std::string line;
		for( int d = 0; d < 20; d++ )
			line += " ";
		line += upper;

		int pad = instructionLength - int(upper.length());
		if( pad <= 0 )
			line += " ";
		for( int d = 0; d < pad; d++ )
			line += " ";

		line += lower;
		return line;
	}

	bool tokenIsScheduledPairFirst( const Token& token )
	{
		return (token.flags() & Token::SCHEDULED_PAIR_FIRST) != 0;
	}

	bool tokenIsScheduledPairSecond( const Token& token )
	{
		return (token.flags() & Token::SCHEDULED_PAIR_SECOND) != 0;
	}

	bool integerSelfImmediateAdd( const Token& token, std::string& reg, long& immediate )
	{
		if( !token.operand() || !vuInstrEquals( lowerVuTokenName(token), VU_OP_IADDIU ) )
			return false;
		if( token.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER) )
			return false;

		const std::list<Token::Argument>& args = token.arguments();
		if( args.size() != 3 )
			return false;

		std::list<Token::Argument>::const_iterator dst = args.begin();
		std::list<Token::Argument>::const_iterator src = dst;
		++src;
		std::list<Token::Argument>::const_iterator imm = src;
		++imm;

		if( dst->type() != Token::Argument::INTEGER_REGISTER
		    || src->type() != Token::Argument::INTEGER_REGISTER
		    || imm->type() != Token::Argument::IMMEDIATE )
			return false;
		if( !(dst->flags() & Token::Argument::WRITE) )
			return false;

		std::string srcReg;
		if( !vuRegisterKey(*dst, reg) || !vuRegisterKey(*src, srcReg) || reg != srcReg )
			return false;

		return evaluateIntegerExpression( imm->immediate(), immediate );
	}

	bool adjustPlainStoreOffset( Token& store, const std::string& baseReg, long delta )
	{
		for( std::list<Token::Argument>::iterator i = store.arguments().begin(); i != store.arguments().end(); ++i )
		{
			if( i->type() != Token::Argument::INTEGER_REGISTER
			    || !(i->flags() & Token::Argument::INDIRECT) )
				continue;

			std::string key;
			if( !vuRegisterKey(*i, key) || key != baseReg )
				continue;

			long offset = 0;
			if( !evaluateIntegerExpression( i->immediate(), offset ) )
				return false;

			std::stringstream s;
			s << (offset + delta);
			i->setImmediate( s.str() );
			return true;
		}
		return false;
	}

	std::string fieldArg( const std::string& reg, const char* field )
	{
		return reg + field;
	}

	std::string offsetBase( long offset, const std::string& base )
	{
		std::stringstream s;
		s << offset << "(" << base << ")";
		return s.str();
	}

	void addScratchReservation( std::list<std::string>& reserved, const std::string& reg )
	{
		if( reg.empty() || containsKey(reserved, reg) )
			return;
		reserved.push_back(reg);
	}

	std::string reserveScratchReg( std::list<std::string>& reserved )
	{
		static const char* kScratchRegs[] =
		{
			"VF31", "VF30", "VF29", "VF28", "VF27", "VF26", "VF25", "VF24",
			"VF23", "VF22", "VF21", "VF20", "VF19", "VF18", "VF17", "VF16",
			"VF15", "VF14", "VF13", "VF12", "VF11", "VF10", "VF09", "VF08",
			"VF07", "VF06", "VF05", "VF04", "VF03", "VF02", "VF01"
		};

		for( unsigned int i = 0; i < sizeof(kScratchRegs) / sizeof(kScratchRegs[0]); ++i )
		{
			if( !containsKey(reserved, kScratchRegs[i]) )
			{
				reserved.push_back(kScratchRegs[i]);
				return kScratchRegs[i];
			}
		}

		return "";
	}

	bool tokenHasFields( const Token& token, unsigned int fields )
	{
		return token.fields() == fields;
	}

	bool getArg( const Token& token, unsigned int index, Token::Argument& arg )
	{
		unsigned int current = 0;
		for( std::list<Token::Argument>::const_iterator i = token.arguments().begin();
		     i != token.arguments().end(); ++i, ++current )
		{
			if( current == index )
			{
				arg = *i;
				return true;
			}
		}
		return false;
	}

	bool getRegisterArgKey( const Token& token, unsigned int index, std::string& key )
	{
		Token::Argument arg("");
		return getArg(token, index, arg) && vuRegisterKey(arg, key);
	}

	bool getImmediateArg( const Token& token, unsigned int index, std::string& immediate )
	{
		Token::Argument arg("");
		if( !getArg(token, index, arg) || arg.type() != Token::Argument::IMMEDIATE )
			return false;
		immediate = arg.immediate();
		return true;
	}

	bool getIndirectBaseAndOffset( const Token& token, std::string& base, long& offset )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess(token, access) )
			return false;
		if( !access.hasMemoryBase || !access.hasMemoryOffset )
			return false;
		base = access.memoryBaseRegister;
		offset = access.memoryOffset;
		return true;
	}

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CodeGenerator::CodeGenerator()
{
	m_currentCycle    = 0;
	m_knownLoopOptimizations = false;
	m_genericSoftwarePipelining = false;
	m_strictScheduleSlots = false;
	m_skipBranchPreBubble = false;
	m_enableUpperMoves = false;
	m_ignoredImplicitWawResources = VU_RESOURCE_NONE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CodeGenerator::~CodeGenerator()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::beginProcess(const std::list<Token>& tokens)
{
	// Detect if the source already contains a .vu directive
	bool hasVuDirective = false;
	for( std::list<Token>::const_iterator it = tokens.begin(); it != tokens.end(); ++it )
	{
		if( (*it).operand() && (*it).operand()->isPreprocessor() && (*it).operand()->name() == ".vu" )
		{
			hasVuDirective = true;
			break;
		}
	}

	// When generating named output, always add .vu and .align BEFORE globals and label
	// This matches SCE vcl output format and ensures proper code alignment
	if( m_name.length() > 0 )
	{
		m_codeLines.push_back(std::string("		.vu"));
		m_codeLines.push_back(std::string("		.align 4"));
		m_codeLines.push_back(std::string("		.global	") + m_name + std::string("_CodeStart") );
		m_codeLines.push_back(std::string("		.global	") + m_name + std::string("_CodeEnd") );
		m_codeLines.push_back( std::string(m_name) + "_CodeStart:" );
	}
	else if( !hasVuDirective )
	{
		// No name but source omitted .vu - add directives for dvp-as compatibility
		m_codeLines.push_back(std::string("		.vu"));
		m_codeLines.push_back(std::string("		.align 4"));
	}

	bool exitWritten = true;

	std::list<Token> workTokens = tokens;
	coalesceAdjacentVuIntegerAdds(workTokens);
	bool appliedGenericSoftwarePipeline = false;
	if( !m_knownLoopOptimizations && m_genericSoftwarePipelining )
	{
		const std::vector<VuSoftwarePipelineRewritePlan> pipelinePlans =
			buildVuSoftwarePipelineRewritePlans(workTokens);
		// Track 9.G-1h step 4b-3b-5: dormant read of shadow-placer refit
		// metrics. When OPENVCL_USE_EXPANDED_DDG_PLACER=1 the analysis
		// publishes per-plan refit verdicts (II / stages / conflicts /
		// mainTokenCount) computed by re-running the modulo placer on
		// the expanded-DDG shadow opportunity. Today the bake-in always
		// keeps the original (passthrough) placement; logging only.
		if( std::getenv("OPENVCL_USE_EXPANDED_DDG_PLACER") != NULL )
		{
			for( unsigned int p = 0; p < pipelinePlans.size(); ++p )
			{
				const VuSoftwarePipelineRewritePlan& plan = pipelinePlans[p];
				if( plan.kernelRewriteRefitII == 0u )
					continue;
				// 4b-8c: gate verdict mirrors applyVuGenericKernelRewritePlans.
				const bool gatePass =
				    plan.kernelRewriteRefitII != 0u &&
				    plan.kernelRewriteRefitII < plan.kernelRewriteII;
				std::cerr << "[refit-codegen] loop=" << plan.mainLabel
				          << " refitII=" << plan.kernelRewriteRefitII
				          << " refitStages=" << plan.kernelRewriteRefitStageCount
				          << " refitMain=" << plan.kernelRewriteRefitMainTokenCount
				          << " refitConflicts=" << plan.kernelRewriteRefitConflicts
				          << " origII=" << plan.kernelRewriteII
				          << " origStages=" << plan.kernelRewriteStageCount
				          << " gate=" << ( gatePass ? "publish" : "reject" )
				          << std::endl;
			}
		}
		if( !pipelinePlans.empty() )
		{
			// Track 9.G step 7b: optional SCE-style multi-stage emitter,
			// gated by OPENVCL_USE_GENERIC_KERNEL_REWRITE. Only plans
			// passing isVuPlanEligibleForGenericKernelRewrite are
			// rewritten by the new emitter; ineligible plans remain
			// the responsibility of applyVuSoftwarePipelinePlans below.
			if( std::getenv("OPENVCL_USE_GENERIC_KERNEL_REWRITE") != NULL )
			{
				// 9.G-1h-4a-2: capture MAIN-body ranges out-of-band
				// for the 4a-3 scheduler-bypass consumer.
				std::list<Token> genericRewritten =
					applyVuGenericKernelRewritePlans(workTokens, pipelinePlans, m_kernelBlockRanges);
				workTokens.swap(genericRewritten);
			}
			std::list<Token> pipelinedTokens =
				applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance(workTokens);
			workTokens.swap(pipelinedTokens);
			appliedGenericSoftwarePipeline = true;
		}
	}
	if( !appliedGenericSoftwarePipeline )
		fillPreIncrementStoreBranchDelaySlots(workTokens);
	// An upper move is emitted as an FMAC (maxx), which produces flags a plain move
	// does not - so it is only free when nothing reads them. The status register is
	// as much "them" as MAC is, and the two used to be one test only because
	// isVuMacReader() listed the fs* readers.
	const bool macFlagsDead = !vuTokenListReadsMac(workTokens)
	                       && !vuTokenListReadsStatus(workTokens);
	m_enableUpperMoves = macFlagsDead;
	m_ignoredImplicitWawResources = VU_RESOURCE_NONE;

	const bool emitTypedScheduleDirectly = m_strictScheduleSlots || !m_knownLoopOptimizations;
	if( emitTypedScheduleDirectly )
	{
		if( !appliedGenericSoftwarePipeline )
		{
			fillBranchDelaySlots(workTokens);
			fillDeadFallthroughBranchDelaySlots(workTokens);
		}
		if( !emitStrictScheduledProgram(workTokens, exitWritten) )
			return false;
	}
	else
	{
		std::list<Token> scheduledTokens = scheduleVuTokensReadySetWithFlagLiveness(workTokens);
		workTokens.swap(scheduledTokens);
		fillBranchDelaySlots(workTokens);
		fillDeadFallthroughBranchDelaySlots(workTokens);

		for( std::list<Token>::iterator k = workTokens.begin(); k != workTokens.end(); )
		{
			m_ignoredImplicitWawResources = ignoredImplicitWawResourcesForRemaining(workTokens.begin(), k, workTokens.end());

		if( tryEmitKnownLoopOptimization(workTokens, k) )
		{
			exitWritten = false;
			continue;
		}

		Token token = *k;

		//handle label
		if(token.label().length()>0)
			m_codeLines.push_back(token.label() + ":");

		if(!token.operand())
		{
			++k;
			continue;
		}

		if( ((token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER)) && !exitWritten )
		{
			m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP) + "[E]", vuInstr(VU_OP_NOP)));
			m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP)));
			exitWritten = true;
		}

		if(token.flags()&Token::IGNORED)
		{
			++k;
			continue;
		}

		if(!(token.flags()&Token::PROCESSED) && !(token.operand()->flags()&Operand::PREPROCESSOR) )
		{
			++k;
			continue;
		}

		//handle alignment
		if(token.operand()->flags()&Operand::PREPROCESSOR)
		{
			if(token.operand()->name() == ".vu")
			{
				// .vu and alignment are already handled at the start of beginProcess()
				// when m_name is set, so skip adding redundant directives here
				++k;
				continue;
			}
			else if( token.operand()->name() == "--cont" )
			{
				m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP) + "[E]", vuInstr(VU_OP_NOP)));
				m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP)));
				exitWritten = true;
				++k;
				continue;
			}
			else if( token.operand()->name() == "--barrier" )
			{
				++k;
				continue;
			}
		}
		else exitWritten = false;

		if( (token.operand()->flags()&Operand::FILTERED) )
		{
			++k;
			continue;
		}

		if( token.flags() & Token::BRANCH_DELAY_FILLER )
		{
			++k;
			continue;
		}

		if( !m_strictScheduleSlots && token.label().length() == 0 && readHazardDelay(token, NULL) > 0 )
		{
			enum { FillerLookaheadLimit = 96 };
			const bool waitsForQ = vuTokenReadsQ(token);
			const bool waitsForP = vuTokenReadsP(token);
			const int qGap = waitsForQ ? (m_latencyTracker.qReadyCycle() - m_currentCycle) : 0;
			const int pGap = waitsForP ? (m_latencyTracker.pReadyCycle() - m_currentCycle) : 0;
			if( tokenIsLowerExecutionPath(token) && (qGap > 1 || pGap > 1) )
			{
				const bool waitQ = qGap >= pGap;
				const int waitGap = waitQ ? qGap : pGap;
				std::list<Token>::iterator waitFiller = workTokens.end();
				std::list<Token>::iterator p = k;
				++p;
				unsigned int lookahead = 0;
				for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
				{
					if( (*p).label().length() != 0 )
						break;
					if( !isVuEmittableInstruction(*p) )
						break;
					if( !tokenIsUpperExecutionPath(*p) )
						continue;

					bool canCross = true;
					std::list<Token>::iterator c = k;
					for( ; c != p; ++c )
					{
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(*p, NULL) <= waitGap )
					{
						waitFiller = p;
						break;
					}
				}

				if( waitFiller != workTokens.end() )
				{
					emitUpperWithWait(*waitFiller, waitQ);
					workTokens.erase(waitFiller);
					continue;
				}
			}

			std::list<Token>::iterator filler = workTokens.end();
			std::list<Token>::iterator p = k;
			++p;
			unsigned int lookahead = 0;
			for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
			{
				if( (*p).label().length() != 0 )
					break;
				if( !isVuEmittableInstruction(*p) )
					break;
				if( !vuTokenRangeCanBeCrossed(*k, *p)
				    && !isVuPlainMemoryStore(*p)
				    && !vuTokenCanMoveBefore(*p, *k, m_ignoredImplicitWawResources) )
					continue;

				bool canCross = true;
				std::list<Token>::iterator c = k;
				for( ; c != p; ++c )
				{
					if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
					{
						canCross = false;
						break;
					}
				}
				if( canCross && readHazardDelay(*p, NULL) <= 0 )
				{
					filler = p;
					break;
				}
			}

			if( filler != workTokens.end() )
			{
				std::list<Token>::iterator fillerPartner = workTokens.end();
				p = filler;
				++p;
				lookahead = 0;
				for( ; p != workTokens.end() && lookahead < FillerLookaheadLimit; ++p, ++lookahead )
				{
					if( (*p).label().length() != 0 )
						break;
					if( !isVuEmittableInstruction(*p) )
						break;
					if( !vuTokenRangeCanBeCrossed(*k, *p)
					    && !isVuPlainMemoryStore(*p)
					    && !vuTokenCanMoveBefore(*p, *k, m_ignoredImplicitWawResources) )
						continue;
					if( !tokensCanPair(*filler, *p) )
						continue;

					bool canCross = true;
					std::list<Token>::iterator c = k;
					for( ; c != p; ++c )
					{
						if( c == filler )
							continue;
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(*filler, &*p) <= 0 )
					{
						fillerPartner = p;
						break;
					}
				}

				if( fillerPartner != workTokens.end() )
				{
					emitPairedTokens(*filler, *fillerPartner);
					workTokens.erase(fillerPartner);
				}
				else
					emitSingleToken(*filler);

				workTokens.erase(filler);
				continue;
			}
		}

		if( tokenIsScheduledPairFirst( token ) )
		{
			std::list<Token>::iterator scheduledPartner = k;
			++scheduledPartner;
			if( scheduledPartner != workTokens.end()
			    && tokenIsScheduledPairSecond( *scheduledPartner )
			    && tokensCanPair( token, *scheduledPartner )
			    && readHazardDelay( token, &*scheduledPartner ) <= readHazardDelay( token, NULL ) )
			{
				padForReadHazards( token, &*scheduledPartner );
				std::list<Token>::iterator branchDelayFiller = scheduledPartner;
				++branchDelayFiller;
				if( vuTokenBranchDelaySlots( *scheduledPartner ) == 1
				    && branchDelayFiller != workTokens.end()
				    && (branchDelayFiller->flags() & Token::BRANCH_DELAY_FILLER) )
				{
					while( readHazardDelay( *branchDelayFiller, NULL ) > 1 )
					{
						addNopLine();
						m_currentCycle++;
					}
					emitPairedBranchWithDelayFiller( token, *scheduledPartner, *branchDelayFiller );
					workTokens.erase( branchDelayFiller );
				}
				else
					emitPairedTokens( token, *scheduledPartner );
				if( isVuTerminalUnconditionalBranch(token)
				    || isVuTerminalUnconditionalBranch(*scheduledPartner) )
					exitWritten = true;
				workTokens.erase( scheduledPartner );
				++k;
				continue;
			}
		}

		if( vuTokenBranchDelaySlots(token) == 1 && nextTokenIsBranchDelayFiller(k, workTokens.end()) )
		{
			std::list<Token>::iterator filler = k;
			++filler;
			padForReadHazards(token, NULL);
			while( readHazardDelay(*filler, NULL) > 1 )
			{
				addNopLine();
				m_currentCycle++;
			}
			emitBranchWithDelayFiller(token, *filler);
			if( isVuTerminalUnconditionalBranch(token) )
				exitWritten = true;
			workTokens.erase(filler);
			++k;
			continue;
		}

		// --- DUAL-PIPE PAIRING ---
		// Look ahead within the current straight-line instruction run for
		// an opposite-pipe partner.  The first implementation only paired
		// adjacent instructions, which left many upper/lower opportunities
		// unused once latency padding had made source order sparse.  This
		// pass may pull a later partner into the current cycle, but only if
		// every crossed instruction has no register/resource conflict and
		// no memory/control side effect that would make reordering risky.
		if( !m_strictScheduleSlots )
		{
			enum { PairLookaheadLimit = 96 };
			std::list<Token>::iterator p = k;
			++p;
			bool foundPartner = false;
			const Token* partner = NULL;
			std::list<Token>::iterator partnerIt = workTokens.end();
			const int tokenDelay = readHazardDelay(token, NULL);
			unsigned int lookahead = 0;
			for( ; p != workTokens.end() && lookahead < PairLookaheadLimit; ++p, ++lookahead )
			{
				if( (*p).label().length() != 0 )
					break;
				if( !isVuEmittableInstruction(*p) )
					break;
				if( vuTokenBranchDelaySlots(*p) > 0 && nextTokenIsBranchDelayFiller(p, workTokens.end()) )
					break;
				const bool adjacentCandidate = (lookahead == 0);
				const bool adjacentQpProducerPair =
				    adjacentCandidate
				    && (token.operand()->unit() == Operand::FDIV
				        || token.operand()->unit() == Operand::EFU
				        || (*p).operand()->unit() == Operand::FDIV
				        || (*p).operand()->unit() == Operand::EFU);
				const bool adjacentPlainStorePair =
				    adjacentCandidate
				    && (isVuPlainMemoryStore(token) || isVuPlainMemoryStore(*p));
				const bool adjacentXgkickPair =
				    adjacentCandidate
				    && tokenIsUpperExecutionPath(token)
				    && isVuXgkick(*p);
				const bool adjacentBranchPair =
				    adjacentCandidate
				    && tokenIsUpperExecutionPath(token)
				    && vuTokenBranchDelaySlots(*p) > 0;
				if( !adjacentQpProducerPair && !adjacentPlainStorePair && !adjacentXgkickPair
				    && !adjacentBranchPair
				    && !vuTokenRangeCanBeCrossed(*k, *p)
				    && !isVuPlainMemoryStore(*p)
				    && !vuTokenCanMoveBefore(*p, token, m_ignoredImplicitWawResources) )
					continue;
				if( tokensCanPair(token, *p) )
				{
					bool canCross = true;
					std::list<Token>::iterator c = k;
					++c;
					for( ; c != p; ++c )
					{
						if( !vuTokenCanMoveBefore(*p, *c, m_ignoredImplicitWawResources) )
						{
							canCross = false;
							break;
						}
					}
					if( canCross && readHazardDelay(token, &*p) <= tokenDelay )
					{
						foundPartner = true;
						partner = &*p;
						partnerIt = p;
						break;
					}
				}
			}

			if( !foundPartner && vuTokenReadsQ(token) && tokenIsUpperExecutionPath(token) )
			{
				const int qGap = m_latencyTracker.qReadyCycle() - m_currentCycle;
				const int needed = readHazardDelay(token, NULL);
				if( qGap > 1 && qGap >= needed )
				{
					emitUpperWithWait(token, true);
					++k;
					continue;
				}
			}

			padForReadHazards(token, partner);

			if( foundPartner )
			{
				emitPairedTokens(token, *partner);
				if( isVuTerminalUnconditionalBranch(token) || isVuTerminalUnconditionalBranch(*partner) )
					exitWritten = true;
				workTokens.erase(partnerIt);
				++k;
				continue;
			}
		}

		if( m_strictScheduleSlots )
			padForReadHazards(token, NULL);
		emitSingleToken(token);
		if( isVuTerminalUnconditionalBranch(token) )
			exitWritten = true;
		++k;
	}
	}

	if( !exitWritten )
	{
		// Auto-terminate with an exit pair if the last instruction wasn't closed
		m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP) + "[E]", vuInstr(VU_OP_NOP)));
		m_codeLines.push_back( formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP) ) );
		// Do not treat as an error; some VCL programs end in a loop
	}

	// Last, because it needs the whole program: a forward branch that shortens the
	// distance to a CLIP reader is not in the emitter's hands until the row it
	// jumps to has been emitted.
	padClipFlagWindowAcrossPaths();
	// And after it, for the same reason and on the same graph: a branch entered
	// through a label is one row from a producer sitting in the delay slot of the
	// jump that got there, and nothing before this point has looked.
	padBranchConditionAcrossPaths();

	if( m_name.length() > 0 )
	{
		// Pad to 16-byte (qword) alignment before _CodeEnd.  Each emitted
		// VU instruction is 8 bytes; without this Sony-emulating `.align 4`
		// the _CodeEnd symbol can land at an 8-mod-16 offset (e.g. 0x9B8).
		// ps2gl's renderer-load path computes the MPG size from
		// _CodeEnd - _CodeStart and feeds `MicrocodePacketSize / 8`
		// instructions through VIF MPG in chunks of up to 256.  When the
		// total is odd, the final chunk's qword count (sendSize64 / 2)
		// truncates by one instruction and the VIF NUM field
		// (sendSize64 & 0xff) disagrees with the actual bytes in the Ref
		// transfer — VIF then pulls bytes from the *next* command into
		// the VU instruction stream, corrupting whatever follows.
		// Symptom in ps2gl/box.elf: general_quad's xgkick never reached
		// or its GIF chain malformed, cube invisible.  Sony's vcl emits
		// `.align 4` for exactly this reason.
		m_codeLines.push_back(std::string("		.align 4"));
		m_codeLines.push_back( std::string(m_name) + "_CodeEnd:" );
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::retractLastEmittedRow()
{
	if( m_codeLines.empty() )
		return false;

	// Only a plain instruction row may be taken back: a label, a directive or a
	// source comment (emitSource) is not ours to move.
	const std::string& last = m_codeLines.back();
	if( last.empty() || last[0] != ' ' )
		return false;

	m_codeLines.pop_back();
	return true;
}

void CodeGenerator::addNopLine()
{
	m_codeLines.push_back( formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP) ) );
}

void CodeGenerator::emitWaitQ()
{
	const int nextCycle = m_currentCycle + 1;
	m_codeLines.push_back( formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ) ) );
	const int qReadyCycle = m_latencyTracker.qReadyCycle();
	m_currentCycle = (qReadyCycle > nextCycle) ? qReadyCycle : nextCycle;
}

void CodeGenerator::emitWaitP()
{
	const int nextCycle = m_currentCycle + 1;
	m_codeLines.push_back( formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITP) ) );
	const int pReadyCycle = m_latencyTracker.pReadyCycle();
	m_currentCycle = (pReadyCycle > nextCycle) ? pReadyCycle : nextCycle;
}

void CodeGenerator::emitUpperWithWait( const Token& token, bool waitQ )
{
	const int instructionLength = 32;
	std::string instruction = generateInstruction(token);
	std::string outputLine;
	for(int d = 0; d < 20; d++)
		outputLine += " ";
	outputLine += instruction;
	int pad = instructionLength - int(instruction.length());
	if( pad <= 0 )
		outputLine += " ";
	for(int d = 0; d < pad; d++)
		outputLine += " ";
	outputLine += waitQ ? vuInstr(VU_OP_WAITQ) : vuInstr(VU_OP_WAITP);

	const int readyCycle = waitQ ? m_latencyTracker.qReadyCycle() : m_latencyTracker.pReadyCycle();
	const int issueCycle = (readyCycle > m_currentCycle) ? readyCycle : m_currentCycle;
	m_codeLines.push_back(outputLine);
	recordRegisterWrites(token, issueCycle);
	m_currentCycle = issueCycle + 1;
}

bool CodeGenerator::branchNeedsPreBubble( const Token& token ) const
{
	if( vuTokenBranchDelaySlots( token ) == 0 )
		return false;

	VuTokenResourceAccess access;
	if( !buildVuTokenResourceAccess( token, access ) )
		return true;

	if( !(access.instructionFlags & VU_INSTR_BRANCH) )
		return false;

	if( access.instructionFlags & VU_INSTR_REGISTER_BRANCH )
		return true;

	return !(access.instructionFlags & VU_INSTR_UNCONDITIONAL_BRANCH);
}

// Would --emit-delay-fillers move this slot's instruction into `branch`'s delay
// slot? Same conditions the emitter applies later, so the bubble decision above and
// the move below cannot disagree.
bool CodeGenerator::slotCanBecomeBranchDelayFiller( const VuScheduledIssueSlot& slot,
                                                    const Token& branch ) const
{
	if( !vuEmitDelayFillersEnabled() || slot.padding )
		return false;
	if( !slot.firstToken || slot.secondToken || slot.firstToken == &branch )
		return false;
	return canMoveIntoBranchDelaySlot( *slot.firstToken, branch );
}

// Does either of the two slots above `index` write a register `branch` reads?
// i-1 is the row that creates the hazard; i-2 is checked as well because
// --emit-delay-fillers can move the i-1 row into the delay slot, which puts i-2
// directly above the branch. Padding slots produce nothing, so they answer no.
bool CodeGenerator::scheduledSlotsFeedBranch(
    const std::vector<const VuScheduledIssueSlot*>& slots,
    unsigned int index,
    const Token& branch ) const
{
	std::list<std::string> reads;
	collectVuRegisterReadKeys( branch, reads );
	if( reads.empty() )
		return false;

	for( unsigned int back = 1; back <= 2 && back <= index; ++back )
	{
		const VuScheduledIssueSlot& slot = *slots[index - back];
		if( slot.padding )
			continue;
		// The row two up only becomes adjacent if the one above it is about to be
		// retracted into the delay slot; otherwise it is already far enough away.
		if( back == 2 && !slotCanBecomeBranchDelayFiller( *slots[index - 1], branch ) )
			continue;
		const Token* tokens[2] = { slot.firstToken, slot.secondToken };
		for( int t = 0; t < 2; ++t )
		{
			if( !tokens[t] || tokens[t] == &branch )
				continue;
			// Interlocked producers do not need the word - see --branch-interlock.
			if( vuBranchInterlockEnabled()
			    && ( isVuPlainMemoryLoad( *tokens[t] )
			         || isVuClipReader( lowerVuTokenName( *tokens[t] ) )
			         || isVuMacOrStatusReader( lowerVuTokenName( *tokens[t] ) ) ) )
				continue;
			std::list<std::string> writes;
			collectVuRegisterWriteKeys( *tokens[t], writes );
			for( std::list<std::string>::const_iterator w = writes.begin(); w != writes.end(); ++w )
			{
				for( std::list<std::string>::const_iterator r = reads.begin(); r != reads.end(); ++r )
				{
					if( *w == *r )
						return true;
				}
			}
		}
	}
	return false;
}

void CodeGenerator::padForBranchPreBubble( const Token& token )
{
	if( !branchNeedsPreBubble( token ) )
		return;

	// branchNeedsPreBubble() answers "is this the kind of branch that can need a
	// bubble", never "does THIS one". The strict emitter decides that per branch
	// from the slots above it and parks the answer here - see
	// --branch-bubble-on-dependency. Do NOT ask the latency tracker: recordWrites()
	// ignores anything with latency <= 1, so it has never heard of this hazard and
	// would happily drop every bubble.
	if( m_skipBranchPreBubble )
	{
		m_skipBranchPreBubble = false;
		return;
	}

	if( m_codeLines.empty()
	    || m_codeLines.back() != formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP) ) )
	{
		addNopLine();
		m_currentCycle++;
	}
}

// What one line of m_codeLines IS. Two callers ask, and they must not be allowed to
// disagree about what a word is: emittedRowsSinceClipWrite() measures a distance back
// to a CLIP in words, emittedWordCount() measures the whole program in the same unit.
// Only an instruction row occupies micro memory - blank lines, `.directives`,
// `; source` comments and `label:` lines do not.
namespace
{
	enum EmittedLineKind
	{
		EMITTED_NOTHING,
		EMITTED_LABEL,
		EMITTED_ROW
	};

	EmittedLineKind classifyEmittedLine( const std::string& line )
	{
		const std::string::size_type first = line.find_first_not_of( " \t" );
		if( first == std::string::npos )
			return EMITTED_NOTHING;
		if( line[first] == ';' || line[first] == '.' )
			return EMITTED_NOTHING;
		if( line[line.size() - 1] == ':' )
			return EMITTED_LABEL;
		return EMITTED_ROW;
	}
}

unsigned int CodeGenerator::emittedRowsSinceClipWrite() const
{
	// How many instruction rows have been EMITTED since the row holding a CLIP.
	//
	// The scheduler already keeps its distance in cycles (VuLatencyTracker), and that
	// is not the same measurement: a wait the hardware interlocks is a cycle that
	// costs no word, so a four-cycle separation can come out as two emitted rows. The
	// CLIP flag window is counted in instructions - six bits pushed in per CLIP - so
	// this has to be checked where the words actually are.
	//
	// Read backwards over m_codeLines, skipping what is not an instruction row
	// (labels, directives, source comments). A label is a barrier: the reader below it
	// may be entered from somewhere else entirely, so the distance is unknown and
	// padding it would be guesswork.
	unsigned int rows = 0;
	for( std::list<std::string>::const_reverse_iterator i = m_codeLines.rbegin();
	     i != m_codeLines.rend(); ++i )
	{
		const std::string& line = *i;
		const EmittedLineKind kind = classifyEmittedLine( line );
		if( kind == EMITTED_NOTHING )
			continue;
		if( kind == EMITTED_LABEL )
			break;
		// The mnemonic comes from the opcode metadata, never a literal - there is a
		// unit test that enforces exactly that (test_codegen_instruction_literals).
		if( line.find( vuInstr( VU_OP_CLIP ) ) != std::string::npos )
			return rows;
		++rows;
		if( rows > 32 )
			break;
	}
	return 0xFFFFu;
}

unsigned int CodeGenerator::emittedWordCount() const
{
	// The size the emitted program occupies in micro memory, counted the way nm
	// counts it in the size gate: (CodeEnd - CodeStart) / 8 is one word per
	// instruction row, and the trailing `.align 4` rounds an odd count up to an even
	// upload.
	//
	// Deliberately the finished emission and not the scheduler's own estimate,
	// vuScheduledProgramEmittedWordCount(): after the scheduler is done the emitter
	// still retracts rows into branch delay slots, pads clip windows and adds the [E]
	// pair, so the two numbers do not have to agree, and only this one is the number
	// the assembler will produce.
	unsigned int rows = 0;
	for( std::list<std::string>::const_iterator i = m_codeLines.begin();
	     i != m_codeLines.end(); ++i )
	{
		if( classifyEmittedLine( *i ) == EMITTED_ROW )
			++rows;
	}
	return rows + ( rows & 1u );
}

namespace
{
	// Every identifier on an emitted row, lower-cased and cut at its field
	// selector, in order. `clipw.xyz VF01, VF01` yields clipw, vf01, vf01.
	//
	// Whole tokens, not a substring search. emittedRowsSinceClipWrite() below asks
	// whether the CLIP mnemonic appears anywhere in the line, which also says yes to
	// a branch whose TARGET LABEL happens to start with the same four letters - a
	// jump to Lclipcarry3 reads as a CLIP push. None of this engine's programs has
	// such a label, so that is a latent fragility rather than a live bug, but a pass
	// that computes distances from those rows cannot afford it.
	void emittedRowTokens( const std::string& line, std::vector<std::string>& out )
	{
		std::string token;
		for( std::string::size_type i = 0; i <= line.size(); ++i )
		{
			const char c = ( i < line.size() ) ? line[i] : ' ';
			if( isalnum( (unsigned char)c ) || c == '_' || c == '.' )
			{
				token += (char)tolower( (unsigned char)c );
				continue;
			}
			if( !token.empty() )
			{
				const std::string::size_type dot = token.find( '.' );
				out.push_back( dot == std::string::npos ? token : token.substr( 0, dot ) );
				token.clear();
			}
		}
	}

	bool rowHas( const std::vector<std::string>& tokens, const std::string& mnemonic )
	{
		if( mnemonic.empty() )
			return false;
		for( unsigned int i = 0; i < tokens.size(); ++i )
		{
			if( tokens[i] == mnemonic )
				return true;
		}
		return false;
	}

	std::string emittedLabelName( const std::string& line )
	{
		std::string::size_type first = line.find_first_not_of( " \t" );
		std::string::size_type last = line.find_last_of( ':' );
		if( first == std::string::npos || last == std::string::npos || last <= first )
			return std::string();
		std::string name = line.substr( first, last - first );
		while( !name.empty() && ( name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t' ) )
			name.erase( name.size() - 1 );
		for( unsigned int i = 0; i < name.size(); ++i )
			name[i] = (char)tolower( (unsigned char)name[i] );
		return name;
	}

	enum { BR_NONE = 0, BR_COND, BR_UNCOND, BR_INDIRECT };

	bool isIntegerRegisterToken( const std::string& token )
	{
		if( token.size() < 3 || token.compare( 0, 2, "vi" ) != 0 )
			return false;
		for( std::string::size_type i = 2; i < token.size(); ++i )
		{
			if( !isdigit( (unsigned char)token[i] ) )
				return false;
		}
		return token != "vi00";
	}

	// The condition registers of the conditional branch on this row, taken by
	// OPERAND COUNT rather than "every VIxx after the mnemonic": ibeq/ibne test
	// two, the four sign tests one, and the token after those is the label.
	void branchConditionRegisters( const std::vector<std::string>& tokens,
	                               const VuInstructionOpcode* conditional,
	                               unsigned int conditionalCount,
	                               std::list<std::string>& out )
	{
		for( unsigned int t = 0; t < tokens.size(); ++t )
		{
			for( unsigned int k = 0; k < conditionalCount; ++k )
			{
				if( tokens[t] != vuInstr( conditional[k] ) )
					continue;
				const bool twoOperands = conditional[k] == VU_OP_IBEQ
				                      || conditional[k] == VU_OP_IBNE;
				const unsigned int want = twoOperands ? 2u : 1u;
				unsigned int taken = 0;
				for( unsigned int u = t + 1; u < tokens.size() && taken < want; ++u )
				{
					if( tokens[u].size() >= 3 && tokens[u].compare( 0, 2, "vi" ) == 0 )
					{
						++taken;
						if( isIntegerRegisterToken( tokens[u] ) )
							out.push_back( tokens[u] );
					}
				}
			}
		}
	}

	// Does this row write `reg` with an integer ARITHMETIC op? The destination of
	// every opcode in `arith` is its first operand, so it is the token right after
	// the mnemonic.
	bool rowWritesIntegerRegister( const std::vector<std::string>& tokens,
	                               const VuInstructionOpcode* arith,
	                               unsigned int arithCount,
	                               const std::string& reg )
	{
		for( unsigned int t = 0; t + 1 < tokens.size(); ++t )
		{
			for( unsigned int k = 0; k < arithCount; ++k )
			{
				if( tokens[t] == vuInstr( arith[k] ) && tokens[t + 1] == reg )
					return true;
			}
		}
		return false;
	}

	// The emitted program as a control-flow graph over its instruction ROWS.
	// Shared by the two cross-path padding passes below, because both need the
	// same graph and the same rule about where a branch fans out - and a second
	// copy of that rule is a second place for it to be wrong.
	struct EmittedRowGraph
	{
		std::vector< std::list<std::string>::iterator > rowIt;
		std::vector< std::vector<std::string> > rowTokens;
		std::vector<int> branchKind;
		std::vector<unsigned int> branchTarget;
		std::vector< std::vector<unsigned int> > successors;
		std::vector<char> labelled;
		unsigned int rowCount;
	};

	void buildEmittedRowGraph( std::list<std::string>& codeLines, EmittedRowGraph& g )
	{
		std::map<std::string, unsigned int> labels;
		for( std::list<std::string>::iterator i = codeLines.begin(); i != codeLines.end(); ++i )
		{
			const EmittedLineKind kind = classifyEmittedLine( *i );
			if( kind == EMITTED_NOTHING )
				continue;
			if( kind == EMITTED_LABEL )
			{
				const std::string name = emittedLabelName( *i );
				if( !name.empty() && labels.find( name ) == labels.end() )
					labels[name] = (unsigned int)g.rowIt.size();
				continue;
			}
			std::vector<std::string> tokens;
			emittedRowTokens( *i, tokens );
			g.rowIt.push_back( i );
			g.rowTokens.push_back( tokens );
		}

		g.rowCount = (unsigned int)g.rowIt.size();
		g.branchKind.assign( g.rowCount, BR_NONE );
		g.branchTarget.assign( g.rowCount, g.rowCount );
		g.successors.assign( g.rowCount, std::vector<unsigned int>() );
		g.labelled.assign( g.rowCount, 0 );
		if( g.rowCount == 0 )
			return;

		const VuInstructionOpcode conditional[6] = { VU_OP_IBEQ, VU_OP_IBGEZ, VU_OP_IBGTZ,
		                                             VU_OP_IBLEZ, VU_OP_IBLTZ, VU_OP_IBNE };
		for( unsigned int i = 0; i < g.rowCount; ++i )
		{
			const std::vector<std::string>& tokens = g.rowTokens[i];
			if( rowHas( tokens, vuInstr( VU_OP_JR ) ) || rowHas( tokens, vuInstr( VU_OP_JALR ) ) )
				g.branchKind[i] = BR_INDIRECT;
			else if( rowHas( tokens, vuInstr( VU_OP_B ) ) || rowHas( tokens, vuInstr( VU_OP_BAL ) ) )
				g.branchKind[i] = BR_UNCOND;
			else
			{
				for( int k = 0; k < 6; ++k )
				{
					if( rowHas( tokens, vuInstr( conditional[k] ) ) )
					{
						g.branchKind[i] = BR_COND;
						break;
					}
				}
			}
			if( g.branchKind[i] == BR_COND || g.branchKind[i] == BR_UNCOND )
			{
				// The target is the last token on the row that names a label.
				for( unsigned int t = (unsigned int)tokens.size(); t > 0; --t )
				{
					std::map<std::string, unsigned int>::const_iterator found =
						labels.find( tokens[t - 1] );
					if( found != labels.end() )
					{
						g.branchTarget[i] = found->second;
						break;
					}
				}
			}
		}

		// A delay slot executes whichever way its branch goes, so the row after a
		// branch fans out, not the branch row itself.
		for( unsigned int i = 0; i < g.rowCount; ++i )
		{
			if( g.branchKind[i] == BR_NONE )
			{
				if( i + 1 < g.rowCount )
					g.successors[i].push_back( i + 1 );
				continue;
			}
			if( g.branchKind[i] == BR_INDIRECT || i + 1 >= g.rowCount )
				continue;
			g.successors[i].push_back( i + 1 );
			const unsigned int slot = i + 1;
			if( g.branchTarget[i] < g.rowCount )
				g.successors[slot].push_back( g.branchTarget[i] );
			if( g.branchKind[i] == BR_COND && slot + 1 < g.rowCount )
				g.successors[slot].push_back( slot + 1 );
		}

		for( std::map<std::string, unsigned int>::const_iterator i = labels.begin(); i != labels.end(); ++i )
		{
			if( i->second < g.rowCount )
				g.labelled[i->second] = 1;
		}
	}
}

// The CLIP window is a distance along a PATH, and the emitter measures a file.
//
// padForClipFlagWindow() below walks the rows already emitted backwards and stops
// at a label, which is the right answer to the wrong question. A reader four rows
// under its push on the page can be one row under it along a taken branch - a
// forward branch that jumps over the rows between them shortens the distance
// without moving a word - and a reader entered through a label is exactly the case
// the backward walk declares unknowable and then does nothing about.
//
// So this runs once, over the finished program, on its own control-flow graph:
// for every CLIP reader, the fewest rows any path executes between a push and that
// reader, and NOPs above the reader until even the shortest path clears the window.
// The pads go BELOW the reader's label, so a branch that enters there passes
// through them too - which is what makes padding a labelled reader meaningful at
// all.
//
// ROWS, NOT CYCLES, AND THAT IS A DELIBERATE OVER-APPROXIMATION. Every row costs
// at least one cycle, so three rows of separation guarantee the four cycles the
// window needs; the converse is false, and Sony's vcl lives in the gap - measured
// over these 70 programs it puts 125 readers closer than three ROWS and is right
// every time, because it counts the cycles the hardware will stall and writes them
// down (STALL_LATENCY ?N). Matching that would mean an interlock model deciding
// what gets emitted, and the one instrument in this work that has such a model
// needed a fit to Sony's own annotations to reach 99.88%. A sufficient condition
// costs words nobody needs; a fitted one costs correctness when the fit is wrong.
void CodeGenerator::padClipFlagWindowAcrossPaths()
{
	const unsigned int latency = vuClipFlagVisibilityLatency();
	if( latency <= 1 )
		return;
	// Each insertion can only lengthen distances, never shorten one, so this
	// terminates; the cap is a guard against a bug in that reasoning, not a
	// budget.
	for( unsigned int guard = 0; guard < 4096; ++guard )
	{
		if( !insertOneCrossPathClipPad( latency ) )
			return;
	}
}

bool CodeGenerator::insertOneCrossPathClipPad( unsigned int latency )
{
	EmittedRowGraph g;
	buildEmittedRowGraph( m_codeLines, g );
	const unsigned int rowCount = g.rowCount;
	if( rowCount == 0 )
		return false;

	const std::vector< std::list<std::string>::iterator >& rowIt = g.rowIt;
	const std::vector< std::vector<std::string> >& rowTokens = g.rowTokens;
	const std::vector<int>& branchKind = g.branchKind;
	const std::vector< std::vector<unsigned int> >& successors = g.successors;
	const std::vector<char>& labelled = g.labelled;

	const VuInstructionOpcode pushes[3] = { VU_OP_CLIP, VU_OP_CLIPW, VU_OP_CLIPLW };
	const VuInstructionOpcode readers[4] = { VU_OP_FCAND, VU_OP_FCOR, VU_OP_FCEQ, VU_OP_FCGET };

	std::vector<char> isPush( rowCount, 0 );
	std::vector<char> isReader( rowCount, 0 );
	for( unsigned int i = 0; i < rowCount; ++i )
	{
		const std::vector<std::string>& tokens = rowTokens[i];
		for( int k = 0; k < 3; ++k )
		{
			if( rowHas( tokens, vuInstr( pushes[k] ) ) )
				isPush[i] = 1;
		}
		for( int k = 0; k < 4; ++k )
		{
			if( rowHas( tokens, vuInstr( readers[k] ) ) )
				isReader[i] = 1;
		}
	}

	// distance[i] = fewest rows executed strictly between a push and row i.
	const unsigned int UNREACHED = 0xFFFFu;
	std::vector<unsigned int> distance( rowCount, UNREACHED );
	std::list<unsigned int> work;
	for( unsigned int i = 0; i < rowCount; ++i )
	{
		if( !isPush[i] )
			continue;
		for( unsigned int s = 0; s < successors[i].size(); ++s )
		{
			const unsigned int next = successors[i][s];
			if( distance[next] != 0 )
			{
				distance[next] = 0;
				work.push_back( next );
			}
		}
	}
	while( !work.empty() )
	{
		const unsigned int i = work.front();
		work.pop_front();
		if( isPush[i] )
			continue;
		for( unsigned int s = 0; s < successors[i].size(); ++s )
		{
			const unsigned int next = successors[i][s];
			if( distance[i] + 1 < distance[next] )
			{
				distance[next] = distance[i] + 1;
				work.push_back( next );
			}
		}
	}

	// `labelled` comes from the shared graph: the backward walk below stops at a
	// label exactly where emittedRowsSinceClipWrite() does.
	for( unsigned int i = 0; i < rowCount; ++i )
	{
		if( !isReader[i] || isPush[i] )
			continue;
		if( distance[i] == UNREACHED || distance[i] + 1 >= latency )
			continue;
		// A delay slot cannot be pushed away from the branch it belongs to; the
		// scheduler owns that pairing, and no NOP may come between them.
		if( i > 0 && ( branchKind[i - 1] == BR_COND || branchKind[i - 1] == BR_UNCOND ) )
			continue;

		// ONLY WHERE THE PAGE AND THE PATH DISAGREE. Where the backward walk
		// produced a number, that number is a decision already taken about
		// straight-line code, and this pass does not re-open it: the ps2gl steady
		// states emit a reader in the row directly under its clipw deliberately,
		// sized against SCE's own output, and openvcl's own test pins those sizes.
		// What is left is the case the walk cannot answer - a reader entered
		// through a label - which it currently answers by doing nothing.
		unsigned int fileRows = UNREACHED;
		{
			unsigned int rows = 0;
			unsigned int j = i;
			while( rows <= 32 )
			{
				if( labelled[j] || j == 0 )
					break;
				--j;
				if( isPush[j] )
				{
					fileRows = rows;
					break;
				}
				++rows;
			}
		}
		if( distance[i] >= fileRows )
			continue;
		const unsigned int pad = latency - 1 - distance[i];
		for( unsigned int p = 0; p < pad; ++p )
			m_codeLines.insert( rowIt[i], formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP) ) );
		return true;
	}
	return false;
}

// A BRANCH CONDITION IS A DISTANCE ALONG A PATH TOO, AND THIS ONE WAS LEFT IN
// FILE ORDER.
//
// The emitter's baseline rule is one bubble row in front of every conditional
// branch. --branch-bubble-on-dependency narrows that to branches whose condition
// is actually produced nearby, and asks the question with
// scheduledSlotsFeedBranch(), which looks at the one or two SCHEDULE SLOTS above
// the branch. For a branch in the middle of a block those are its only
// predecessors and the answer is right. For a branch that is the FIRST ROW OF A
// LABELLED BLOCK it is not: that row has another predecessor, the DELAY SLOT of
// every branch that jumps to the label, and a delay slot is exactly where
// --emit-delay-fillers likes to put an integer op. The file then shows the
// producer five rows above the label and the hardware executes it one row above
// the branch.
//
// That is live in the engine. main's #218 clip programs walk the frustum planes
// with the per-package mask in an integer register:
//
//     planeLoop:
//         ibltz   buffer, planeActive     <- first row of the block
//     ...
//     planeAdvance:
//         ibne    planePtr, vertMask, planeLoop
//         iadd    buffer, buffer, buffer  <- delay slot: shifts the mask
//
// so every iteration after the first tested the PREVIOUS plane's bit, and the
// clipper applied the wrong subset of the frustum planes - visible on hardware
// as unclipped geometry that changes with the camera, and invisible in PCSX2,
// whose VU has no such hazard. Five programs carried it: all five stapip_clip_*.
// Sony's vcl emits the same delay-slot fill and then pads the loop head, which is
// what this pass now does.
//
// WHAT IS NOT CLAIMED. This does not decide a latency. It restores the emitter's
// own invariant - one row between a dependent integer producer and the branch
// reading it - along paths as well as down the page, and the interlocked
// producers --branch-interlock exempts (loads, CLIP and MAC/STATUS readers) stay
// exempt here, because Sony puts those in the row directly above a branch on
// purpose and annotates the stall it leaves to the hardware.
//
// ORDER. This runs after padClipFlagWindowAcrossPaths(). Inserting rows can only
// LENGTHEN a CLIP push-to-reader distance, never shorten one, so no clip
// judgement already made can be invalidated by a pad added here.
void CodeGenerator::padBranchConditionAcrossPaths()
{
	if( !vuBranchBubbleOnDependencyEnabled() )
		return;      // the unconditional bubble is already in front of every branch
	for( unsigned int guard = 0; guard < 4096; ++guard )
	{
		if( !insertOneCrossPathBranchBubble() )
			return;
	}
}

bool CodeGenerator::insertOneCrossPathBranchBubble()
{
	EmittedRowGraph g;
	buildEmittedRowGraph( m_codeLines, g );
	if( g.rowCount == 0 )
		return false;

	// Integer ARITHMETIC writers only. The destination of each of these is the
	// token immediately after the mnemonic in the emitted row, which is what makes
	// this answerable from the emitted text at all. Loads and flag readers are
	// deliberately absent: --branch-interlock leaves those to the hardware.
	static const VuInstructionOpcode arith[10] = {
		VU_OP_IADD, VU_OP_IADDI, VU_OP_IADDIU, VU_OP_ISUB, VU_OP_ISUBIU,
		VU_OP_IAND, VU_OP_IOR, VU_OP_MFIR, VU_OP_XTOP, VU_OP_XITOP };
	const VuInstructionOpcode conditional[6] = { VU_OP_IBEQ, VU_OP_IBGEZ, VU_OP_IBGTZ,
	                                             VU_OP_IBLEZ, VU_OP_IBLTZ, VU_OP_IBNE };

	for( unsigned int i = 0; i < g.rowCount; ++i )
	{
		if( g.branchKind[i] != BR_COND )
			continue;
		// Only a branch entered through a label can have a predecessor the
		// schedule-order test could not see. Everywhere else
		// scheduledSlotsFeedBranch() already decided, and this pass does not
		// re-open a decision that was taken with full information.
		if( !g.labelled[i] )
			continue;
		// A delay slot cannot be pushed away from the branch it belongs to.
		if( i > 0 && ( g.branchKind[i - 1] == BR_COND || g.branchKind[i - 1] == BR_UNCOND ) )
			continue;

		std::list<std::string> reads;
		branchConditionRegisters( g.rowTokens[i], conditional, 6, reads );
		if( reads.empty() )
			continue;

		bool needsPad = false;
		for( unsigned int p = 0; p < g.rowCount && !needsPad; ++p )
		{
			bool feeds = false;
			for( unsigned int s = 0; s < g.successors[p].size(); ++s )
			{
				if( g.successors[p][s] == i )
					feeds = true;
			}
			if( !feeds )
				continue;
			// p executes in the row immediately before the branch. Does it write
			// one of the branch's condition registers?
			for( std::list<std::string>::const_iterator r = reads.begin();
			     r != reads.end() && !needsPad; ++r )
			{
				if( rowWritesIntegerRegister( g.rowTokens[p], arith, 10, *r ) )
					needsPad = true;
			}
		}
		if( !needsPad )
			continue;

		// One row, below the label, so both the fall-through and every taken edge
		// pass through it.
		m_codeLines.insert( g.rowIt[i], formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP) ) );
		return true;
	}
	return false;
}

void CodeGenerator::padForClipFlagWindow( const Token& a, const Token* b )
{
	const unsigned int latency = vuClipFlagVisibilityLatency();
	if( latency <= 1 )
		return;

	bool readsClip = false;
	bool writesClip = false;
	const Token* pair[2] = { &a, b };
	for( int k = 0; k < 2; ++k )
	{
		if( !pair[k] || !pair[k]->operand() )
			continue;
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess( *pair[k], access ) )
			continue;
		if( access.implicitReads & VU_RESOURCE_CLIP )
		{
			// EVERY reader of the flag waits, whatever its mask.
			//
			// This used to exempt a full-window mask, on the argument that the window
			// holds six bits per CLIP so a mask covering the whole thing ("is anything
			// outside", 0x3FFFF for three vertices) gets the same answer whichever
			// position the bits are in. That argument is about WHICH ENTRY a
			// judgement occupies. It says nothing about WHETHER THE NEWEST ONE HAS
			// ARRIVED, and 0x3FFFF is three entries wide: read one row after its CLIP
			// the mask covers a window shifted by a whole vertex, so the answer is
			// about the PREVIOUS vertex. In this engine's cull programs that answer
			// is the ADC bit of the vertex that completes a triangle, so the triangle
			// that gets drawn is the one judged by its own first two vertices and the
			// PREVIOUS triangle's third - a frustum test that can pass a vertex the
			// pipeline never clips. Measured by p8-flagorder.py: 23 readers over 14
			// programs, and SCE does it in none of them.
			//
			// What this does NOT claim is that SCE keeps four rows. Measured over 70
			// programs, SCE puts 89 positional readers closer than three rows to the
			// nearest preceding CLIP: five in each of the sixteen `_cl` programs
			// (masks 15 and 10), three in each of the two billboards (mask 63, one of
			// them in the row directly below), and one in each of three stapip_clip_*.
			// SCE can, for two reasons we cannot copy: it pairs a reader with the CLIP
			// four rows back rather than the nearest one - modelling the register as
			// the 24-bit shift window it is, with two or three CLIPs in flight - and
			// it counts the cycles the hardware will stall on rows it did not pad,
			// which it writes down (STALL_LATENCY ?N) and we neither write down nor
			// model. Rows are the only measure we have, and four of them is the floor.
			readsClip = true;
		}
		if( access.implicitWrites & VU_RESOURCE_CLIP )
			writesClip = true;
	}
	// A row that both writes and reads the flag cannot be helped by padding above it;
	// leave it to the scheduler rather than emit nops that change nothing.
	if( !readsClip || writesClip )
		return;

	unsigned int rows = emittedRowsSinceClipWrite();
	if( rows == 0xFFFFu )
		return;
	while( rows + 1 < latency )
	{
		addNopLine();
		m_currentCycle++;
		++rows;
	}
}

void CodeGenerator::emitSingleToken( const Token& token )
{
	std::string instruction = generateInstruction(token);
	std::string outputLine = "";

	// emit original sourcecode as a comment
	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << token.line().number() << ": " << token.line().content();
		m_codeLines.push_back( s.str() );
	}

	int instructionLength = 32;

	for(int d = 0; d < 20; d++)
		outputLine += " ";

	if(tokenIsLowerExecutionPath(token))
	{
		outputLine += vuInstr(VU_OP_NOP);

		for(int d = 0; d < instructionLength-3; d++)
			outputLine += " ";

		outputLine += instruction;
	}
	else
	{
		outputLine += instruction;

		if( !token.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(instruction.length())) <= 0 )
				outputLine += " ";

			for(int d = 0; d < instructionLength-int(instruction.length()); d++)
				outputLine += " ";

			outputLine += vuInstr(VU_OP_NOP);
		}
	}

	padForClipFlagWindow( token, NULL );

	const unsigned int branchDelaySlots = vuTokenBranchDelaySlots( token );
	if( branchDelaySlots > 0 )
	{
		padForBranchPreBubble( token );
		m_codeLines.push_back(outputLine);
		m_currentCycle++;
		for( unsigned int i = 0; i < branchDelaySlots; ++i )
		{
			addNopLine();
			m_currentCycle++;
		}
	}
	else
	{
		int issueCycle = m_currentCycle;
		m_codeLines.push_back(outputLine);
		m_currentCycle++;
		recordRegisterWrites(token, issueCycle);
	}

}

void CodeGenerator::emitBranchWithDelayFiller( const Token& branch, const Token& filler )
{
	std::string branchInstruction = generateInstruction(branch);
	std::string branchLine = "";
	std::string fillerInstruction = generateInstruction(filler);
	std::string fillerLine = "";
	const int instructionLength = 32;

	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << branch.line().number() << ": " << branch.line().content();
		m_codeLines.push_back( s.str() );
	}

	for(int d = 0; d < 20; d++)
		branchLine += " ";

	if(tokenIsLowerExecutionPath(branch))
	{
		branchLine += vuInstr(VU_OP_NOP);
		for(int d = 0; d < instructionLength-3; d++)
			branchLine += " ";
		branchLine += branchInstruction;
	}
	else
	{
		branchLine += branchInstruction;
		if( !branch.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(branchInstruction.length())) <= 0 )
				branchLine += " ";
			for(int d = 0; d < instructionLength-int(branchInstruction.length()); d++)
				branchLine += " ";
			branchLine += vuInstr(VU_OP_NOP);
		}
	}

	padForBranchPreBubble( branch );
	m_codeLines.push_back(branchLine);
	m_currentCycle++;

	if( emitSource() )
	{
		std::stringstream s;
		s << " ; Line " << filler.line().number() << ": " << filler.line().content();
		m_codeLines.push_back( s.str() );
	}

	for(int d = 0; d < 20; d++)
		fillerLine += " ";

	if(tokenIsLowerExecutionPath(filler))
	{
		fillerLine += vuInstr(VU_OP_NOP);
		for(int d = 0; d < instructionLength-3; d++)
			fillerLine += " ";
		fillerLine += fillerInstruction;
	}
	else
	{
		fillerLine += fillerInstruction;
		if( !filler.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(fillerInstruction.length())) <= 0 )
				fillerLine += " ";
			for(int d = 0; d < instructionLength-int(fillerInstruction.length()); d++)
				fillerLine += " ";
			fillerLine += vuInstr(VU_OP_NOP);
		}
	}

	m_codeLines.push_back(fillerLine);
	recordRegisterWrites(filler, m_currentCycle);
	m_currentCycle++;
}

void CodeGenerator::emitPairedTokens( const Token& a, const Token& b )
{
	const unsigned int aBranchDelaySlots = vuTokenBranchDelaySlots( a );
	const unsigned int bBranchDelaySlots = vuTokenBranchDelaySlots( b );
	const unsigned int branchDelaySlots = aBranchDelaySlots > bBranchDelaySlots ? aBranchDelaySlots : bBranchDelaySlots;
	std::string pairedLine;
	if( tokenIsLowerExecutionPath(a) )
		pairedLine = formatPairedLine(b, a);
	else
		pairedLine = formatPairedLine(a, b);
	padForClipFlagWindow( a, &b );
	if( branchNeedsPreBubble(a) )
		padForBranchPreBubble(a);
	if( branchNeedsPreBubble(b) )
		padForBranchPreBubble(b);
	m_codeLines.push_back(pairedLine);

	recordRegisterWrites(a, m_currentCycle);
	recordRegisterWrites(b, m_currentCycle);
	m_currentCycle++;
	for( unsigned int i = 0; i < branchDelaySlots; ++i )
	{
		addNopLine();
		m_currentCycle++;
	}
}

void CodeGenerator::emitPairedBranchWithDelayFiller( const Token& a, const Token& b, const Token& filler )
{
	std::string pairedLine;
	if( tokenIsLowerExecutionPath(a) )
		pairedLine = formatPairedLine(b, a);
	else
		pairedLine = formatPairedLine(a, b);
	padForClipFlagWindow( a, &b );
	if( branchNeedsPreBubble(a) )
		padForBranchPreBubble(a);
	if( branchNeedsPreBubble(b) )
		padForBranchPreBubble(b);
	m_codeLines.push_back(pairedLine);

	recordRegisterWrites(a, m_currentCycle);
	recordRegisterWrites(b, m_currentCycle);
	m_currentCycle++;

	std::string fillerInstruction = generateInstruction(filler);
	std::string fillerLine = "";
	const int instructionLength = 32;
	for(int d = 0; d < 20; d++)
		fillerLine += " ";

	if(tokenIsLowerExecutionPath(filler))
	{
		fillerLine += vuInstr(VU_OP_NOP);
		for(int d = 0; d < instructionLength-3; d++)
			fillerLine += " ";
		fillerLine += fillerInstruction;
	}
	else
	{
		fillerLine += fillerInstruction;
		if( !filler.operand()->isPreprocessor() )
		{
			if( (instructionLength-int(fillerInstruction.length())) <= 0 )
				fillerLine += " ";
			for(int d = 0; d < instructionLength-int(fillerInstruction.length()); d++)
				fillerLine += " ";
			fillerLine += vuInstr(VU_OP_NOP);
		}
	}

	m_codeLines.push_back(fillerLine);
	recordRegisterWrites(filler, m_currentCycle);
	m_currentCycle++;
}

namespace
{

const Token* branchTokenInScheduledSlot( const VuScheduledIssueSlot& slot )
{
	if( slot.firstToken && vuTokenBranchDelaySlots(*slot.firstToken) > 0 )
		return slot.firstToken;
	if( slot.secondToken && vuTokenBranchDelaySlots(*slot.secondToken) > 0 )
		return slot.secondToken;
	return NULL;
}

const Token* branchDelayFillerInScheduledSlot( const VuScheduledIssueSlot& slot )
{
	if( slot.firstToken && (slot.firstToken->flags() & Token::BRANCH_DELAY_FILLER) )
		return slot.firstToken;
	if( slot.secondToken && (slot.secondToken->flags() & Token::BRANCH_DELAY_FILLER) )
		return slot.secondToken;
	return NULL;
}

// 9.G-1h-4a-3b (G-1): bake-in record for one rewritten MAIN body.
// Built from a VuKernelBlockRange descriptor against the live post-
// rewrite token list passed into emitStrictScheduledProgram. The
// consumer side resolves the MAIN_LOOP / EPI0 labels by name, walks
// the body positionally, and pairs the placer grid's non-NOP cells
// (4 lanes per cycle: upper, lower, fdiv, efu) onto surviving body
// tokens 1:1. Rename-eligible plans are NOT published into the
// descriptor (see VuSchedulerAnalysis.cpp gate), so each non-NOP
// grid cell maps to exactly one body token here.
struct VuKernelBakeIn
{
	const Token* mainLabelTok;                                        // marks the MAIN_LOOP label slot
	const Token* branchTok;                                           // the closing back-edge branch
	unsigned int II;
	std::vector< std::pair<const Token*, const Token*> > cycles;      // (upper, effective-lower) per cycle
	std::set<const Token*> bodySkip;                                  // tokens to skip when the scheduler revisits them
	bool active;

	VuKernelBakeIn() : mainLabelTok(NULL), branchTok(NULL), II(0), active(false) {}
};

void buildKernelBakeIns( const std::list<Token>& tokens,
                         const std::vector<VuKernelBlockRange>& ranges,
                         std::vector<VuKernelBakeIn>& outBakeIns )
{
	outBakeIns.clear();
	outBakeIns.reserve( ranges.size() );

	for( size_t r = 0; r < ranges.size(); ++r )
	{
		const VuKernelBlockRange& range = ranges[r];
		VuKernelBakeIn b;
		b.II = range.II;

		const Token* mainLabelTok = NULL;
		const Token* endLabelTok  = NULL;
		for( std::list<Token>::const_iterator it = tokens.begin(); it != tokens.end(); ++it )
		{
			if( !mainLabelTok && it->label() == range.mainLabel ) mainLabelTok = &*it;
			else if( !endLabelTok && it->label() == range.endLabel ) { endLabelTok = &*it; break; }
		}
		if( mainLabelTok == NULL || endLabelTok == NULL || range.II == 0 )
		{
			outBakeIns.push_back(b);
			continue;
		}
		b.mainLabelTok = mainLabelTok;

		// Collect body tokens between (mainLabelTok, endLabelTok). The
		// last token in that range is the back-edge branch (mainBranch
		// in the rewrite emitter); everything before it is body.
		std::vector<const Token*> body;
		bool inRange = false;
		const Token* lastInRange = NULL;
		for( std::list<Token>::const_iterator it = tokens.begin(); it != tokens.end(); ++it )
		{
			const Token* p = &*it;
			if( p == endLabelTok ) break;
			if( inRange )
			{
				if( lastInRange ) body.push_back( lastInRange );
				lastInRange = p;
			}
			if( p == mainLabelTok ) inRange = true;
		}
		if( lastInRange == NULL )
		{
			outBakeIns.push_back(b);
			continue;
		}
		b.branchTok = lastInRange;

		// 9.G-1h-4b-7b: if the analysis published a refit grid for this
		// range, prefer it over the original positional grid->body
		// mapping. The rewrite emitter in applyVuGenericKernelRewritePlans
		// walks plan.kernelRewriteMainTokens cell-by-cell and emits, per
		// non-NO_TOKEN cell, either (materializeMOVEs* + src verbatim)
		// or N split clones; then a tail block of one MOVE per assigned
		// decision. buildVuKernelExpandedNodes in VuSchedulerAnalysis.cpp
		// walks the exact same sequence to build the refitNodes vector,
		// so body[k] (k-th surviving body token) corresponds 1:1 to
		// refitNodes[k]. refitMainTokens (II*4) holds, per non-empty
		// cell, the refitNodes index whose body[] token should be placed
		// at that (cycle, lane). When the count alignment fails we fall
		// back to the legacy positional path so any size mismatch keeps
		// bake-in inactive (consumer treats !b.active as "no bake-in").
		// Only take the refit path when the analysis actually produced
		// non-PASSTHROUGH nodes (split clones, materialize MOVEs, or
		// tail MOVEs). All-passthrough refit grids carry the same body
		// the legacy positional walk does, but the shadow placer may
		// permute cells inside a cycle, which would silently change
		// emission for shaders the refit path is not meant to alter.
		bool refitHasRewrites = false;
		if( !range.refitMainTokens.empty() && body.size() == range.refitNodes.size() )
		{
			for( size_t n = 0; n < range.refitNodes.size(); ++n )
			{
				if( range.refitNodes[n].role != VuKernelRefitNode::ROLE_PASSTHROUGH )
				{
					refitHasRewrites = true;
					break;
				}
			}
		}
		if( refitHasRewrites )
		{
			bool refitOk = true;
			b.cycles.assign( range.II, std::pair<const Token*, const Token*>( NULL, NULL ) );
			for( unsigned int g = 0; g < range.refitMainTokens.size() && refitOk; ++g )
			{
				const unsigned int nodeIdx = range.refitMainTokens[g];
				if( nodeIdx == VuKernelRefitNode::NO_INDEX )
					continue;
				if( nodeIdx >= body.size() ) { refitOk = false; break; }
				const unsigned int cycle = g / 4;
				const unsigned int lane  = g % 4;
				if( cycle >= range.II ) { refitOk = false; break; }
				const Token* tok = body[nodeIdx];
				// Map (upper=0, lower=1, fdiv=2, efu=3) onto the
				// (upper, lower) pair the consumer expects, with the
				// same fdiv/efu->lower fallback as the legacy path.
				std::pair<const Token*, const Token*>& slot = b.cycles[cycle];
				if( lane == 0 )
				{
					if( slot.first ) { refitOk = false; break; }
					slot.first = tok;
				}
				else
				{
					if( !slot.second )
						slot.second = tok;
					// Multiple non-upper lanes can fold into the
					// single "lower" slot in the consumer's view; the
					// legacy path silently overwrote earlier picks
					// only when no later lane filled in, so prefer
					// the first non-NULL choice (lower > fdiv > efu).
				}
			}
			if( refitOk )
			{
				for( size_t k = 0; k < body.size(); ++k )
					b.bodySkip.insert( body[k] );
				b.bodySkip.insert( b.branchTok );
				b.active = true;
				outBakeIns.push_back(b);
				continue;
			}
			// Refit alignment failed: fall through to legacy positional
			// path. Reset cycles so the legacy walk starts from a clean
			// vector.
			b.cycles.clear();
		}

		// Map grid cells onto body tokens in placer push order.
		unsigned int bodyIdx = 0;
		bool ok = true;
		b.cycles.resize( range.II );
		for( unsigned int c = 0; c < range.II && ok; ++c )
		{
			const Token* lane[4] = { NULL, NULL, NULL, NULL };
			for( unsigned int l = 0; l < 4; ++l )
			{
				const unsigned int gridIdx = c * 4 + l;
				if( gridIdx >= range.placerGridMainTokens.size() ) continue;
				if( range.placerGridMainTokens[gridIdx] == VuKernelRewritePlan::NO_TOKEN ) continue;
				if( bodyIdx >= body.size() ) { ok = false; break; }
				lane[l] = body[bodyIdx++];
			}
			if( !ok ) break;
			const Token* upper = lane[0];
			const Token* lower = lane[1];
			if( !lower ) lower = lane[2];
			if( !lower ) lower = lane[3];
			b.cycles[c] = std::make_pair( upper, lower );
		}
		if( !ok || bodyIdx != body.size() )
		{
			outBakeIns.push_back(b);
			continue;
		}

		for( size_t k = 0; k < body.size(); ++k )
			b.bodySkip.insert( body[k] );
		b.bodySkip.insert( b.branchTok );
		b.active = true;
		outBakeIns.push_back(b);
	}
}

}

bool CodeGenerator::prepareStrictScheduledToken( const Token& token,
                                                 bool& exitWritten,
                                                 bool allowBranchDelayFiller )
{
	if( token.label().length() > 0 )
		m_codeLines.push_back(token.label() + ":");

	if( !token.operand() )
		return false;

	if( ((token.operand()->unit() == Operand::EXIT) || (token.operand()->unit() == Operand::ENTER)) && !exitWritten )
	{
		m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP) + "[E]", vuInstr(VU_OP_NOP)));
		m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP)));
		exitWritten = true;
	}

	if( token.flags()&Token::IGNORED )
		return false;

	if( !(token.flags()&Token::PROCESSED) && !(token.operand()->flags()&Operand::PREPROCESSOR) )
		return false;

	if( token.operand()->flags()&Operand::PREPROCESSOR )
	{
		if( token.operand()->name() == ".vu" )
			return false;
		if( token.operand()->name() == "--cont" )
		{
			m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP) + "[E]", vuInstr(VU_OP_NOP)));
			m_codeLines.push_back(formatRawPairedInstructionLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP)));
			exitWritten = true;
			return false;
		}
		if( token.operand()->name() == "--barrier" )
			return false;
	}
	else
	{
		exitWritten = false;
	}

	if( token.operand()->flags()&Operand::FILTERED )
		return false;

	return true;
}

void CodeGenerator::emitStrictScheduledPadding( const VuScheduledIssueSlot& slot )
{
	switch( slot.paddingKind )
	{
		case VU_SCHEDULED_PADDING_WAITQ:
			emitWaitQ();
			break;

		case VU_SCHEDULED_PADDING_WAITP:
			emitWaitP();
			break;

		case VU_SCHEDULED_PADDING_NOP:
		{
			// emitCycleCount, not cycleCount: a wait on the interlocked FMAC
			// pipeline costs cycles on the hardware either way, and writing nops
			// for it only burns micro memory. See VuLatencyTracker's
			// manualReadHazardDelay. m_currentCycle still advances by the full
			// count so the cycle model stays truthful.
			const unsigned int cycles = slot.cycleCount > 0 ? slot.cycleCount : 1;
			const unsigned int words = slot.emitCycleCount < cycles ? slot.emitCycleCount : cycles;
			for( unsigned int i = 0; i < words; ++i )
				addNopLine();
			// The cycles that became no words belong to the FMAC interlock and to
			// nothing else. Q and P are not interlocked, so a division's latency
			// cannot be paid out of them - see the same call in
			// appendReadHazardPaddingSlots.
			m_latencyTracker.delayPipelinedResultsBy(
				static_cast<int>( cycles ) - static_cast<int>( words ) );
			m_currentCycle += cycles;
			break;
		}

		case VU_SCHEDULED_PADDING_NONE:
			break;
	}
}

// Puts the build-wide --exempt-full-clip-masks setting back when the emission of one
// program is over, so a source file holding two programs does not inherit the arm the
// first one happened to win with.
namespace
{
	struct VuClipExemptionScope
	{
		explicit VuClipExemptionScope( bool restore ) : m_restore( restore ) {}
		~VuClipExemptionScope() { setVuExemptFullClipMasksEnabled( m_restore ); }

	private:
		bool m_restore;
	};
}

// --clip-exemption-best-of. Both arms are legal schedules of the same token list -
// the exemption only ever removes a constraint the emitter does not impose either -
// so the choice between them is pure size, and it is made here, once per program,
// against the word count the whole program will emit.
//
// Not against the segment's: freeing a reader cannot lengthen the segment it sits in,
// so a per-segment comparison would take the exemption almost everywhere and call it
// a win, while the rows it costs land further down. stapip_cull_d and stapip_cull_td
// are exactly that case.
//
// The winning arm is left in force for the emission that follows: CodeGenerator's own
// m_latencyTracker asks the same question again while it pairs and pads, and it has
// to get the answer the schedule was built with.
static VuScheduledProgram scheduleProgramChoosingClipExemption( const std::list<Token>& tokens )
{
	if( !vuClipExemptionBestOfEnabled() || !vuTokenListHasFullWindowClipReader( tokens ) )
		return scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );

	setVuExemptFullClipMasksEnabled( false );
	VuScheduledProgram withoutExemption = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );
	const unsigned int withoutWords = vuScheduledProgramEmittedWordCount( withoutExemption );

	setVuExemptFullClipMasksEnabled( true );
	VuScheduledProgram withExemption = scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );
	const unsigned int withWords = vuScheduledProgramEmittedWordCount( withExemption );

	if( vuShowPairMissesEnabled() )
		std::cerr << "[clipexempt] off=" << withoutWords
		          << " on=" << withWords
		          << " keep=" << ( withWords < withoutWords ? "on" : "off" ) << std::endl;

	// Ties go to "off": a program the exemption does not shorten keeps the schedule it
	// would have had without the flag, so the flag can only ever be read as a win.
	if( withWords < withoutWords )
		return withExemption;

	setVuExemptFullClipMasksEnabled( false );
	return withoutExemption;
}

bool CodeGenerator::emitStrictScheduledProgram( const std::list<Token>& tokens, bool& exitWritten )
{
	const bool exemptionOnEntry = vuExemptFullClipMasksEnabled();
	VuScheduledProgram program = scheduleProgramChoosingClipExemption(tokens);
	const VuClipExemptionScope exemptionScope( exemptionOnEntry );
	std::vector<const VuScheduledIssueSlot*> slots;
	for( std::vector<VuScheduledBasicBlock>::const_iterator block = program.blocks.begin();
	     block != program.blocks.end();
	     ++block )
	{
		for( std::vector<VuScheduledIssueSlot>::const_iterator slot = block->issueSlots.begin();
		     slot != block->issueSlots.end();
		     ++slot )
		{
			slots.push_back(&(*slot));
		}
	}

	// 9.G-1h-4a-3b (G-1): build bake-in tables for non-rename-eligible
	// rewritten MAIN bodies. The rewrite pass published a descriptor per
	// such body in m_kernelBlockRanges; here we resolve labels to live
	// tokens and align the placer grid against surviving body tokens.
	std::vector<VuKernelBakeIn> bakeIns;
	buildKernelBakeIns( tokens, m_kernelBlockRanges, bakeIns );

	for( unsigned int i = 0; i < slots.size(); ++i )
	{
		const VuScheduledIssueSlot& slot = *slots[i];
		m_ignoredImplicitWawResources = slot.ignoredImplicitWawResources;

		// 9.G-1h-4a-3b (G-1): detect arrival at a baked-in MAIN_LOOP label
		// and emit the entire body + back-edge branch in placer order,
		// then skip the scheduler's emission of those tokens.
		{
			const VuKernelBakeIn* bake = NULL;
			for( size_t r = 0; r < bakeIns.size(); ++r )
			{
				if( !bakeIns[r].active ) continue;
				if( slot.firstToken == bakeIns[r].mainLabelTok ||
				    slot.secondToken == bakeIns[r].mainLabelTok )
				{
					bake = &bakeIns[r];
					break;
				}
			}
			if( bake )
			{
				// 9.G-1h-4a-3b (G-1) cost gate: only fire bake-in when
				// the placer grid is at least as tight as what the
				// scheduler is about to emit for the same range. Each
				// VuScheduledIssueSlot is one issue cycle (padding NOP
				// slots included), so the scheduler's slot count over
				// [i .. lastBodySlot] is the candidate cycle budget.
				unsigned int lastBodySlot = i;
				for( unsigned int j = i + 1; j < slots.size(); ++j )
				{
					const VuScheduledIssueSlot& cand = *slots[j];
					const bool ftHit = cand.firstToken  && bake->bodySkip.count(cand.firstToken);
					const bool stHit = cand.secondToken && bake->bodySkip.count(cand.secondToken);
					if( ftHit || stHit )
						lastBodySlot = j;
				}
				const unsigned int schedulerCycles = lastBodySlot - i + 1;
				const unsigned int branchDelay =
					bake->branchTok ? vuTokenBranchDelaySlots(*bake->branchTok) : 0;
				const unsigned int lastCycle = bake->II - 1;
				const bool substituteBranch =
					( bake->II > 0 ) && ( bake->cycles[lastCycle].second == NULL );
				const unsigned int bakeCycles =
					bake->II + branchDelay + ( substituteBranch ? 0u : 1u );
				if( bakeCycles > schedulerCycles )
				{
					// Bake-in would regress; let the scheduler handle
					// this body normally. (Tracking 9.G-1h-5: placer
					// relaxation should close this gap before bake-in
					// is allowed to fire by default.)
					bake = NULL;
				}
			}
			if( bake )
			{
				// Emit the MAIN_LOOP label.
				m_codeLines.push_back( bake->mainLabelTok->label() + ":" );

				// Smart-tail substitution: if the last cycle has an empty
				// lower lane, place the back-edge branch there to save a
				// cycle (matches SCEI's last-cycle pairing).
				const unsigned int lastCycle = bake->II - 1;
				const bool substituteBranch =
					( bake->II > 0 ) && ( bake->cycles[lastCycle].second == NULL );

				for( unsigned int c = 0; c < bake->II; ++c )
				{
					const Token* up = bake->cycles[c].first;
					const Token* lo = bake->cycles[c].second;
					const bool isLast = ( c == lastCycle );
					if( isLast && substituteBranch )
					{
						// up may be NULL (rare). emitPairedTokens handles
						// the branch's delay slots internally.
						if( up )
							emitPairedTokens( *up, *bake->branchTok );
						else
							emitSingleToken( *bake->branchTok );
						continue;
					}

					if( !up && !lo )
					{
						addNopLine();
						m_currentCycle++;
					}
					else if( up && lo )
					{
						emitPairedTokens( *up, *lo );
					}
					else if( up )
					{
						const int issueCycle = m_currentCycle;
						m_codeLines.push_back( formatRawPairedInstructionLine( generateInstruction(*up), vuInstr(VU_OP_NOP) ) );
						recordRegisterWrites( *up, issueCycle );
						m_currentCycle++;
					}
					else
					{
						const int issueCycle = m_currentCycle;
						m_codeLines.push_back( formatRawPairedInstructionLine( vuInstr(VU_OP_NOP), generateInstruction(*lo) ) );
						recordRegisterWrites( *lo, issueCycle );
						m_currentCycle++;
					}
				}

				if( !substituteBranch )
					emitSingleToken( *bake->branchTok );

				// Advance past every slot up to (and including) the last
				// slot whose firstToken or secondToken is one of the body
				// or back-edge tokens we just emitted. Padding NOP slots
				// interleaved in that range are skipped too — their NOPs
				// were already accounted for by empty grid cells in the
				// bake-in cycle loop above.
				unsigned int lastBodySlot = i;
				for( unsigned int j = i + 1; j < slots.size(); ++j )
				{
					const VuScheduledIssueSlot& cand = *slots[j];
					const bool ftHit = cand.firstToken  && bake->bodySkip.count(cand.firstToken);
					const bool stHit = cand.secondToken && bake->bodySkip.count(cand.secondToken);
					if( ftHit || stHit )
						lastBodySlot = j;
				}
				i = lastBodySlot;
				continue;
			}
		}

		if( slot.padding )
		{
			emitStrictScheduledPadding(slot);
			continue;
		}

		const Token* first = slot.firstToken;
		const Token* second = slot.secondToken;
		if( second == first )
			second = NULL;

		bool firstEmits = first && prepareStrictScheduledToken(*first, exitWritten, false);
		bool secondEmits = second && prepareStrictScheduledToken(*second, exitWritten, false);
		if( (slot.paddingKind == VU_SCHEDULED_PADDING_WAITQ
		     || slot.paddingKind == VU_SCHEDULED_PADDING_WAITP)
		    && firstEmits
		    && !secondEmits
		    && first
		    && tokenIsUpperExecutionPath(*first) )
		{
			emitUpperWithWait(*first, slot.paddingKind == VU_SCHEDULED_PADDING_WAITQ);
			continue;
		}
		const Token* branch = branchTokenInScheduledSlot(slot);
		m_skipBranchPreBubble = branch != NULL
		                        && vuBranchBubbleOnDependencyEnabled()
		                        && !scheduledSlotsFeedBranch( slots, i, *branch );
		const Token* delayFiller = NULL;
		unsigned int delayFillerSlot = i + 1;
		while( branch && delayFillerSlot < slots.size() && slots[delayFillerSlot]->padding )
			++delayFillerSlot;
		if( branch && delayFillerSlot < slots.size() )
			delayFiller = branchDelayFillerInScheduledSlot(*slots[delayFillerSlot]);

		// --emit-delay-fillers: a branch's delay slot is a word whether or not
		// anything useful is in it. fillBranchDelaySlots() runs before scheduling
		// and only ever considers the instruction that precedes the branch IN
		// SOURCE ORDER, so after scheduling most slots are still empty (measured
		// on stapip_clip_c: 13 of 20 empty, against 9 of 20 for SCE's vcl, whose
		// filled slots hold sq/isw as often as integer ops).
		//
		// Here the schedule is known, so the instruction actually scheduled just
		// BEFORE the branch can be offered instead. It executes once either way -
		// the delay slot runs before the branch takes effect - and
		// canMoveIntoBranchDelaySlot() is the same legality test the pre-pass
		// uses: no label, no second branch, and the branch must not read what the
		// candidate writes (it used to read the new value; after the move it would
		// see the old one).
		if( !delayFiller && branch && vuEmitDelayFillersEnabled() && i > 0 )
		{
			const VuScheduledIssueSlot& prev = *slots[i - 1];
			const Token* candidate = prev.padding ? NULL : prev.firstToken;
			if( candidate
			    && !prev.secondToken
			    && candidate != branch
			    && m_emittedDelayFiller != candidate
			    && canMoveIntoBranchDelaySlot(*candidate, *branch)
			    && prepareStrictScheduledToken(*candidate, exitWritten, true) )
			{
				// The previous slot has already been emitted as its own row, so
				// take that row back and re-emit the pair as branch + filler.
				if( retractLastEmittedRow() )
				{
					m_emittedDelayFiller = candidate;
					emitBranchWithDelayFiller(*branch, *candidate);
					if( isVuTerminalUnconditionalBranch(*branch) )
						exitWritten = true;
					continue;
				}
			}
		}

		if( delayFiller && prepareStrictScheduledToken(*delayFiller, exitWritten, true) )
		{
			const Token* branchPartner = NULL;
			if( firstEmits && first != branch )
				branchPartner = first;
			if( secondEmits && second != branch )
				branchPartner = second;

			if( firstEmits && secondEmits
			    && branch
			    && branchPartner
			    && vuTokensHaveDataDependency(*branchPartner, *delayFiller) )
			{
				emitSingleToken(*branchPartner);
				while( readHazardDelay(*delayFiller, NULL) > 1 )
				{
					addNopLine();
					m_currentCycle++;
				}
				emitBranchWithDelayFiller(*branch, *delayFiller);
			}
			else if( firstEmits && secondEmits )
				emitPairedBranchWithDelayFiller(*first, *second, *delayFiller);
			else if( branch == first && firstEmits )
				emitBranchWithDelayFiller(*first, *delayFiller);
			else if( branch == second && secondEmits )
				emitBranchWithDelayFiller(*second, *delayFiller);
			else if( firstEmits )
				emitSingleToken(*first);
			else if( secondEmits )
				emitSingleToken(*second);
			else
				emitSingleToken(*delayFiller);
			if( branch && isVuTerminalUnconditionalBranch(*branch) )
				exitWritten = true;
			i = delayFillerSlot;
			continue;
		}

		if( firstEmits && secondEmits )
		{
			if( tokensCanPair(*first, *second) )
			{
				emitPairedTokens(*first, *second);
			}
			else
			{
				emitSingleToken(*first);
				emitSingleToken(*second);
			}
			if( isVuTerminalUnconditionalBranch(*first) || isVuTerminalUnconditionalBranch(*second) )
				exitWritten = true;
			continue;
		}

		if( firstEmits )
		{
			emitSingleToken(*first);
			if( isVuTerminalUnconditionalBranch(*first) )
				exitWritten = true;
		}
		if( secondEmits )
		{
			emitSingleToken(*second);
			if( isVuTerminalUnconditionalBranch(*second) )
				exitWritten = true;
		}
	}

	return true;
}

bool CodeGenerator::emitsAsUpperMove( const Token& token ) const
{
	return ( m_enableUpperMoves || (m_ignoredImplicitWawResources & VU_RESOURCE_MAC) )
	    && isVuMoveAsUpperMaxCandidate(token);
}

bool CodeGenerator::tokenIsLowerExecutionPath( const Token& token ) const
{
	if( emitsAsUpperMove(token) )
		return false;
	return token.operand() && token.operand()->isLowerExecutionPath();
}

bool CodeGenerator::tokenIsUpperExecutionPath( const Token& token ) const
{
	if( emitsAsUpperMove(token) )
		return true;
	return token.operand() && token.operand()->isUpperExecutionPath();
}

int CodeGenerator::readHazardDelay( const Token& token, const Token* partner ) const
{
	return m_latencyTracker.readHazardDelay( token, partner, m_currentCycle );
}

void CodeGenerator::padForReadHazards( const Token& token, const Token* partner )
{
	while( true )
	{
		int needed = readHazardDelay(token, partner);
		if( needed <= 0 )
			break;

		const VuScheduledPaddingKind paddingKind =
			vuScheduledPaddingKindForReadHazard( token, partner, m_latencyTracker, m_currentCycle );
		if( paddingKind == VU_SCHEDULED_PADDING_WAITQ )
		{
			emitWaitQ();
			continue;
		}
		if( paddingKind == VU_SCHEDULED_PADDING_WAITP )
		{
			emitWaitP();
			continue;
		}

		addNopLine();
		m_currentCycle++;
	}
}

void CodeGenerator::recordRegisterWrites( const Token& token, int issueCycle )
{
	m_latencyTracker.recordWrites( token, issueCycle, emitsAsUpperMove( token ) );
}

unsigned int CodeGenerator::ignoredImplicitWawResourcesForRemaining( std::list<Token>::const_iterator programBegin,
                                                                     std::list<Token>::const_iterator begin,
                                                                     std::list<Token>::const_iterator end ) const
{
	return vuIgnoredFlagWawResourcesForRemaining( programBegin, begin, end );
}

void CodeGenerator::fillBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( branch->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			continue;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess(*branch, branchAccess) )
			continue;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			continue;
		if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
			continue;

		if( movePreIncrementStoreIntoBranchDelaySlot(tokens, branch) )
			continue;
		if( moveIndependentStoreIntoBranchDelaySlot(tokens, branch) )
			continue;

		if( branch == tokens.begin() )
			continue;
		std::list<Token>::iterator candidate = branch;
		--candidate;

		// --emit-delay-fillers: if the instruction right before the branch cannot
		// go in the slot, keep walking back. A candidate further up is just as
		// good as long as it can cross everything between itself and the branch,
		// which vuTokenCanMoveBefore answers with the same rules the scheduler
		// uses. Without this the search gives up after one try and the slot stays
		// a nop word - measured on stapip_clip_c, 13 of 20 slots empty against
		// SCE vcl's 9.
		if( !canMoveIntoBranchDelaySlot(*candidate, *branch) )
		{
			if( !vuEmitDelayFillersEnabled() )
				continue;

			bool found = false;
			std::list<Token>::iterator probe = candidate;
			for( unsigned int steps = 0; steps < 16 && probe != tokens.begin(); ++steps )
			{
				--probe;
				if( !canMoveIntoBranchDelaySlot(*probe, *branch) )
					continue;

				bool crossable = true;
				for( std::list<Token>::iterator crossed = probe; crossable; )
				{
					++crossed;
					if( crossed == branch )
						break;
					if( !vuTokenCanMoveBefore( *crossed, *probe ) )
						crossable = false;
				}
				if( !crossable )
					continue;

				candidate = probe;
				found = true;
				break;
			}
			if( !found )
				continue;
		}

		Token filler = *candidate;
		filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);
		tokens.erase(candidate);
		std::list<Token>::iterator afterBranch = branch;
		++afterBranch;
		tokens.insert(afterBranch, filler);
	}
}

void CodeGenerator::fillPreIncrementStoreBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( branch->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T) )
			continue;

		VuTokenResourceAccess branchAccess;
		if( !buildVuTokenResourceAccess(*branch, branchAccess) )
			continue;
		if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
			continue;

		movePreIncrementStoreIntoBranchDelaySlot(tokens, branch);
	}
}

bool CodeGenerator::movePreIncrementStoreIntoBranchDelaySlot( std::list<Token>& tokens,
                                                              std::list<Token>::iterator branch ) const
{
	if( branch == tokens.begin() )
		return false;
	if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
		return false;

	std::list<Token>::iterator increment = branch;
	--increment;
	if( increment == tokens.begin() )
		return false;
	std::list<Token>::iterator store = increment;
	--store;

	if( store->label().length() != 0 || increment->label().length() != 0 )
		return false;
	if( store->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER
	                      | Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( increment->flags() & (Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( !isVuPlainMemoryStore(*store) )
		return false;

	std::string incrementReg;
	long incrementAmount = 0;
	if( !integerSelfImmediateAdd(*increment, incrementReg, incrementAmount) )
		return false;
	(void)incrementAmount;
	if( !vuTokenReadsRegister(*branch, incrementReg) )
		return false;

	VuTokenResourceAccess storeAccess;
	if( !buildVuTokenResourceAccess(*store, storeAccess) )
		return false;
	if( !storeAccess.hasMemoryBase || !storeAccess.hasMemoryOffset )
		return false;
	if( storeAccess.memoryBaseRegister != incrementReg )
		return false;
	if( storeAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( storeAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;

	VuTokenResourceAccess incrementAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*increment, incrementAccess)
	    || !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( incrementAccess.memoryKind != VU_MEMORY_NONE
	    || incrementAccess.implicitReads != VU_RESOURCE_NONE
	    || incrementAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
		return false;

	Token filler = *store;
	if( !adjustPlainStoreOffset(filler, incrementReg, -incrementAmount) )
		return false;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);

	tokens.erase(store);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

bool CodeGenerator::moveIndependentStoreIntoBranchDelaySlot( std::list<Token>& tokens,
                                                             std::list<Token>::iterator branch ) const
{
	if( branch == tokens.begin() )
		return false;
	if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
		return false;

	std::list<Token>::iterator increment = branch;
	--increment;
	if( increment == tokens.begin() )
		return false;
	std::list<Token>::iterator store = increment;
	--store;

	if( store->label().length() != 0 || increment->label().length() != 0 )
		return false;
	if( store->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER
	                      | Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( increment->flags() & (Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( !isVuPlainMemoryStore(*store) )
		return false;

	std::string incrementReg;
	long incrementAmount = 0;
	if( !integerSelfImmediateAdd(*increment, incrementReg, incrementAmount) )
		return false;
	if( !vuTokenReadsRegister(*branch, incrementReg) )
		return false;

	VuTokenResourceAccess storeAccess;
	VuTokenResourceAccess incrementAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*store, storeAccess)
	    || !buildVuTokenResourceAccess(*increment, incrementAccess)
	    || !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
		return false;
	if( storeAccess.memoryFlags != VU_MEMORY_FLAG_NONE
	    || storeAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( intersectsKeys(storeAccess.registerReads, incrementAccess.registerWrites)
	    || intersectsKeys(storeAccess.registerWrites, incrementAccess.registerReads)
	    || intersectsKeys(storeAccess.registerWrites, incrementAccess.registerWrites) )
		return false;
	if( intersectsKeys(storeAccess.registerWrites, branchAccess.registerReads)
	    || intersectsKeys(storeAccess.registerReads, branchAccess.registerWrites) )
		return false;

	Token filler = *store;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);

	tokens.erase(store);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

void CodeGenerator::fillDeadFallthroughBranchDelaySlots( std::list<Token>& tokens ) const
{
	for( std::list<Token>::iterator branch = tokens.begin(); branch != tokens.end(); ++branch )
	{
		if( vuTokenBranchDelaySlots(*branch) != 1 )
			continue;
		if( nextTokenIsBranchDelayFiller(branch, tokens.end()) )
			continue;
		moveDeadFallthroughIntoBranchDelaySlot(tokens, branch);
	}
}

bool CodeGenerator::moveDeadFallthroughIntoBranchDelaySlot( std::list<Token>& tokens,
                                                            std::list<Token>::iterator branch ) const
{
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(*branch, branchAccess) )
		return false;
	if( (branchAccess.instructionFlags & VU_INSTR_BRANCH) == 0 )
		return false;
	if( branchAccess.instructionFlags & (VU_INSTR_UNCONDITIONAL_BRANCH
	                                   | VU_INSTR_LINK_BRANCH
	                                   | VU_INSTR_REGISTER_BRANCH) )
		return false;

	std::list<Token>::iterator candidate = branch;
	++candidate;
	if( candidate == tokens.end() )
		return false;
	if( candidate->label().length() != 0 )
		return false;
	if( candidate->flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER
	                          | Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( !isVuEmittableInstruction(*candidate) )
		return false;
	if( !candidate->operand() || candidate->operand()->unit() != Operand::IALU )
		return false;
	if( candidate->operand()->latency() > 1 )
		return false;

	VuTokenResourceAccess candidateAccess;
	if( !buildVuTokenResourceAccess(*candidate, candidateAccess) )
		return false;
	if( candidateAccess.memoryKind != VU_MEMORY_NONE
	    || candidateAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( candidateAccess.branchDelaySlots > 0 )
		return false;
	if( candidateAccess.instructionFlags & (VU_INSTR_BRANCH
	                                      | VU_INSTR_WAIT_Q
	                                      | VU_INSTR_WAIT_P
	                                      | VU_INSTR_WRITES_Q
	                                      | VU_INSTR_WRITES_P
	                                      | VU_INSTR_XGKICK) )
		return false;
	if( candidateAccess.implicitReads != VU_RESOURCE_NONE
	    || candidateAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;
	if( candidateAccess.registerWrites.empty() )
		return false;
	for( std::list<std::string>::const_iterator i = candidateAccess.registerWrites.begin();
	     i != candidateAccess.registerWrites.end(); ++i )
	{
		if( i->size() < 2 || i->compare(0, 2, "VI") != 0 )
			return false;
	}

	std::string targetLabel;
	if( !branchTargetLabel(*branch, targetLabel) )
		return false;

	std::list<Token>::iterator target = tokens.end();
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->label() == targetLabel )
		{
			target = i;
			break;
		}
	}
	if( target == tokens.end() )
		return false;
	if( target->lineNumber() <= branch->lineNumber() )
		return false;
	if( !writesAreDeadFromTarget(candidateAccess.registerWrites, target, tokens.end()) )
		return false;
	// The straight-line walk above cannot see a path, only a file layout. A
	// branch below the target that jumps back ABOVE it re-enters rows the walk
	// never visits, and a forward branch can jump past the write the walk counted
	// as a kill. Either way it answers "dead" for a register the taken path
	// reads, and the delay slot then executes a conditional instruction
	// unconditionally.
	if( !writesAreDeadFromTargetOnEveryPath(candidateAccess.registerWrites, tokens, target) )
		return false;

	Token filler = *candidate;
	filler.setFlags(filler.flags() | Token::BRANCH_DELAY_FILLER);
	tokens.erase(candidate);
	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	tokens.insert(afterBranch, filler);
	return true;
}

bool CodeGenerator::canMoveIntoBranchDelaySlot( const Token& candidate, const Token& branch ) const
{
	if( candidate.label().length() != 0 )
		return false;
	if( candidate.flags() & (Token::PREORDERED | Token::E | Token::D | Token::T | Token::BRANCH_DELAY_FILLER
	                         | Token::SCHEDULED_PAIR_FIRST | Token::SCHEDULED_PAIR_SECOND) )
		return false;
	if( !isVuEmittableInstruction(candidate) )
		return false;
	if( vuTokenBranchDelaySlots(candidate) > 0 )
		return false;
	// Integer ALU, or - with --emit-delay-fillers - a plain STORE as well. SCE's
	// vcl puts stores in delay slots freely: of its 94 filled slots across the 25
	// microprograms of a real engine, 34 hold an `sq` or `isw` against 37 integer
	// ALU ops. A store is the safe half of that - it writes no register, so moving
	// it one word later cannot make a consumer read too early, and crossing only
	// the branch reorders it against no other memory access.
	const bool candidateIsIalu =
	    candidate.operand() && candidate.operand()->unit() == Operand::IALU;
	if( !candidate.operand() )
		return false;
	if( !candidateIsIalu && !vuEmitDelayFillersEnabled() )
		return false;
	if( candidate.operand()->latency() > 1 )
		return false;

	VuTokenResourceAccess candidateAccess;
	VuTokenResourceAccess branchAccess;
	if( !buildVuTokenResourceAccess(candidate, candidateAccess)
	    || !buildVuTokenResourceAccess(branch, branchAccess) )
		return false;

	const bool candidateIsPlainStore =
	    vuEmitDelayFillersEnabled()
	    && candidateAccess.memoryKind == VU_MEMORY_STORE
	    && candidateAccess.memoryFlags == VU_MEMORY_FLAG_NONE;
	if( !candidateIsIalu && !candidateIsPlainStore )
		return false;
	if( candidateAccess.memoryKind != VU_MEMORY_NONE && !candidateIsPlainStore )
		return false;
	if( candidateAccess.memoryFlags != VU_MEMORY_FLAG_NONE )
		return false;
	if( candidateAccess.branchDelaySlots > 0 )
		return false;
	if( candidateAccess.instructionFlags & (VU_INSTR_BRANCH
	                                      | VU_INSTR_WAIT_Q
	                                      | VU_INSTR_WAIT_P
	                                      | VU_INSTR_WRITES_Q
	                                      | VU_INSTR_WRITES_P
	                                      | VU_INSTR_XGKICK) )
		return false;
	if( candidateAccess.implicitReads != VU_RESOURCE_NONE
	    || candidateAccess.implicitWrites != VU_RESOURCE_NONE )
		return false;

	if( intersectsKeys(candidateAccess.registerWrites, branchAccess.registerReads)
	    || intersectsKeys(candidateAccess.registerWrites, branchAccess.registerWrites)
	    || intersectsKeys(candidateAccess.registerReads, branchAccess.registerWrites) )
		return false;

	if( candidateAccess.implicitWrites & (branchAccess.implicitReads | branchAccess.implicitWrites) )
		return false;
	if( branchAccess.implicitWrites & (candidateAccess.implicitReads | candidateAccess.implicitWrites) )
		return false;

	return true;
}

bool CodeGenerator::nextTokenIsBranchDelayFiller( std::list<Token>::iterator token,
                                                  std::list<Token>::iterator end ) const
{
	if( token == end )
		return false;
	std::list<Token>::iterator next = token;
	++next;
	return next != end && (next->flags() & Token::BRANCH_DELAY_FILLER) != 0;
}

bool CodeGenerator::branchTargetLabel( const Token& branch, std::string& label ) const
{
	for( std::list<Token::Argument>::const_iterator i = branch.arguments().begin(); i != branch.arguments().end(); ++i )
	{
		if( i->flags() & Token::Argument::BRANCH )
		{
			if( i->type() != Token::Argument::IMMEDIATE )
				return false;
			label = i->immediate();
			return true;
		}
	}
	return false;
}

bool CodeGenerator::writesAreDeadFromTarget( const std::list<std::string>& writes,
                                             std::list<Token>::iterator target,
                                             std::list<Token>::iterator end ) const
{
	std::list<std::string> remaining = writes;
	for( std::list<Token>::iterator i = target; i != end; ++i )
	{
		VuTokenResourceAccess access;
		if( !buildVuTokenResourceAccess(*i, access) )
			continue;

		if( intersectsKeys(access.registerReads, remaining) )
			return false;

		for( std::list<std::string>::iterator r = remaining.begin(); r != remaining.end(); )
		{
			if( containsKey(access.registerWrites, *r) )
				r = remaining.erase(r);
			else
				++r;
		}
		if( remaining.empty() )
			return true;
	}
	return true;
}

// Backward liveness over the token list's own control-flow graph, asked about
// one point: the branch target. Answering it needs a graph rather than a walk
// because the only unsafe direction is a path the file order does not show -
// which is every backward branch, and every forward branch over a kill.
//
// Two modelling points that are easy to get wrong:
//   * a branch delay slot runs on BOTH paths, so when a filler row is already
//     parked below a branch the control transfer belongs to the FILLER, not to
//     the branch. Giving the branch the transfer would make the filler
//     fall-through-only and hide any read it performs from the taken path.
//   * a successor this cannot name is not an absent successor. A register or
//     link branch, or a target label that is not in this list, means the graph
//     is incomplete, and an incomplete graph may not be used to call anything
//     dead - so those refuse rather than approximate.
bool CodeGenerator::writesAreDeadFromTargetOnEveryPath( const std::list<std::string>& writes,
                                                        std::list<Token>& tokens,
                                                        std::list<Token>::iterator target ) const
{
	if( writes.empty() )
		return true;

	std::vector<Token*> rows;
	std::map<std::string, unsigned int> labels;
	unsigned int targetIndex = 0;
	bool haveTarget = false;
	for( std::list<Token>::iterator i = tokens.begin(); i != tokens.end(); ++i )
	{
		if( i->label().length() != 0 && labels.find(i->label()) == labels.end() )
			labels[i->label()] = (unsigned int)rows.size();
		if( i == target )
		{
			targetIndex = (unsigned int)rows.size();
			haveTarget = true;
		}
		rows.push_back( &*i );
	}
	if( !haveTarget )
		return false;

	const unsigned int rowCount = (unsigned int)rows.size();
	std::vector< std::vector<unsigned int> > successors( rowCount );
	for( unsigned int i = 0; i < rowCount; ++i )
	{
		VuTokenResourceAccess access;
		const bool haveAccess = buildVuTokenResourceAccess( *rows[i], access );
		const bool isBranch = haveAccess && (access.instructionFlags & VU_INSTR_BRANCH) != 0;

		// The row that actually transfers control: the branch itself, or the
		// filler already sitting in its delay slot.
		const Token* transferBranch = NULL;
		unsigned int transferRow = i;
		if( isBranch )
		{
			if( i + 1 < rowCount && (rows[i + 1]->flags() & Token::BRANCH_DELAY_FILLER) )
			{
				successors[i].push_back( i + 1 );
				continue;
			}
			transferBranch = rows[i];
		}
		else if( i > 0 && (rows[i]->flags() & Token::BRANCH_DELAY_FILLER) )
		{
			VuTokenResourceAccess previous;
			if( buildVuTokenResourceAccess( *rows[i - 1], previous )
			    && (previous.instructionFlags & VU_INSTR_BRANCH) )
			{
				transferBranch = rows[i - 1];
				access = previous;
			}
		}

		if( transferBranch )
		{
			if( access.instructionFlags & (VU_INSTR_LINK_BRANCH | VU_INSTR_REGISTER_BRANCH) )
				return false;
			std::string label;
			if( !branchTargetLabel( *transferBranch, label ) )
				return false;
			std::map<std::string, unsigned int>::const_iterator found = labels.find( label );
			if( found == labels.end() )
				return false;
			successors[transferRow].push_back( found->second );
			if( !(access.instructionFlags & VU_INSTR_UNCONDITIONAL_BRANCH)
			    && transferRow + 1 < rowCount )
				successors[transferRow].push_back( transferRow + 1 );
			continue;
		}

		if( i + 1 < rowCount )
			successors[i].push_back( i + 1 );
	}

	for( std::list<std::string>::const_iterator w = writes.begin(); w != writes.end(); ++w )
	{
		std::vector<char> reads( rowCount, 0 );
		std::vector<char> kills( rowCount, 0 );
		std::vector<char> live( rowCount, 0 );
		for( unsigned int i = 0; i < rowCount; ++i )
		{
			VuTokenResourceAccess access;
			if( !buildVuTokenResourceAccess( *rows[i], access ) )
				continue;
			// Read before write, so a read-modify-write row (`iaddi x, x, -1`)
			// is a use and not a kill.
			if( containsKey( access.registerReads, *w )
			    || (access.hasMemoryBase && access.memoryBaseRegister == *w) )
				reads[i] = 1;
			else if( containsKey( access.registerWrites, *w ) )
				kills[i] = 1;
		}

		bool changed = true;
		while( changed )
		{
			changed = false;
			for( unsigned int j = rowCount; j > 0; --j )
			{
				const unsigned int i = j - 1;
				char value = 0;
				if( reads[i] )
					value = 1;
				else if( !kills[i] )
				{
					for( unsigned int s = 0; s < successors[i].size(); ++s )
					{
						if( live[successors[i][s]] )
						{
							value = 1;
							break;
						}
					}
				}
				if( value != live[i] )
				{
					live[i] = value;
					changed = true;
				}
			}
		}

		if( live[targetIndex] )
			return false;
	}

	return true;
}

bool CodeGenerator::tryEmitKnownLoopOptimization( std::list<Token>& tokens,
                                                  std::list<Token>::iterator& token )
{
	if( !m_knownLoopOptimizations )
		return false;

	// Transitional fast paths for known ps2gl loop shapes.  They are kept as
	// performance references while generic scheduling/software-pipelining grows
	// enough to replace them.
	return tryEmitFastNoLightsSoftwarePipelineLoop(tokens, token)
	    || tryEmitFastLitSoftwarePipelineLoop(tokens, token)
	    || tryEmitSceiSoftwarePipelineLoop(tokens, token)
	    || tryEmitPs2glPrimitiveXformSoftwarePipelineLoop(tokens, token)
	    || tryEmitLinearXformSoftwarePipelineLoop(tokens, token)
	    || tryEmitDirLightSpecSoftwarePipelineLoop(tokens, token)
	    || tryEmitDirLightNoSpecSoftwarePipelineLoop(tokens, token)
	    || tryEmitPtLightSpecSoftwarePipelineLoop(tokens, token)
	    || tryEmitPtLightNoSpecSoftwarePipelineLoop(tokens, token)
	    || tryEmitFinalColorSoftwarePipelineLoop(tokens, token);
}

bool CodeGenerator::tryEmitFastNoLightsSoftwarePipelineLoop( std::list<Token>& tokens,
                                                             std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmFastNoLights" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FastNoLightsLoopPipelinePattern pattern;
	if( !collectFastNoLightsLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFastNoLightsSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFastNoLightsLoopPipelinePattern( std::list<Token>::iterator begin,
                                                            std::list<Token>::iterator end,
                                                            FastNoLightsLoopPipelinePattern& pattern )
{
	pattern = FastNoLightsLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveMulax = false;
	bool haveMadday = false;
	bool haveMaddaz = false;
	bool haveMaddw = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveAdcAdd = false;
	bool haveFtoi = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( haveBranch && dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( haveColorStore && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			else if( haveStripLoad && src == pattern.stripAdcReg )
			{
				pattern.newAdcReg = dst;
				pattern.adcImmediate = imm;
				haveAdcAdd = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( haveBranch && base == pattern.inputReg && offset == 0 && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
			}
			else if( haveBranch && base == pattern.inputReg && offset == 2 && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.xformedCopyReg = dst;
				pattern.texOffset = offset;
				haveTexLoad = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ILW) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( haveBranch && base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.outputReg = base;
				pattern.texReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && token.broadcast() == Token::X )
		{
			if( !getRegisterArgKey(token, 1, pattern.row0Reg)
			    || !getRegisterArgKey(token, 2, pattern.vertexReg) )
				return false;
			pattern.mulaxOp = generateOperand(token);
			haveMulax = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Y )
		{
			if( !getRegisterArgKey(token, 1, pattern.row1Reg) )
				return false;
			pattern.maddayOp = generateOperand(token);
			haveMadday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Z )
		{
			if( !getRegisterArgKey(token, 1, pattern.row2Reg) )
				return false;
			pattern.maddazOp = generateOperand(token);
			haveMaddaz = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::W )
		{
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg) )
				return false;
			pattern.maddwOp = generateOperand(token);
			haveMaddw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MFIR) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FTOI4) )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}
	}

	if( pattern.xformedCopyReg.empty() )
		pattern.xformedCopyReg = pattern.texReg;

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveMulax
	    && haveMadday
	    && haveMaddaz
	    && haveMaddw
	    && haveDiv
	    && haveMfir
	    && haveAdcAdd
	    && haveFtoi
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.xformedCopyReg.empty()
	    && !pattern.texReg.empty();
}

void CodeGenerator::emitFastNoLightsSoftwarePipelineLoop( const FastNoLightsLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string color = p.colorReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string xCopy = p.xformedCopyReg;
	const std::string tex = p.texReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(0, in)));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstr(VU_OP_SQ, color + ", " + offsetBase(1, out)));
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_IBEQ, in + ", " + last + ", " + p.epilogOneLabel));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", gs + ", " + x + ", " + x),
	                  vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  vuInstr(VU_OP_SQ, color + ", " + offsetBase(4, out)));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrFields(VU_OP_LQ, "xyz", tex + ", " + offsetBase(-1, in)));
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + strip + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", gs + ", " + gs + ", q"),
	                  vuInstr(VU_OP_IADDIU, out + ", " + out + ", 6"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_IBEQ, in + ", " + last + ", " + p.epilogTwoLabel));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", tex + ", " + tex + ", q"),
	                  vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs),
	                  vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, color + ", " + offsetBase(1, out)));
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", xCopy + ", " + x + ", " + x),
	                  vuInstrFields(VU_OP_SQ, "xyz", tex + ", " + offsetBase(-6, out)));
	emitRawPairedLine(p.mulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  vuInstrFields(VU_OP_LQ, "xyz", tex + ", " + offsetBase(-1, in)));
	emitRawPairedLine(p.maddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-4, out)));
	emitRawPairedLine(p.maddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(p.maddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + strip + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", gs + ", " + xCopy + ", q"),
	                  vuInstr(VU_OP_IADDIU, out + ", " + out + ", 3"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_IBNE, in + ", " + last + ", " + p.mainLabel));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", tex + ", " + tex + ", q"),
	                  vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_SQ, "xyz", tex + ", " + offsetBase(-6, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_LQ, "xyz", tex + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", gs + ", " + x + ", q"),
	                  vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-4, out)));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", tex + ", " + tex + ", q"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + strip + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_SQ, "xyz", tex + ", " + offsetBase(-3, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_B, p.exitLabel));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-1, out)));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", gs + ", " + x + ", " + x),
	                  vuInstrFields(VU_OP_LQ, "xyz", tex + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", gs + ", " + gs + ", q"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + strip + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", tex + ", " + tex + ", q"),
	                  vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_SQ, "xyz", tex + ", " + offsetBase(0, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, gs + ", " + offsetBase(2, out)));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitFastLitSoftwarePipelineLoop( std::list<Token>& tokens,
                                                        std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmFast" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FastLitLoopPipelinePattern pattern;
	if( !collectFastLitLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFastLitSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFastLitLoopPipelinePattern( std::list<Token>::iterator begin,
                                                       std::list<Token>::iterator end,
                                                       FastLitLoopPipelinePattern& pattern )
{
	pattern = FastLitLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveNormalLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveTransformMulax = false;
	bool haveTransformMadday = false;
	bool haveTransformMaddaz = false;
	bool haveTransformMaddw = false;
	bool haveLightDirMulax = false;
	bool haveLightDirMadday = false;
	bool haveLightDirMaddz = false;
	bool haveCosineMax = false;
	bool haveLightColorMulax = false;
	bool haveLightColorMadday = false;
	bool haveLightColorMaddz = false;
	bool haveColorAdd = false;
	bool haveColorMin = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveAdcAdd = false;
	bool haveFtoi = false;
	bool haveTransformMulq = false;
	bool haveTexMulq = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			else if( !pattern.stripAdcReg.empty() && src == pattern.stripAdcReg )
			{
				pattern.newAdcReg = dst;
				pattern.adcImmediate = imm;
				haveAdcAdd = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( base == pattern.inputReg && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				if( offset == 0 )
				{
					pattern.vertexReg = dst;
					pattern.vertexOffset = offset;
					haveVertexLoad = true;
				}
				else if( offset == 1 )
				{
					pattern.normalReg = dst;
					pattern.normalOffset = offset;
					haveNormalLoad = true;
				}
				else if( offset == 2 )
				{
					pattern.texInputReg = dst;
					pattern.texOffset = offset;
					haveTexLoad = true;
				}
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ILW) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 0 )
			{
				pattern.outputReg = base;
				pattern.texOutputReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && token.broadcast() == Token::X )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row0Reg = row;
				pattern.transformMulaxOp = generateOperand(token);
				haveTransformMulax = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir0Reg = row;
				pattern.lightDirMulaxOp = generateOperand(token);
				haveLightDirMulax = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor0Reg = row;
				pattern.lightColorMulaxOp = generateOperand(token);
				haveLightColorMulax = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Y )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row1Reg = row;
				pattern.transformMaddayOp = generateOperand(token);
				haveTransformMadday = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir1Reg = row;
				pattern.lightDirMaddayOp = generateOperand(token);
				haveLightDirMadday = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor1Reg = row;
				pattern.lightColorMaddayOp = generateOperand(token);
				haveLightColorMadday = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Z )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				pattern.transformMaddazOp = generateOperand(token);
				haveTransformMaddaz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::Z )
		{
			std::string dst;
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, row)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				pattern.transformMaddazOp = generateOperand(token);
				haveTransformMaddaz = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir2Reg = row;
				pattern.cosinesReg = dst;
				pattern.lightDirMaddzOp = generateOperand(token);
				haveLightDirMaddz = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor2Reg = row;
				pattern.colorReg = dst;
				pattern.lightColorMaddzOp = generateOperand(token);
				haveLightColorMaddz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::W )
		{
			std::string src;
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			pattern.transformMaddwOp = generateOperand(token);
			haveTransformMaddw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && !pattern.cosinesReg.empty() && src == pattern.cosinesReg )
			{
				pattern.clampedCosinesReg = dst;
				haveCosineMax = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.constantColorReg = src1;
				haveColorAdd = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MINI) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.maxColorReg = src1;
				haveColorMin = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MFIR) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FTOI4) )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULQ) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.xformedReg )
				haveTransformMulq = true;
			else if( src == pattern.texInputReg )
			{
				pattern.texOutputReg = dst;
				haveTexMulq = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveNormalLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveTransformMulax
	    && haveTransformMadday
	    && haveTransformMaddaz
	    && haveTransformMaddw
	    && haveLightDirMulax
	    && haveLightDirMadday
	    && haveLightDirMaddz
	    && haveCosineMax
	    && haveLightColorMulax
	    && haveLightColorMadday
	    && haveLightColorMaddz
	    && haveColorAdd
	    && haveColorMin
	    && haveDiv
	    && haveMfir
	    && haveAdcAdd
	    && haveFtoi
	    && haveTransformMulq
	    && haveTexMulq
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.texInputReg.empty()
	    && !pattern.texOutputReg.empty()
	    && !pattern.lightDir0Reg.empty()
	    && !pattern.lightDir1Reg.empty()
	    && !pattern.lightDir2Reg.empty()
	    && !pattern.cosinesReg.empty()
	    && !pattern.clampedCosinesReg.empty()
	    && !pattern.lightColor0Reg.empty()
	    && !pattern.lightColor1Reg.empty()
	    && !pattern.lightColor2Reg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.constantColorReg.empty()
	    && !pattern.maxColorReg.empty();
}

void CodeGenerator::emitFastLitSoftwarePipelineLoop( const FastLitLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string normal = p.normalReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string texIn = p.texInputReg;
	const std::string texOut = p.texOutputReg;
	const std::string cos = p.cosinesReg;
	const std::string clamped = p.clampedCosinesReg;
	const std::string color = p.colorReg;
	const std::string colorAccum = p.colorAccumReg;
	const std::string colorRaw = p.colorRawReg;
	const std::string xCarry = p.xformedCarryReg;
	const std::string xQ = p.xformedQReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + clamped + ", " + cos + ", " + vf00, vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 0");

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + xCarry + ", " + x);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + clamped + ", " + cos + ", " + vf00,
	                  vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 6");
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + xQ + ", " + xCarry + ", q",
	                  vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + texIn + ", " + offsetBase(-4, in));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + xQ, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + texOut + ", " + texIn + ", q",
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(p.lightDirMulaxOp + " ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x"),
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(p.lightDirMaddayOp + " ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y"),
	                  vuInstrPrefix(VU_OP_SQ) + color + ", " + offsetBase(-8, out));
	emitRawPairedLine(p.lightDirMaddzOp + " " + cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"),
	                  vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"),
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + xCarry + ", " + x);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + clamped + ", " + cos + ", " + vf00,
	                  vuInstrPrefixFields(VU_OP_SQ, "xyz") + texOut + ", " + offsetBase(-9, out));
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"),
	                  vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(-7, out));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + xQ + ", " + xCarry + ", q",
	                  vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(p.lightColorMulaxOp + " ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x"),
	                  vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(p.lightColorMaddayOp + " ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + texIn + ", " + offsetBase(-4, in));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + xQ, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + texOut + ", " + texIn + ", q",
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + xCarry + ", " + x);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + color + ", " + offsetBase(-5, out));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(-4, out));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + xQ + ", " + xCarry + ", q",
	                  vuInstrPrefixFields(VU_OP_SQ, "xyz") + texOut + ", " + offsetBase(-6, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + xQ, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + texOut + ", " + texIn + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + color + ", " + offsetBase(-2, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(-1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + texOut + ", " + offsetBase(-3, out));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.lightColorMaddzOp + " " + colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z"),
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MOVE, "xyz") + xCarry + ", " + x);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + colorAccum + ", " + colorRaw + ", " + p.constantColorReg,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + texIn + ", " + offsetBase(-1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(-3, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + xQ + ", " + xCarry + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + strip + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + color + ", " + colorAccum + ", " + fieldArg(p.maxColorReg, "w"),
	                  vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + xQ, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + texOut + ", " + texIn + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + color + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(2, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + texOut + ", " + offsetBase(0, out));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitSceiSoftwarePipelineLoop( std::list<Token>& tokens,
                                                     std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( m_name != "vsmSCEI" )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	SceiLoopPipelinePattern pattern;
	if( !collectSceiLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitSceiSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectSceiLoopPipelinePattern( std::list<Token>::iterator begin,
                                                    std::list<Token>::iterator end,
                                                    SceiLoopPipelinePattern& pattern )
{
	pattern = SceiLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveNormalLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveTransformMulax = false;
	bool haveTransformMadday = false;
	bool haveTransformMaddaz = false;
	bool haveTransformMaddw = false;
	bool haveLightDirMulax = false;
	bool haveLightDirMadday = false;
	bool haveLightDirMaddz = false;
	bool haveCosineMax = false;
	bool haveLightColorMulax = false;
	bool haveLightColorMadday = false;
	bool haveLightColorMaddz = false;
	bool haveColorAdd = false;
	bool haveColorMin = false;
	bool haveGsAdd = false;
	bool haveClipMul = false;
	bool haveClipw = false;
	bool haveFcand = false;
	bool haveAdcOr = false;
	bool haveAdcAdd = false;
	bool haveDiv = false;
	bool haveMfir = false;
	bool haveFtoi = false;
	bool haveTransformMulq = false;
	bool haveTexMulq = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;

			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
				}
				else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
				}
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;

			if( base == pattern.inputReg && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				if( offset == 0 )
				{
					pattern.vertexReg = dst;
					pattern.vertexOffset = offset;
					haveVertexLoad = true;
				}
				else if( offset == 1 )
				{
					pattern.normalReg = dst;
					pattern.normalOffset = offset;
					haveNormalLoad = true;
				}
				else if( offset == 2 )
				{
					pattern.texInputReg = dst;
					pattern.texOffset = offset;
					haveTexLoad = true;
				}
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ILW) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 0 )
			{
				pattern.outputReg = base;
				pattern.texOutputReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
			}
			else if( offset == 1 )
			{
				pattern.outputReg = base;
				pattern.colorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
			}
			else if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && token.broadcast() == Token::X )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row0Reg = row;
				if( !captureVuInstructionForm(token, pattern.transformMulaxInstr) )
					return false;
				haveTransformMulax = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir0Reg = row;
				if( !captureVuInstructionForm(token, pattern.lightDirMulaxInstr) )
					return false;
				haveLightDirMulax = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor0Reg = row;
				if( !captureVuInstructionForm(token, pattern.lightColorMulaxInstr) )
					return false;
				haveLightColorMulax = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Y )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row1Reg = row;
				if( !captureVuInstructionForm(token, pattern.transformMaddayInstr) )
					return false;
				haveTransformMadday = true;
			}
			else if( src == pattern.normalReg )
			{
				pattern.lightDir1Reg = row;
				if( !captureVuInstructionForm(token, pattern.lightDirMaddayInstr) )
					return false;
				haveLightDirMadday = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor1Reg = row;
				if( !captureVuInstructionForm(token, pattern.lightColorMaddayInstr) )
					return false;
				haveLightColorMadday = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Z )
		{
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 1, row) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.vertexReg )
			{
				pattern.row2Reg = row;
				if( !captureVuInstructionForm(token, pattern.transformMaddazInstr) )
					return false;
				haveTransformMaddaz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::Z )
		{
			std::string dst;
			std::string row;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, row)
			    || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src == pattern.normalReg )
			{
				pattern.lightDir2Reg = row;
				pattern.cosinesReg = dst;
				if( !captureVuInstructionForm(token, pattern.lightDirMaddzInstr) )
					return false;
				haveLightDirMaddz = true;
			}
			else if( src == pattern.clampedCosinesReg )
			{
				pattern.lightColor2Reg = row;
				pattern.colorReg = dst;
				if( !captureVuInstructionForm(token, pattern.lightColorMaddzInstr) )
					return false;
				haveLightColorMaddz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::W )
		{
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg) )
				return false;
			if( !captureVuInstructionForm(token, pattern.transformMaddwInstr) )
				return false;
			haveTransformMaddw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && !pattern.cosinesReg.empty() && src == pattern.cosinesReg )
			{
				pattern.clampedCosinesReg = dst;
				haveCosineMax = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.xformedReg.empty() && src0 == pattern.xformedReg )
			{
				pattern.clipReg = dst;
				pattern.clipScalesReg = src1;
				haveClipMul = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.xformedReg.empty() && src0 == pattern.xformedReg )
			{
				pattern.gsReg = dst;
				pattern.gsOffsetsReg = src1;
				haveGsAdd = true;
			}
			else if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.constantColorReg = src1;
				haveColorAdd = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MINI) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src0 == pattern.colorReg )
			{
				pattern.maxColorReg = src1;
				haveColorMin = true;
			}
			continue;
		}

		if( isVuClipw(mnemonic) )
		{
			std::string clip;
			std::string scales;
			if( !getRegisterArgKey(token, 0, clip) || !getRegisterArgKey(token, 1, scales) )
				return false;
			if( clip == pattern.clipReg )
			{
				pattern.clipScalesReg = scales;
				haveClipw = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FCAND) )
		{
			std::string imm;
			if( getImmediateArg(token, 1, imm) )
				pattern.clipImmediate = imm;
			haveFcand = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_IOR) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				continue;
			if( !pattern.stripAdcReg.empty()
			    && ((src0 == "VI01" && src1 == pattern.stripAdcReg)
			        || (src1 == "VI01" && src0 == pattern.stripAdcReg)) )
			{
				pattern.newAdcReg = dst;
				haveAdcOr = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MFIR) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.gsReg = dst;
			pattern.newAdcReg = src;
			haveMfir = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FTOI4) )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULQ) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.xformedReg )
				haveTransformMulq = true;
			else if( src == pattern.texInputReg )
			{
				pattern.texOutputReg = dst;
				haveTexMulq = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveNormalLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveTransformMulax
	    && haveTransformMadday
	    && haveTransformMaddaz
	    && haveTransformMaddw
	    && haveLightDirMulax
	    && haveLightDirMadday
	    && haveLightDirMaddz
	    && haveCosineMax
	    && haveLightColorMulax
	    && haveLightColorMadday
	    && haveLightColorMaddz
	    && haveColorAdd
	    && haveColorMin
	    && haveGsAdd
	    && haveClipMul
	    && haveClipw
	    && haveFcand
	    && haveAdcOr
	    && haveAdcAdd
	    && haveDiv
	    && haveMfir
	    && haveFtoi
	    && haveTransformMulq
	    && haveTexMulq
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.xformedQReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.gsOffsetsReg.empty()
	    && !pattern.texInputReg.empty()
	    && !pattern.texOutputReg.empty()
	    && !pattern.clipReg.empty()
	    && !pattern.clipScalesReg.empty()
	    && !pattern.lightDir0Reg.empty()
	    && !pattern.lightDir1Reg.empty()
	    && !pattern.lightDir2Reg.empty()
	    && !pattern.cosinesReg.empty()
	    && !pattern.clampedCosinesReg.empty()
	    && !pattern.lightColor0Reg.empty()
	    && !pattern.lightColor1Reg.empty()
	    && !pattern.lightColor2Reg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.colorRawReg.empty()
	    && !pattern.constantColorReg.empty()
	    && !pattern.maxColorReg.empty();
}

void CodeGenerator::emitSceiSoftwarePipelineLoop( const SceiLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string normal = p.normalReg;
	const std::string strip = p.stripAdcReg;
	const std::string adc = p.newAdcReg;
	const std::string x = p.xformedReg;
	const std::string xQ = p.xformedQReg;
	const std::string gs = p.gsReg;
	const std::string texIn = p.texInputReg;
	const std::string texOut = p.texOutputReg;
	const std::string clip = p.clipReg;
	const std::string cos = p.cosinesReg;
	const std::string clamped = p.clampedCosinesReg;
	const std::string color = p.colorReg;
	const std::string colorRaw = p.colorRawReg;
	const std::string vf00 = "VF00";
	const std::string vi01 = "VI01";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(vuInstr(p.transformMulaxInstr, "ACC, " + p.row0Reg + ", " + fieldArg(vert, "x")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMaddayInstr, "ACC, " + p.row1Reg + ", " + fieldArg(vert, "y")),
	                  vuInstrFields(VU_OP_LQ, "xyz", normal + ", " + offsetBase(1, in)));
	emitRawPairedLine(vuInstr(p.transformMaddazInstr, "ACC, " + p.row2Reg + ", " + fieldArg(vert, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMaddwInstr, x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMulaxInstr, "ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMaddayInstr, "ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMaddzInstr, cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z")),
	                  vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", clamped + ", " + cos + ", " + vf00),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_IBEQ, in + ", " + last + ", " + p.epilogOneLabel));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", xQ + ", " + x + ", q"),
	                  vuInstr(VU_OP_IADDIU, out + ", " + out + ", 0"));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(vuInstr(p.lightColorMulaxInstr, "ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x")),
	                  vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(vuInstr(p.lightColorMaddayInstr, "ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightColorMaddzInstr, colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_MUL, "xyz", clip + ", " + xQ + ", " + p.clipScalesReg), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMulaxInstr, "ACC, " + p.row0Reg + ", " + fieldArg(vert, "x")),
	                  vuInstrFields(VU_OP_LQ, "xyz", normal + ", " + offsetBase(1, in)));
	emitRawPairedLine(vuInstr(p.transformMaddayInstr, "ACC, " + p.row1Reg + ", " + fieldArg(vert, "y")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMaddazInstr, "ACC, " + p.row2Reg + ", " + fieldArg(vert, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMaddwInstr, x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMulaxInstr, "ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMaddayInstr, "ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMaddzInstr, cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z")),
	                  vuInstrFields(VU_OP_LQ, "xyz", texIn + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_CLIPW, "xyz", fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w")),
	                  vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", gs + ", " + xQ + ", " + p.gsOffsetsReg),
	                  vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", color + ", " + colorRaw + ", " + p.constantColorReg),
	                  vuInstr(VU_OP_IADDIU, out + ", " + out + ", 6"));
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", clamped + ", " + cos + ", " + vf00),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", texOut + ", " + texIn + ", q"),
	                  vuInstr(VU_OP_FCAND, vi01 + ", " + p.clipImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs),
	                  vuInstr(VU_OP_IOR, adc + ", " + vi01 + ", " + strip));
	emitRawPairedLine(vuInstrBroadcastFields(VU_OP_MINI, "w", "xyz", color + ", " + color + ", " + fieldArg(p.maxColorReg, "w")),
	                  vuInstr(VU_OP_IBEQ, in + ", " + last + ", " + p.epilogTwoLabel));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", xQ + ", " + x + ", q"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + adc + ", " + p.adcImmediate));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstr(p.lightColorMulaxInstr, "ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x")),
	                  vuInstrFields(VU_OP_LQ, "xyz", vert + ", " + offsetBase(0, in)));
	emitRawPairedLine(vuInstr(p.lightColorMaddayInstr, "ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y")),
	                  vuInstrFields(VU_OP_LQ, "xyz", normal + ", " + offsetBase(1, in)));
	emitRawPairedLine(vuInstr(p.lightColorMaddzInstr, colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z")),
	                  vuInstr(VU_OP_IADDIU, out + ", " + out + ", 3"));
	emitRawPairedLine(vuInstrFields(VU_OP_MUL, "xyz", clip + ", " + xQ + ", " + p.clipScalesReg),
	                  vuInstr(VU_OP_SQ, color + ", " + offsetBase(-8, out)));
	emitRawPairedLine(vuInstr(p.transformMulaxInstr, "ACC, " + p.row0Reg + ", " + fieldArg(vert, "x")),
	                  vuInstrFields(VU_OP_SQ, "xyz", texOut + ", " + offsetBase(-9, out)));
	emitRawPairedLine(vuInstr(p.transformMaddayInstr, "ACC, " + p.row1Reg + ", " + fieldArg(vert, "y")),
	                  vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstr(p.transformMaddazInstr, "ACC, " + p.row2Reg + ", " + fieldArg(vert, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.transformMaddwInstr, x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMulaxInstr, "ACC, " + p.lightDir0Reg + ", " + fieldArg(normal, "x")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightDirMaddayInstr, "ACC, " + p.lightDir1Reg + ", " + fieldArg(normal, "y")),
	                  vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-7, out)));
	emitRawPairedLine(vuInstr(p.lightDirMaddzInstr, cos + ", " + p.lightDir2Reg + ", " + fieldArg(normal, "z")),
	                  vuInstrFields(VU_OP_LQ, "xyz", texIn + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_CLIPW, "xyz", fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w")),
	                  vuInstr(VU_OP_DIV, std::string("q, ") + fieldArg(vf00, "w") + ", " + fieldArg(x, "w")));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", gs + ", " + xQ + ", " + p.gsOffsetsReg),
	                  vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", color + ", " + colorRaw + ", " + p.constantColorReg), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_MAX, "xyz", clamped + ", " + cos + ", " + vf00),
	                  vuInstr(VU_OP_IADDIU, in + ", " + in + ", 3"));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", texOut + ", " + texIn + ", q"),
	                  vuInstr(VU_OP_FCAND, vi01 + ", " + p.clipImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs),
	                  vuInstr(VU_OP_IOR, adc + ", " + vi01 + ", " + strip));
	emitRawPairedLine(vuInstrBroadcastFields(VU_OP_MINI, "w", "xyz", color + ", " + color + ", " + fieldArg(p.maxColorReg, "w")),
	                  vuInstr(VU_OP_IBNE, in + ", " + last + ", " + p.mainLabel));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", xQ + ", " + x + ", q"),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + adc + ", " + p.adcImmediate));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightColorMulaxInstr, "ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x")),
	                  vuInstrFields(VU_OP_SQ, "xyz", texOut + ", " + offsetBase(-6, out)));
	emitRawPairedLine(vuInstr(p.lightColorMaddayInstr, "ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y")),
	                  vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstrFields(VU_OP_MUL, "xyz", clip + ", " + xQ + ", " + p.clipScalesReg),
	                  vuInstr(VU_OP_SQ, color + ", " + offsetBase(-5, out)));
	emitRawPairedLine(vuInstr(p.lightColorMaddzInstr, colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", gs + ", " + xQ + ", " + p.gsOffsetsReg),
	                  vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-4, out)));
	emitRawPairedLine(vuInstrFields(VU_OP_CLIPW, "xyz", fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w")),
	                  vuInstrFields(VU_OP_LQ, "xyz", texIn + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", color + ", " + colorRaw + ", " + p.constantColorReg),
	                  vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", texOut + ", " + texIn + ", q"),
	                  vuInstr(VU_OP_FCAND, vi01 + ", " + p.clipImmediate));
	emitRawPairedLine(vuInstrBroadcastFields(VU_OP_MINI, "w", "xyz", color + ", " + color + ", " + fieldArg(p.maxColorReg, "w")),
	                  vuInstr(VU_OP_IOR, adc + ", " + vi01 + ", " + strip));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + adc + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_SQ, "xyz", texOut + ", " + offsetBase(-3, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, color + ", " + offsetBase(-2, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_B, p.exitLabel));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, gs + ", " + offsetBase(-1, out)));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightColorMulaxInstr, "ACC, " + p.lightColor0Reg + ", " + fieldArg(clamped, "x")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_MUL, "xyz", clip + ", " + xQ + ", " + p.clipScalesReg), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightColorMaddayInstr, "ACC, " + p.lightColor1Reg + ", " + fieldArg(clamped, "y")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(p.lightColorMaddzInstr, colorRaw + ", " + p.lightColor2Reg + ", " + fieldArg(clamped, "z")), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_CLIPW, "xyz", fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w")),
	                  vuInstrFields(VU_OP_LQ, "xyz", texIn + ", " + offsetBase(-1, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", gs + ", " + xQ + ", " + p.gsOffsetsReg),
	                  vuInstrFields(VU_OP_ILW, "w", strip + ", " + offsetBase(-3, in)));
	emitRawPairedLine(vuInstrFields(VU_OP_ADD, "xyz", color + ", " + colorRaw + ", " + p.constantColorReg), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrFields(VU_OP_MULQ, "xyz", texOut + ", " + texIn + ", q"),
	                  vuInstr(VU_OP_FCAND, vi01 + ", " + p.clipImmediate));
	emitRawPairedLine(vuInstrFields(VU_OP_FTOI4, "xyz", gs + ", " + gs),
	                  vuInstr(VU_OP_IOR, adc + ", " + vi01 + ", " + strip));
	emitRawPairedLine(vuInstrBroadcastFields(VU_OP_MINI, "w", "xyz", color + ", " + color + ", " + fieldArg(p.maxColorReg, "w")),
	                  vuInstr(VU_OP_IADDIU, adc + ", " + adc + ", " + p.adcImmediate));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_MFIR, "w", gs + ", " + adc));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrFields(VU_OP_SQ, "xyz", texOut + ", " + offsetBase(0, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, color + ", " + offsetBase(1, out)));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_SQ, gs + ", " + offsetBase(2, out)));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitPs2glPrimitiveXformSoftwarePipelineLoop( std::list<Token>& tokens,
                                                                    std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	const bool triTemplate =
	    m_name == "vsmGeneralNoSpecTri"
	    || m_name == "vsmGeneralTri"
	    || m_name == "vsmGeneralPVDiffTri";
	const bool quadTemplate = m_name == "vsmGeneralNoSpecQuad";
	const bool pvDiffQuadTemplate = false;
	const bool indexedTemplate = m_name == "vsmIndexed";
	if( !triTemplate && !quadTemplate && !pvDiffQuadTemplate && !indexedTemplate )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	if( triTemplate )
		emitPs2glTriXformSoftwarePipelineLoop(m_name == "vsmGeneralPVDiffTri");
	else if( quadTemplate )
		emitPs2glQuadXformSoftwarePipelineLoop();
	else if( pvDiffQuadTemplate )
		emitPs2glPvDiffQuadXformSoftwarePipelineLoop();
	else
		emitPs2glIndexedXformSoftwarePipelineLoop();
	token = afterBranch;
	return true;
}

void CodeGenerator::emitPs2glTriXformSoftwarePipelineLoop( bool pvDiff )
{
	const std::string mainLabel = "xform_loop_lid__MAIN_LOOP";
	const std::string epiLabel = "xform_loop_lid__EPI0";
	const std::string exitLabel = "xform_loop_lid__EXIT_POINT";
	const long inputStep = pvDiff ? 12 : 9;
	const long secondVertexOffset = pvDiff ? 4 : 3;
	const long thirdVertexOffset = pvDiff ? 8 : 6;
	const long secondTexOffset = pvDiff ? 6 : 5;
	const long thirdTexOffset = pvDiff ? 10 : 8;

	m_codeLines.push_back("xform_loop_lid__ENTRY_POINT:");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI08, VI07, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI07, VI06, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI06, VI05, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + "VF06, VF07, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_LOI) + "0x45000000");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF07, VF00, i", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, " + offsetBase(secondVertexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF08x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF08y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF08z", vuInstrPrefix(VU_OP_IADD) + "VI06, VI05, VI00");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF15, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF15w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, " + offsetBase(thirdVertexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF14, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF08x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, " + offsetBase(secondTexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF08y", vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF08z", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF14w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF15, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF07, q", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI03, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF12, VF04, VF00w", vuInstrPrefix(VU_OP_IADDIU) + "VI10, VI04, 0");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF10, VF08, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF15, 2(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF07, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF14, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF13, VF08, VF06", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF10, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF10, VF10", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, " + offsetBase(thirdTexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF15, q", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF14, VF07, VF08", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF16, VF07, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI09, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF10, VF07, VF05", vuInstrPrefix(VU_OP_SQ) + "VF10, 5(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF12, q", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, " + integerText(inputStep));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF11, q", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF16xyz, VF07w", vuInstrPrefix(VU_OP_IBEQ) + "VI03, VI06, " + epiLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF13xyz, VF07w", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI04, 0");

	m_codeLines.push_back(mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF12, VF10", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, " + offsetBase(secondVertexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF10, VF07, VF08", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF07, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI10, 9");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF13, VF14, VF08w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF11x", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 6(VI09)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF11y", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF12, VI08");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF11z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF15, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, " + offsetBase(thirdVertexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefix(VU_OP_SQ) + "VF12, 2(VI09)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstrPrefix(VU_OP_IADDIU) + "VI11, VI03, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF14, VF04, VF00w", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF15w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF11x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF16, " + offsetBase(secondTexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF11y", vuInstrPrefix(VU_OP_IADDIU) + "VI10, VI04, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF11z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF12, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, " + offsetBase(thirdTexOffset, "VI03"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF13xyz, VF10xyz", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF10, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF15, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF14w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF16, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF15, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF00xyz, VF10xyz, VF13xyz", vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI03, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF13, VF08, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF10, VF08, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF17, VF07, VF05", vuInstrPrefix(VU_OP_FMAND) + "VI01, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF14, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF15, q", vuInstrPrefix(VU_OP_IOR) + "VI01, VI03, VI01");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF10, VF10", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI11, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF15, VF17", vuInstrPrefix(VU_OP_IADDIU) + "VI11, VI01, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF14, VF07, VF08", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF15, VI11");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF16, VF07, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF10, VF07, VF05", vuInstrPrefix(VU_OP_SQ) + "VF10, 5(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF12, q", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, " + integerText(inputStep));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF11, q", vuInstrPrefix(VU_OP_SQ) + "VF15, 8(VI09)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF16xyz, VF07w", vuInstrPrefix(VU_OP_IBNE) + "VI03, VI06, " + mainLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF13xyz, VF07w", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI04, 0");

	m_codeLines.push_back(epiLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF09, VF10", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF10, VF07, VF08", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF07, VF06", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF14, VF14, VF08w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF07, VF05", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF09, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 6(VI09)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF14xyz, VF10xyz", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF11xyz, VF10xyz, VF14xyz", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF09, 2(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI02, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ABS, "xyz") + "VF00, VF11", vuInstrPrefix(VU_OP_FMAND) + "VI07, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + "VI02, VI02, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI02, VI02, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF15, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF15, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF15, 8(VI09)");

	m_codeLines.push_back(exitLabel + ":");
}

void CodeGenerator::emitPs2glQuadXformSoftwarePipelineLoop()
{
	const std::string mainLabel = "xform_loop_lid__MAIN_LOOP";
	const std::string epi0Label = "xform_loop_lid__EPI0";
	const std::string epi1Label = "xform_loop_lid__EPI1";
	const std::string exitLabel = "xform_loop_lid__EXIT_POINT";
	const std::string fallbackLabel = "xform_loop_lid__FALLBACK_SHORT";
	const std::string doneLabel = "xform_loop_lid__AFTER_FALLBACK";

	m_codeLines.push_back("xform_loop_lid__ENTRY_POINT:");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUB) + "VI01, VI05, VI03");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + "VI01, VI01, 384");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBLEZ) + "VI01, " + fallbackLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI08, VI07, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI07, VI06, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI06, VI05, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_LOI) + "0x45000000");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF20x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF20y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 6(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF20z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF20, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF08x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF08y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF08z", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF20w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF13, 3(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF14, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF13x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF13y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF13z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF13, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + "VI02, 76(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF13w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF15, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF20, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF06, 76(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCSET) + "0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF07, VF00, i", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF15, q", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF07, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF10, VF08, VF06", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 5(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF13, VF13, q", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI03, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF11, VF08, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF17, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF10xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF07, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF12, VF08, VF13", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI01, 12");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF13, VF06", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF14w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF08, VF13, VF05", vuInstrPrefix(VU_OP_IBEQ) + "VI03, VI06, " + epi1Label);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF16, q", vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");

	m_codeLines.push_back("xform_loop_lid__PRO1:");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF19, VF20, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF17, VF17, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF18, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF13, VF16, VF13", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF16, VF05", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF20x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF20y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF22, 6(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF20z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF20, VF04, VF00w", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI03, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF16, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI04, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF22x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF22y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF25, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF22z", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF20w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF24, 3(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF18, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF25, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF23, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF14, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF17, 6(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF22, VF20, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF24x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 5(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF24y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF25, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF24z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF19, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF24, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF11", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF19, VF22, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF24w");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF10, VF12, VF08w", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF12, VF18, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF19xyz, VF07w", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI01, 12");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF25, q", vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF10xyz, VF13xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF00xyz, VF13xyz, VF10xyz", vuInstrPrefix(VU_OP_SQ) + "VF11, 2(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF18, VF08", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF13, VF24, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF18, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF19, VF12", vuInstrPrefix(VU_OP_IAND) + "VI01, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF23, q", vuInstrPrefix(VU_OP_FMAND) + "VI10, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF11, VF22, VF05", vuInstrPrefix(VU_OP_IOR) + "VI01, VI01, VI10");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF12, VF22, VF13", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI01, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF13, VF06", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF19, VI01");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF08, VF13, VF05", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF14w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF22, VF07", vuInstrPrefix(VU_OP_IBEQ) + "VI03, VI06, " + epi0Label);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF16, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF22, VI01");

	m_codeLines.push_back(mainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF19, 8(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF19, VF20, q", vuInstrPrefix(VU_OP_SQ) + "VF18, 5(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF11", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF13, VF16, VF13", vuInstrPrefix(VU_OP_SQ) + "VF22, 11(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF17, VF17, q", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI09, 12");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF16, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF18, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF22, 6(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF16, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI03, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF20x", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI04, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF20y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF20z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF20, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF22x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF22y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF25, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF22z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF20w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF24, 3(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF18, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF25, 7(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF23, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF14, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF17, 6(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF24x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF22, VF20, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 5(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF24y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF24z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF25, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF24, VF04, VF00w", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI01, 12");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF19, VF22, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF19, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF10, VF12, VF08w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF24w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF19xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF12, VF18, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF25, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF10xyz, VF13xyz", vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF18, VF08", vuInstrPrefix(VU_OP_IAND) + "VI01, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF00xyz, VF13xyz, VF10xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF13, VF24, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF23, q", vuInstrPrefix(VU_OP_SQ) + "VF11, 2(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF11, VF22, VF05", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF18, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF19, VF12", vuInstrPrefix(VU_OP_FMAND) + "VI10, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF12, VF22, VF13", vuInstrPrefix(VU_OP_IOR) + "VI01, VI01, VI10");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF13, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI01, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF08, VF13, VF05", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF19, VI01");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF14w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF22, VF07", vuInstrPrefix(VU_OP_IBNE) + "VI03, VI06, " + mainLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF16, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF22, VI01");

	m_codeLines.push_back(epi0Label + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF19, VF20, q", vuInstrPrefix(VU_OP_SQ) + "VF19, 8(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF17, VF17, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI08");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF18, VF14, q", vuInstrPrefix(VU_OP_SQ) + "VF18, 5(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefix(VU_OP_SQ) + "VF22, 11(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF13, VF16, VF13", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI09, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF18, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 12(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF16, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF19, 21(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF10, VF12, VF08w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 15(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF17, 18(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 16(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 19(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF10xyz, VF13xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 13(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF00xyz, VF13xyz, VF10xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 22(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF18, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF12, VF18, VF05", vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF11", vuInstrPrefix(VU_OP_IAND) + "VI02, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF16, VF05", vuInstrPrefix(VU_OP_FMAND) + "VI07, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF18, VF08", vuInstrPrefix(VU_OP_IOR) + "VI02, VI02, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF19, VF12", vuInstrPrefix(VU_OP_IADDIU) + "VI02, VI02, 0x7fff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF19, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF22, VF07", vuInstrPrefix(VU_OP_SQ) + "VF11, 14(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF22, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF18, 17(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF19, 20(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF22, 23(VI04)");

	m_codeLines.push_back(epi1Label + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF20, VF20, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF17, VF17, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF14, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF11", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF13, VF16, VF13", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF10, VF12, VF08w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF14, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF16, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF10xyz, VF13xyz", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF18xyz, VF13xyz, VF10xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF20, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 2(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ABS, "xyz") + "VF00, VF18", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF08, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF12, VF14, VF05", vuInstrPrefix(VU_OP_FMAND) + "VI07, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF16, VF05", vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI02, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF08, VF08", vuInstrPrefix(VU_OP_IOR) + "VI02, VI02, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF12", vuInstrPrefix(VU_OP_IADDIU) + "VI02, VI02, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF07, VF07", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF07, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF17, 6(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF08, 5(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 8(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF07, 11(VI04)");

	m_codeLines.push_back(exitLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + doneLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	emitPs2glQuadXformScalarFallbackLoop();

	m_codeLines.push_back(doneLabel + ":");
}

void CodeGenerator::emitPs2glQuadXformScalarFallbackLoop()
{
	const std::string fallbackLabel = "xform_loop_lid__FALLBACK_SHORT";

	m_codeLines.push_back(fallbackLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 0(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF10, 2(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF11, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF13, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "xyzw") + "ACC, VF01, VF08x", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "xyzw") + "ACC, VF02, VF08y", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "z", "xyzw") + "ACC, VF03, VF08z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "w", "xyzw") + "VF08, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF08w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF08, q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF11, VF08, VF05", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF11, VF11", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF12, VF10, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF10, VF08, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF10, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF12, 0(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF10, 3(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 5(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 2(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "xyzw") + "ACC, VF01, VF10x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "xyzw") + "ACC, VF02, VF10y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "z", "xyzw") + "ACC, VF03, VF10z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "w", "xyzw") + "VF10, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF10w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF10, q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF10, VF05", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF13, VF13", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF14, VF12, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF12, VF10, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF12, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF14, 3(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 9(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 11(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF13, 5(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "xyzw") + "ACC, VF01, VF12x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "xyzw") + "ACC, VF02, VF12y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "z", "xyzw") + "ACC, VF03, VF12z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "w", "xyzw") + "VF12, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF12, VF12, q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF15, VF12, VF05", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF15, VF15", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF14, VF12, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 6(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF14, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 6(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "xyzw") + "ACC, VF01, VF12x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "xyzw") + "ACC, VF02, VF12y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "z", "xyzw") + "ACC, VF03, VF12z", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "w", "xyzw") + "VF12, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF12, VF12, q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF17, VF12, VF05", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF17, VF17", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF14, VF12, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF14, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF14, VF08, VF10", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF08, VF12, VF10", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF10, VF14, VF06w", vuInstrPrefix(VU_OP_FCAND) + "VI01, 0x0ffffff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI01, VI01, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACC, VF10, VF08", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF12, VF08, VF10", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 7(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF10, 10(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FMAND) + "VI08, VI06");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + "VI09, VI01, VI08");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI09, 0x7fff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF15, VI09");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF17, VI09");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF08, 10(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 7(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 12");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF15, 8(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF17, 11(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + "VI03, VI05, " + fallbackLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI04, 12");
}

void CodeGenerator::emitPs2glPvDiffQuadXformSoftwarePipelineLoop()
{
	const std::string mainLabel = "xform_loop_lid__MAIN_LOOP";
	const std::string epiLabel = "xform_loop_lid__EPI0";
	const std::string exitLabel = "xform_loop_lid__EXIT_POINT";

	m_codeLines.push_back("xform_loop_lid__ENTRY_POINT:");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI08, VI07, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI07, VI06, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI06, VI05, VI00");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + "VF06, VF07, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_LOI) + "0x45000000");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF07x", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF07y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF07z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF20, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF22, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF20x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF15, 12(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF20y", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF22w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF20z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 15(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF20, VF04, VF00w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF15x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 4(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF15y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF15z", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF12, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF15, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF20w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 13(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF10, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF12, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF12, 13(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF13, VF22, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF10, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + "VI02, 76(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF07, VF00, i", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF18, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF13, VF06", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 6(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF20, VF20, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF11, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF13, VF05", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF15w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF12, q", vuInstrPrefix(VU_OP_FCSET) + "0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF20, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF07, 15(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF12, VF18, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF22, VF13, VF16", vuInstrPrefix(VU_OP_IADDIU) + "VI08, VI08, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF17, VF16, VF06", vuInstrPrefixFields(VU_OP_LQ, "w") + "VF08, 0(VI00)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF15, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF16, VF05", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI03, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF20, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI01, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF17xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 14(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF17, VF08, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI01, 0");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF22, VF22, VF08w", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 16");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF20, VF20, VF16", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF08, VF08, VF05", vuInstrPrefix(VU_OP_IBEQ) + "VI03, VI06, " + epiLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF17xyz, VF07w", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF10, VI08");

	m_codeLines.push_back(mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF19, VF14", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF16, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF22xyz, VF20xyz", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 8(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF00xyz, VF20xyz, VF22xyz", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF21, 12(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF20, VF18, q", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI03, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF16x", vuInstrPrefix(VU_OP_IADDIU) + "VI10, VI04, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF16y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 4(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF16z", vuInstrPrefix(VU_OP_FMAND) + "VI09, VI07");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF22, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF16, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF17x", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 15(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF17y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 13(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF17z", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF19, VI08");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF17, VF04, VF00w", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF22w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF21x", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF23, 11(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF21y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF23, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF21z", vuInstrPrefix(VU_OP_SQ) + "VF19, 2(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF20, 6(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF10, VF13", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF15, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF11, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF13, VF22, q", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF12, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF14x", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF17w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF14y", vuInstrPrefix(VU_OP_SQ) + "VF10, 5(VI04)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF14z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF10, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF19, VF13, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI11, VI10, 0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF12, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF23, 13(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF13, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF18, 9(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF10, VF10, q", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, 10(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF19xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 15(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF17, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF12w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI10, VI01, 0");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF19, VF07", vuInstrPrefix(VU_OP_IAND) + "VI12, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF07, VF16, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 6(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI11, 0");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF11, q", vuInstrPrefix(VU_OP_IOR) + "VI09, VI12, VI09");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF17, VF12, q", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF15w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI09, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF12, VF18, q", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF20, VI09");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF20, VF08", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF19, VI09");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF22, VF13, VF17", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF21, VF17, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF17, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF18, 14(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF15, q", vuInstrPrefix(VU_OP_SQ) + "VF19, 11(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF20, 8(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF21xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF15, VF16, VF06", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF19, VF08, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI01, 12");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF08, VF08, VF05", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI10, 0");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + "VF22, VF22, VF08w", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 16");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + "VF20, VF16, VF17", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF19xyz, VF07w", vuInstrPrefix(VU_OP_IBNE) + "VI03, VI06, " + mainLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF10, VI08");

	m_codeLines.push_back(epiLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, VF22xyz, VF20xyz", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF10, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + "VF16xyz, VF20xyz, VF22xyz", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF05, VI08");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF05, VF14", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF11, 9(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF20, VF18, q", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF12, 3(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF15xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 10(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ABS, "xyz") + "VF00, VF16", vuInstrPrefix(VU_OP_FMAND) + "VI07, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF05, 2(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF20, 6(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 16777215");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI02, VI01, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + "VI02, VI02, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF20, VF08", vuInstrPrefix(VU_OP_IADDIU) + "VI02, VI02, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF07, VF07", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF20, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF07, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 4(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 1(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF10, VF13", vuInstrPrefix(VU_OP_SQ) + "VF20, 8(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF07, 11(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 7(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF10, 5(VI04)");

	m_codeLines.push_back(exitLabel + ":");
}

void CodeGenerator::emitPs2glIndexedXformSoftwarePipelineLoop()
{
	const std::string mainLabel = "xform_loop_lid__MAIN_LOOP";
	const std::string pro1Label = "xform_loop_lid__PRO1";
	const std::string epi0Label = "xform_loop_lid__EPI0";
	const std::string epi1Label = "xform_loop_lid__EPI1";
	const std::string exitLabel = "xform_loop_lid__EXIT_POINT";

	m_codeLines.push_back("xform_loop_lid__ENTRY_POINT:");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "w") + "VF05, 60(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_LOI) + "0x43000000");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULI, "w") + "VF10, VF05, i", vuInstrPrefix(VU_OP_XTOP) + "VI04");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "y") + "VI08, 0(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_LOI) + "0x437f0000");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF12, VF00, i", vuInstrPrefixFields(VU_OP_ILW, "z") + "VI03, 0(VI04)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MINII, "w") + "VF10, VF10, i", vuInstrPrefix(VU_OP_LOI) + "0x437f0000");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF05, 75(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI06, VI04, 5");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "y") + "VF10, VF00, i", vuInstrPrefix(VU_OP_LOI) + "0x40400000");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_MTIR) + "VI02, VF05x");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + "VI03, VI02, VI03");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "x") + "VF05, VI03");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI00, 0x4e");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF05, VI03");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "z") + "VF09, VF00, i", vuInstrPrefix(VU_OP_LOI) + "0x437d0000");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI08, VI06, VI08");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + "VI09, 0(VI06)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF05, 77(VI00)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQI, "w") + "VF05, (VI06++)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI05, VI04, 0xac");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI04, 5");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI07, VI00, 0xff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI09, VI09, VI07");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF08, VF00, i", vuInstrPrefix(VU_OP_IADD) + "VI01, VI09, VI09");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "y", "w") + "VF06, VF05, VF10y", vuInstrPrefix(VU_OP_IADD) + "VI01, VI01, VI09");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "w") + "VF05, VF05, VF09z", vuInstrPrefix(VU_OP_IADD) + "VI10, VI01, VI04");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, 0(VI10)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "w") + "VF05, VF05, VF08", vuInstrPrefixFields(VU_OP_LQ, "w") + "VF09, 57(VI00)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF11x", vuInstrPrefix(VU_OP_LOI) + "0x45000000");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF11y", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF11z", vuInstrPrefix(VU_OP_IADD) + "VI09, VI09, VI05");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF13, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 0(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_MTIR) + "VI11, VF05w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF13w");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI09, VI11, VI04");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI09)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAXI, "w") + "VF07, VF00, i", vuInstrPrefixFields(VU_OP_MR32, "z") + "VF05, VF09");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF11, VF07, VF12w", vuInstrPrefix(VU_OP_LOI) + "0x44fff000");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDI, "xy") + "VF05, VF00, i", vuInstrPrefix(VU_OP_IADDIU) + "VI02, VI00, 0x4b");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF12x", vuInstrPrefix(VU_OP_XGKICK) + "VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF13, q", vuInstrPrefixFields(VU_OP_ILW, "w") + "VI02, 76(VI00)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF12y", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF06, 76(VI00)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF12z", vuInstrPrefix(VU_OP_FCSET) + "0");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstrPrefix(VU_OP_MTIR) + "VI01, VF06w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF08, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 2(VI10)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF08, VF06", vuInstrPrefix(VU_OP_IADD) + "VI01, VI01, VI05");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI01)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "w") + "VF11, VF10", vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF11", vuInstrPrefix(VU_OP_IBEQ) + "VI06, VI08, " + epi1Label);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 2(VI09)");

	m_codeLines.push_back(pro1Label + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF14, q", vuInstrPrefixFields(VU_OP_ILW, "w") + "VI11, 0(VI06)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQI, "w") + "VF05, (VI06++)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 1(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF16, q", vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI11, VI11, VI07");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI10, VI11, VI11");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "y", "w") + "VF06, VF05, VF10y", vuInstrPrefix(VU_OP_IADD) + "VI10, VI10, VI11");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "w") + "VF05, VF05, VF09z", vuInstrPrefix(VU_OP_IADD) + "VI12, VI10, VI04");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF07, VF11, VF06", vuInstrPrefix(VU_OP_IADD) + "VI11, VI11, VI05");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF11, VF05", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF11, 0(VI12)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF16, VF13", vuInstrPrefix(VU_OP_IAND) + "VI09, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "w") + "VF05, VF05, VF08", vuInstrPrefix(VU_OP_IOR) + "VI09, VI09, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF07xyz, VF07w", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI09, 0x7fff");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF11x", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF16, VI01");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF11y", vuInstrPrefix(VU_OP_MTIR) + "VI10, VF06w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF11z", vuInstrPrefix(VU_OP_MTIR) + "VI09, VF05w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF13, VF04, VF00w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 0(VI11)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF08, q", vuInstrPrefix(VU_OP_SQ) + "VF16, 2(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI11, VI09, VI04");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF13w");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF11, VF07, VF12w", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI03, 0");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF07, VF12, VF12w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI11)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF15, VF14", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 2(VI12)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI03, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF12x", vuInstrPrefix(VU_OP_IOR) + "VI03, VI03, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF13, q", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 0x7fff");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF12y", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF15, VI03");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF12z", vuInstrPrefix(VU_OP_IADD) + "VI10, VI10, VI05");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 3(VI09)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF08, VF05", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI09, 6");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF08, VF06", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI10)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF15, 5(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF11", vuInstrPrefix(VU_OP_IBEQ) + "VI06, VI08, " + epi0Label);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 2(VI11)");

	m_codeLines.push_back(mainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + "VI11, 0(VI06)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQI, "w") + "VF05, (VI06++)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 1(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF15, VF14, q", vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF16, q", vuInstrPrefix(VU_OP_IAND) + "VI11, VI11, VI07");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "y", "w") + "VF06, VF05, VF10y", vuInstrPrefix(VU_OP_IADD) + "VI10, VI11, VI11");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "w") + "VF05, VF05, VF09z", vuInstrPrefix(VU_OP_IADD) + "VI10, VI10, VI11");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADD) + "VI12, VI10, VI04");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF11, VF05", vuInstrPrefix(VU_OP_IADD) + "VI11, VI11, VI05");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF18, VF11, VF06", vuInstrPrefix(VU_OP_MTIR) + "VI10, VF06w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "w") + "VF05, VF05, VF08", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF17, 0(VI12)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF16, VF13", vuInstrPrefix(VU_OP_IAND) + "VI01, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF07", vuInstrPrefix(VU_OP_IOR) + "VI01, VI01, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF18xyz, VF07w", vuInstrPrefix(VU_OP_IADDIU) + "VI13, VI01, 0x7fff");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF17x", vuInstrPrefix(VU_OP_MTIR) + "VI01, VF05w");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF17y", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF16, VI13");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF17z", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF07, 0(VI11)");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF13, VF04, VF00w", vuInstrPrefix(VU_OP_IADD) + "VI11, VI01, VI04");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF15, 0(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF16, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF11, VF07, VF12w", vuInstrPrefix(VU_OP_SQ) + "VF11, 4(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF13w");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF07, VF12, VF12w", vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI03, 0");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI11)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF15, VF14", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF14, 2(VI12)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF08, q", vuInstrPrefix(VU_OP_IAND) + "VI03, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MULA, "x") + "ACC, VF01, VF12x", vuInstrPrefix(VU_OP_IOR) + "VI03, VI03, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF08, VF13, q", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 0x7fff");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "y") + "ACC, VF02, VF12y", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF15, VI03");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADDA, "z") + "ACC, VF03, VF12z", vuInstrPrefix(VU_OP_IADD) + "VI10, VI10, VI05");
	emitRawPairedLine(vuInstrPrefixBroadcast(VU_OP_MADD, "w") + "VF16, VF04, VF00w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 3(VI09)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF13, VF08, VF05", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI09, 6");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF08, VF08, VF06", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF12, 0(VI10)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF15, 5(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, VF00w, VF16w");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF11", vuInstrPrefix(VU_OP_IBNE) + "VI06, VI08, " + mainLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF08xyz, VF07w", vuInstrPrefixFields(VU_OP_LQ, "xyz") + "VF08, 2(VI11)");

	m_codeLines.push_back(epi0Label + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF09, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF16, q", vuInstrPrefix(VU_OP_SQ) + "VF11, 1(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI01, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF16, VF13", vuInstrPrefix(VU_OP_IOR) + "VI01, VI01, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF06, VF11, VF06", vuInstrPrefix(VU_OP_IADDIU) + "VI01, VI01, 0x7fff");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF11, VF05", vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF16, VI01");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF07", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF06xyz, VF07w", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF09, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF08, q", vuInstrPrefix(VU_OP_SQ) + "VF16, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF07, VF12, VF12w", vuInstrPrefix(VU_OP_SQ) + "VF11, 4(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + "VI09, VI03, 0");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI03, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF07", vuInstrPrefix(VU_OP_IOR) + "VI03, VI03, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF12, VF14", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 0x7fff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF12, VI03");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 3(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 4(VI09)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF12, 5(VI09)");

	m_codeLines.push_back(epi1Label + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF07, VF14, q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 1(VI03)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF11, VF16, q", vuInstrPrefix(VU_OP_IAND) + "VI04, VI01, VI02");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + "VI04, VI04, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF16, VF13", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI04, 0x7fff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF16, VI04");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + "VF14, VF11, VF05", vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF07, 0(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + "VF11, VF11, VF06", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + "VF16, VF08, q", vuInstrPrefix(VU_OP_SQ) + "VF16, 2(VI03)");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + "VF11xyz, VF07w", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MINI, "w", "xyz") + "VF07, VF12, VF12w", vuInstrPrefix(VU_OP_IADDIU) + "VI04, VI03, 0");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + "VI01, 262143");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + "VI03, VI01, VI02");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + "VF12, VF14", vuInstrPrefix(VU_OP_IOR) + "VI03, VI03, VI00");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + "VF11, VF07", vuInstrPrefix(VU_OP_IADDIU) + "VI03, VI03, 0x7fff");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + "VF12, VI03");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + "VF16, 3(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF11, 4(VI04)");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + "VF12, 5(VI04)");

	m_codeLines.push_back(exitLabel + ":");
}

bool CodeGenerator::tryEmitLinearXformSoftwarePipelineLoop( std::list<Token>& tokens,
                                                            std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "xform_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	LinearXformLoopPipelinePattern pattern;
	if( !collectLinearXformLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitLinearXformSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectLinearXformLoopPipelinePattern( std::list<Token>::iterator begin,
                                                           std::list<Token>::iterator end,
                                                           LinearXformLoopPipelinePattern& pattern )
{
	pattern = LinearXformLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogLabel = pattern.sourceLabel + "__EPI0";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveVertexLoad = false;
	bool haveStripLoad = false;
	bool haveTexLoad = false;
	bool haveTexStore = false;
	bool haveColorStore = false;
	bool haveGsStore = false;
	bool haveTransformMulax = false;
	bool haveTransformMadday = false;
	bool haveTransformMaddaz = false;
	bool haveTransformMaddw = false;
	bool haveDiv = false;
	bool haveXformedMulq = false;
	bool haveTexMulq = false;
	bool haveGsAdd = false;
	bool haveFtoi = false;
	bool haveSub = false;
	bool haveOpmula = false;
	bool haveOpmsub = false;
	bool haveFmand = false;
	bool haveZSignSub = false;
	bool haveZSwitchSub = false;
	bool haveZSignMask = false;
	bool haveStripFlip = false;
	bool haveZSwitchOr = false;
	bool haveOldDelta = false;
	bool haveOldVertex = false;
	bool haveClipMul = false;
	bool haveClipw = false;
	bool haveFcand = false;
	bool haveClipIand = false;
	bool haveAdcOrClip = false;
	bool haveAdcOrStrip = false;
	bool haveAdcAdd = false;
	bool haveMfir = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == vuInstr(VU_OP_NOP) )
			continue;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				continue;

			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 2 )
			{
				pattern.texReg = dst;
				pattern.texOffset = offset;
				haveTexLoad = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_ILW) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.stripAdcReg = dst;
				pattern.stripOffset = offset;
				haveStripLoad = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;

			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 0 )
			{
				pattern.outputReg = base;
				pattern.texReg = src;
				pattern.texStoreOffset = offset;
				haveTexStore = true;
				continue;
			}
			if( tokenHasFields(token, Token::X | Token::Y | Token::Z) && offset == 1 )
			{
				pattern.outputReg = base;
				pattern.constantColorReg = src;
				pattern.colorStoreOffset = offset;
				haveColorStore = true;
				continue;
			}
			if( offset == 2 )
			{
				pattern.outputReg = base;
				pattern.gsReg = src;
				pattern.gsStoreOffset = offset;
				haveGsStore = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst == src )
			{
				if( !pattern.newAdcReg.empty() && dst == pattern.newAdcReg )
				{
					pattern.adcImmediate = imm;
					haveAdcAdd = true;
					continue;
				}
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				if( dst == pattern.inputReg )
				{
					pattern.inputStep = value;
					haveInputIncrement = true;
					continue;
				}
				if( !pattern.outputReg.empty() && dst == pattern.outputReg )
				{
					pattern.outputStep = value;
					haveOutputIncrement = true;
					continue;
				}
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && token.broadcast() == Token::X )
		{
			if( !getRegisterArgKey(token, 1, pattern.row0Reg) )
				return false;
			pattern.transformMulaxOp = generateOperand(token);
			haveTransformMulax = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Y )
		{
			if( !getRegisterArgKey(token, 1, pattern.row1Reg) )
				return false;
			pattern.transformMaddayOp = generateOperand(token);
			haveTransformMadday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Z )
		{
			if( !getRegisterArgKey(token, 1, pattern.row2Reg) )
				return false;
			pattern.transformMaddazOp = generateOperand(token);
			haveTransformMaddaz = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::W )
		{
			if( !getRegisterArgKey(token, 0, pattern.xformedReg)
			    || !getRegisterArgKey(token, 1, pattern.row3Reg) )
				return false;
			pattern.transformMaddwOp = generateOperand(token);
			haveTransformMaddw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				return false;
			if( !pattern.xformedReg.empty() && denom != pattern.xformedReg )
				return false;
			haveDiv = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULQ) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( src == pattern.xformedReg )
			{
				haveXformedMulq = true;
				continue;
			}
			if( src == pattern.texReg )
			{
				pattern.texReg = dst;
				haveTexMulq = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 == pattern.xformedReg )
			{
				pattern.gsReg = dst;
				pattern.gsOffsetsReg = src1;
				haveGsAdd = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_FTOI4) )
		{
			std::string dst;
			if( !getRegisterArgKey(token, 0, dst) )
				return false;
			pattern.gsReg = dst;
			haveFtoi = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SUB) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src1 != pattern.xformedReg )
				return false;
			pattern.deltaReg = dst;
			pattern.oldVertexReg = src0;
			haveSub = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_OPMULA) )
		{
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 1, src0) || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 != pattern.deltaReg )
				return false;
			pattern.oldDeltaReg = src1;
			haveOpmula = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_OPMSUB) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 != pattern.oldDeltaReg || src1 != pattern.deltaReg )
				return false;
			pattern.bfcNormalReg = dst;
			haveOpmsub = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FMAND) )
		{
			if( !getRegisterArgKey(token, 0, pattern.zSignReg)
			    || !getRegisterArgKey(token, 1, pattern.zSignMaskReg) )
				return false;
			haveFmand = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ISUB) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( !pattern.zSignReg.empty() && dst == pattern.zSignReg && src0 == pattern.zSignReg )
			{
				pattern.zSignSwitchReg = src1;
				haveZSignSub = true;
				continue;
			}
			if( !pattern.zSignSwitchReg.empty() && dst == pattern.zSignSwitchReg
			    && src0 == pattern.zSignMaskReg && src1 == pattern.zSignSwitchReg )
			{
				haveZSwitchSub = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IAND) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 == pattern.stripAdcReg && src1 == pattern.zSignMaskReg )
			{
				pattern.stripFlipReg = dst;
				haveStripFlip = true;
				continue;
			}
			if( dst == pattern.zSignReg && src0 == pattern.zSignReg && src1 == pattern.zSignMaskReg )
			{
				haveZSignMask = true;
				continue;
			}
			if( !pattern.clipResultReg.empty() && dst == pattern.clipResultReg
			    && src0 == pattern.clipResultReg )
			{
				pattern.doClippingReg = src1;
				haveClipIand = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IOR) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( dst == pattern.zSignSwitchReg
			    && src0 == pattern.zSignSwitchReg
			    && src1 == pattern.stripFlipReg )
			{
				haveZSwitchOr = true;
				continue;
			}
			if( !pattern.clipResultReg.empty()
			    && src0 == pattern.clipResultReg
			    && src1 == pattern.zSignReg )
			{
				pattern.newAdcReg = dst;
				haveAdcOrClip = true;
				continue;
			}
			if( !pattern.newAdcReg.empty()
			    && dst == pattern.newAdcReg
			    && src0 == pattern.newAdcReg
			    && src1 == pattern.stripAdcReg )
			{
				haveAdcOrStrip = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( dst == pattern.oldDeltaReg && src0 == pattern.deltaReg )
			{
				pattern.bfcMultiplierReg = src1;
				haveOldDelta = true;
				continue;
			}
			if( dst == pattern.oldVertexReg && src0 == pattern.xformedReg )
			{
				haveOldVertex = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst)
			    || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 == pattern.xformedReg )
			{
				pattern.clipReg = dst;
				pattern.clipScalesReg = src1;
				haveClipMul = true;
				continue;
			}
			return false;
		}

		if( isVuClipw(mnemonic) )
		{
			std::string clip;
			std::string scales;
			if( !getRegisterArgKey(token, 0, clip) || !getRegisterArgKey(token, 1, scales) )
				return false;
			if( clip != pattern.clipReg )
				return false;
			pattern.clipScalesReg = scales;
			haveClipw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FCAND) )
		{
			if( !getRegisterArgKey(token, 0, pattern.clipResultReg) )
				return false;
			getImmediateArg(token, 1, pattern.clipImmediate);
			haveFcand = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MFIR) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( dst != pattern.gsReg || src != pattern.newAdcReg )
				return false;
			haveMfir = true;
			continue;
		}

		return false;
	}

	bool ok = haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveVertexLoad
	    && haveStripLoad
	    && haveTexLoad
	    && haveTexStore
	    && haveColorStore
	    && haveGsStore
	    && haveTransformMulax
	    && haveTransformMadday
	    && haveTransformMaddaz
	    && haveTransformMaddw
	    && haveDiv
	    && haveXformedMulq
	    && haveTexMulq
	    && haveGsAdd
	    && haveFtoi
	    && haveSub
	    && haveOpmula
	    && haveOpmsub
	    && haveFmand
	    && haveZSignSub
	    && haveZSwitchSub
	    && haveZSignMask
	    && haveStripFlip
	    && haveZSwitchOr
	    && haveOldDelta
	    && haveOldVertex
	    && haveClipMul
	    && haveClipw
	    && haveFcand
	    && haveClipIand
	    && haveAdcOrClip
	    && haveAdcOrStrip
	    && haveAdcAdd
	    && haveMfir
	    && (pattern.inputStep == 3 || pattern.inputStep == 4)
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.stripOffset == 0
	    && pattern.texOffset == 2
	    && pattern.texStoreOffset == 0
	    && pattern.colorStoreOffset == 1
	    && pattern.gsStoreOffset == 2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.stripAdcReg.empty()
	    && !pattern.stripFlipReg.empty()
	    && !pattern.row0Reg.empty()
	    && !pattern.row1Reg.empty()
	    && !pattern.row2Reg.empty()
	    && !pattern.row3Reg.empty()
	    && !pattern.xformedReg.empty()
	    && !pattern.gsReg.empty()
	    && !pattern.gsOffsetsReg.empty()
	    && !pattern.oldVertexReg.empty()
	    && !pattern.oldDeltaReg.empty()
	    && !pattern.deltaReg.empty()
	    && !pattern.bfcNormalReg.empty()
	    && !pattern.zSignReg.empty()
	    && !pattern.zSignMaskReg.empty()
	    && !pattern.zSignSwitchReg.empty()
	    && !pattern.bfcMultiplierReg.empty()
	    && !pattern.clipReg.empty()
	    && !pattern.clipScalesReg.empty()
	    && !pattern.clipResultReg.empty()
	    && !pattern.doClippingReg.empty()
	    && !pattern.newAdcReg.empty()
	    && !pattern.constantColorReg.empty()
	    && !pattern.texReg.empty();
	return ok;
}

void CodeGenerator::emitLinearXformScalarBody( const LinearXformLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string vert = p.vertexReg;
	const std::string strip = p.stripAdcReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string delta = p.deltaReg;
	const std::string zSign = p.zSignReg;
	const std::string zSwitch = p.zSignSwitchReg;
	const std::string clip = p.clipReg;
	const std::string clipResult = p.clipResultReg;
	const std::string adc = p.newAdcReg;
	const std::string stripFlip = p.stripFlipReg == clipResult ? adc : p.stripFlipReg;
	const std::string tex = p.texReg;
	const std::string vf00 = "VF00";

	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + tex + ", " + offsetBase(2, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + p.constantColorReg + ", " + offsetBase(1, out));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrPrefix(VU_OP_IAND) + stripFlip + ", " + strip + ", " + p.zSignMaskReg);
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + x + ", " + x + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + gs + ", " + x + ", " + p.gsOffsetsReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + gs, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + p.oldVertexReg + ", " + x, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, " + fieldArg(delta, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + fieldArg(p.bfcNormalReg, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz") + ", " + fieldArg(delta, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FMAND) + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + clip + ", " + x + ", " + p.clipScalesReg,
	                  vuInstrPrefix(VU_OP_ISUB) + zSign + ", " + zSign + ", " + zSwitch);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + p.oldDeltaReg + ", " + delta + ", " + fieldArg(p.bfcMultiplierReg, "w"),
	                  vuInstrPrefix(VU_OP_ISUB) + zSwitch + ", " + p.zSignMaskReg + ", " + zSwitch);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + p.oldVertexReg + ", " + x + ", " + x,
	                  vuInstrPrefix(VU_OP_IAND) + zSign + ", " + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + tex + ", " + tex + ", q",
	                  vuInstrPrefix(VU_OP_IOR) + zSwitch + ", " + zSwitch + ", " + stripFlip);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + clipResult + ", " + p.clipImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + clipResult + ", " + clipResult + ", " + p.doClippingReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + adc + ", " + clipResult + ", " + zSign);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + adc + ", " + adc + ", " + strip);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + adc + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + tex + ", " + offsetBase(0, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(2, out));
}

void CodeGenerator::emitLinearXformSoftwarePipelineLoop( const LinearXformLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vert = p.vertexReg;
	const std::string strip = p.stripAdcReg;
	const std::string x = p.xformedReg;
	const std::string gs = p.gsReg;
	const std::string delta = p.deltaReg;
	const std::string zSign = p.zSignReg;
	const std::string zSwitch = p.zSignSwitchReg;
	const std::string clip = p.clipReg;
	const std::string clipResult = p.clipResultReg;
	const std::string adc = p.newAdcReg;
	const std::string stripFlip = p.stripFlipReg == clipResult ? adc : p.stripFlipReg;
	const std::string tex = p.texReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitLinearXformScalarBody(p);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + integerText(p.inputStep));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.prologLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", " + integerText(p.outputStep));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + tex + ", " + offsetBase(2, in));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrPrefix(VU_OP_IAND) + stripFlip + ", " + strip + ", " + p.zSignMaskReg);
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + x + ", " + x + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + gs + ", " + x + ", " + p.gsOffsetsReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + p.oldVertexReg + ", " + x, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + clip + ", " + x + ", " + p.clipScalesReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, " + fieldArg(delta, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + fieldArg(p.bfcNormalReg, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz") + ", " + fieldArg(delta, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FMAND) + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUB) + zSign + ", " + zSign + ", " + zSwitch);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + zSign + ", " + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUB) + zSwitch + ", " + p.zSignMaskReg + ", " + zSwitch);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + zSwitch + ", " + zSwitch + ", " + stripFlip);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + clipResult + ", " + p.clipImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + integerText(p.inputStep));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.epilogLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + gs,
	                  vuInstrPrefix(VU_OP_IAND) + clipResult + ", " + clipResult + ", " + p.doClippingReg);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + tex + ", " + tex + ", q",
	                  vuInstrPrefix(VU_OP_IOR) + adc + ", " + clipResult + ", " + zSign);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MAX, "xyz") + p.oldVertexReg + ", " + x + ", " + x,
	                  vuInstrPrefix(VU_OP_IOR) + adc + ", " + adc + ", " + strip);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + p.oldDeltaReg + ", " + delta + ", " + fieldArg(p.bfcMultiplierReg, "w"),
	                  vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + adc + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + p.constantColorReg + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + tex + ", " + offsetBase(0, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(2, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vert + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_ILW, "w") + strip + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + tex + ", " + offsetBase(2, in));
	emitRawPairedLine(p.transformMulaxOp + " ACC, " + p.row0Reg + ", " + fieldArg(vert, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddayOp + " ACC, " + p.row1Reg + ", " + fieldArg(vert, "y"),
	                  vuInstrPrefix(VU_OP_IAND) + stripFlip + ", " + strip + ", " + p.zSignMaskReg);
	emitRawPairedLine(p.transformMaddazOp + " ACC, " + p.row2Reg + ", " + fieldArg(vert, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(p.transformMaddwOp + " " + x + ", " + p.row3Reg + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(x, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + x + ", " + x + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + gs + ", " + x + ", " + p.gsOffsetsReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + p.oldVertexReg + ", " + x, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + clip + ", " + x + ", " + p.clipScalesReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMULA, "xyz") + "ACCxyz, " + fieldArg(delta, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_OPMSUB, "xyz") + fieldArg(p.bfcNormalReg, "xyz") + ", " + fieldArg(p.oldDeltaReg, "xyz") + ", " + fieldArg(delta, "xyz"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_CLIPW, "xyz") + fieldArg(clip, "xyz") + ", " + fieldArg(p.clipScalesReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FMAND) + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUB) + zSign + ", " + zSign + ", " + zSwitch);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IAND) + zSign + ", " + zSign + ", " + p.zSignMaskReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUB) + zSwitch + ", " + p.zSignMaskReg + ", " + zSwitch);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + zSwitch + ", " + zSwitch + ", " + stripFlip);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_FCAND) + clipResult + ", " + p.clipImmediate);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + integerText(p.inputStep));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", " + integerText(p.outputStep));

	m_codeLines.push_back(p.epilogLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI4, "xyz") + gs + ", " + gs,
	                  vuInstrPrefix(VU_OP_IAND) + clipResult + ", " + clipResult + ", " + p.doClippingReg);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + adc + ", " + clipResult + ", " + zSign);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IOR) + adc + ", " + adc + ", " + strip);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + adc + ", " + adc + ", " + p.adcImmediate);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + tex + ", " + tex + ", q",
	                  vuInstrPrefixFields(VU_OP_MFIR, "w") + gs + ", " + adc);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + p.constantColorReg + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + tex + ", " + offsetBase(0, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + gs + ", " + offsetBase(2, out));

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitDirLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                               std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "dir_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end()
	    && ( (afterBranch->flags() & Token::BRANCH_DELAY_FILLER)
	         || afterBranch->label().length() == 0 ) )
		++afterBranch;

	DirLightNoSpecLoopPipelinePattern pattern;
	if( !collectDirLightNoSpecLoopPipelinePattern(token, afterBranch, pattern) )
	{
		std::list<Token>::iterator extendedAfterBranch = afterBranch;
		if( extendedAfterBranch != tokens.end() && extendedAfterBranch->label().length() == 0 )
			++extendedAfterBranch;
		if( extendedAfterBranch == afterBranch
		    || !collectDirLightNoSpecLoopPipelinePattern(token, extendedAfterBranch, pattern) )
			return false;
		afterBranch = extendedAfterBranch;
	}

	emitDirLightNoSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::tryEmitDirLightSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                             std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "dir_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	DirLightSpecLoopPipelinePattern pattern;
	if( !collectDirLightSpecLoopPipelinePattern(token, afterBranch, pattern) )
	{
		std::list<Token>::iterator extendedAfterBranch = afterBranch;
		if( extendedAfterBranch != tokens.end() && extendedAfterBranch->label().length() == 0 )
			++extendedAfterBranch;
		if( extendedAfterBranch == afterBranch
		    || !collectDirLightSpecLoopPipelinePattern(token, extendedAfterBranch, pattern) )
			return false;
		afterBranch = extendedAfterBranch;
	}

	emitDirLightSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectDirLightSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                            std::list<Token>::iterator end,
                                                            DirLightSpecLoopPipelinePattern& pattern )
{
	pattern = DirLightSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__SPEC_ENTRY_POINT";
	pattern.prologOneLabel = pattern.sourceLabel + "__SPEC_PRO1";
	pattern.prologTwoLabel = pattern.sourceLabel + "__SPEC_PRO2";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.drainLabel = pattern.sourceLabel + "__SPEC_DRAIN_TAIL";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__SPEC_FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__SPEC_FALLBACK2";
	pattern.fallbackThreeLabel = pattern.sourceLabel + "__SPEC_FALLBACK3";
	pattern.scalarLabel = pattern.sourceLabel + "__SPEC_SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__SPEC_EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveMaterialDiffInputLoad = false;
	bool haveColorLoad = false;
	bool haveDiffuseMul = false;
	bool haveDiffuseAdday = false;
	bool haveDiffuseMaddx = false;
	bool haveDiffuseMax = false;
	bool haveLocalDiff = false;
	bool haveMula = false;
	bool haveSpecMul = false;
	bool haveSpecMr32 = false;
	bool haveSpecAddax = false;
	bool haveSpecMaddy = false;
	bool haveSpecMax = false;
	bool haveSpecPower = false;
	bool haveMaddaw = false;
	bool haveMadd = false;
	bool haveAdd = false;
	bool haveStore = false;
	unsigned int specPowerCount = 0;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == vuInstr(VU_OP_NOP) )
			continue;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				return false;
			if( base == pattern.inputReg && offset == 1 )
			{
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 3 )
			{
				pattern.materialDiffReg = dst;
				pattern.materialDiffOffset = offset;
				pattern.materialDiffFromInput = true;
				haveMaterialDiffInputLoad = true;
				continue;
			}
			if( base != pattern.inputReg && (offset == 0 || offset == 1) )
			{
				if( !pattern.outputReg.empty() && base != pattern.outputReg )
					return false;
				pattern.outputReg = base;
				pattern.colorReg = dst;
				pattern.colorOffset = offset;
				haveColorLoad = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				return false;
			if( dst != src )
				return false;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				return false;
			if( dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
				continue;
			}
			if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( right == pattern.normalReg && !haveDiffuseMul )
			{
				pattern.diffProductReg = dst;
				pattern.lightDirReg = left;
				haveDiffuseMul = true;
				continue;
			}
			if( right == pattern.normalReg && haveDiffuseMul && !haveSpecMul )
			{
				pattern.specProductReg = dst;
				pattern.halfAngleReg = left;
				haveSpecMul = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MR32) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( src != pattern.specProductReg )
				return false;
			pattern.specSwizzleReg = dst;
			haveSpecMr32 = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADDA) && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 1, src) || src != pattern.diffProductReg )
				return false;
			haveDiffuseAdday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( dst != pattern.diffProductReg || product != pattern.diffProductReg )
				return false;
			pattern.onesReg = src;
			haveDiffuseMaddx = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && token.broadcast() == Token::X )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( tokenHasFields(token, Token::Z) && dst == pattern.diffProductReg
			    && src == pattern.diffProductReg )
			{
				haveDiffuseMax = true;
				continue;
			}
			if( tokenHasFields(token, Token::W) && src == pattern.specScratchReg )
			{
				pattern.specIntensityReg = dst;
				haveSpecMax = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == Token::Z
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( product != pattern.diffProductReg )
				return false;
			pattern.localDiffReg = dst;
			pattern.lightDiffReg = src;
			haveLocalDiff = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				return false;
			if( src != pattern.localDiffReg )
				return false;
			if( pattern.materialDiffFromInput )
			{
				if( material != pattern.materialDiffReg )
					return false;
			}
			else
			{
				pattern.materialDiffReg = material;
			}
			haveMula = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADDA) && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 1, src) || src != pattern.specSwizzleReg )
				return false;
			haveSpecAddax = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::Y && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 2, src) )
				return false;
			if( src != pattern.specSwizzleReg )
				return false;
			pattern.specScratchReg = dst;
			haveSpecMaddy = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0 && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src0;
			std::string src1;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src0)
			    || !getRegisterArgKey(token, 2, src1) )
				return false;
			if( src0 != src1 )
				return false;
			if( pattern.specIntensityReg.empty() || (src0 != pattern.specIntensityReg
			    && src0 != pattern.specPowerReg) )
				return false;
			pattern.specPowerReg = dst;
			++specPowerCount;
			haveSpecPower = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 1, src) )
				return false;
			pattern.localSpecReg = src;
			haveMaddaw = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string amb;
			std::string mat;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, amb)
			    || !getRegisterArgKey(token, 2, mat) )
				return false;
			pattern.litReg = dst;
			pattern.lightAmbReg = amb;
			pattern.materialAmbReg = mat;
			haveMadd = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( left != pattern.colorReg || right != pattern.litReg )
				return false;
			pattern.resultReg = dst;
			haveAdd = true;
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_SQ) || mnemonic == vuInstr(VU_OP_SQI)) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			bool haveAddress = getIndirectBaseAndOffset(token, base, offset);
			Token::Argument addressArg("");
			if( !haveAddress && getArg(token, 1, addressArg)
			    && (addressArg.flags() & Token::Argument::POSTINC)
			    && getRegisterArgKey(token, 1, base) )
			{
				offset = 0;
				haveAddress = true;
			}
			if( !haveAddress || !getRegisterArgKey(token, 0, src) )
				return false;
			if( base != pattern.outputReg || src != pattern.resultReg )
				return false;
			if( mnemonic == vuInstr(VU_OP_SQI) || (getArg(token, 1, addressArg)
			    && (addressArg.flags() & Token::Argument::POSTINC)) )
				pattern.postIncrementStore = true;
			pattern.storeOffset = offset;
			haveStore = true;
			continue;
		}

		return false;
	}

	bool ok = haveBranch
	    && haveInputIncrement
	    && (haveOutputIncrement || pattern.postIncrementStore)
	    && haveNormalLoad
	    && haveColorLoad
	    && haveDiffuseMul
	    && haveDiffuseAdday
	    && haveDiffuseMaddx
	    && haveDiffuseMax
	    && haveLocalDiff
	    && haveMula
	    && haveSpecMul
	    && haveSpecMr32
	    && haveSpecAddax
	    && haveSpecMaddy
	    && haveSpecMax
	    && haveSpecPower
	    && specPowerCount >= 5
	    && haveMaddaw
	    && haveMadd
	    && haveAdd
	    && haveStore
	    && ((pattern.materialDiffFromInput && haveMaterialDiffInputLoad && pattern.inputStep == 4
	         && pattern.materialDiffOffset == 3)
	        || (!pattern.materialDiffFromInput && pattern.inputStep == 3))
	    && (pattern.postIncrementStore || pattern.outputStep == 3)
	    && pattern.normalOffset == 1
	    && ((pattern.postIncrementStore && pattern.colorOffset == 0 && pattern.storeOffset == 0)
	        || (!pattern.postIncrementStore && pattern.colorOffset == 1 && pattern.storeOffset == 1))
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.lightDirReg.empty()
	    && !pattern.diffProductReg.empty()
	    && !pattern.onesReg.empty()
	    && !pattern.lightDiffReg.empty()
	    && !pattern.localDiffReg.empty()
	    && !pattern.materialDiffReg.empty()
	    && !pattern.halfAngleReg.empty()
	    && !pattern.specProductReg.empty()
	    && !pattern.specSwizzleReg.empty()
	    && !pattern.specScratchReg.empty()
	    && !pattern.specIntensityReg.empty()
	    && !pattern.specPowerReg.empty()
	    && !pattern.localSpecReg.empty()
	    && !pattern.lightAmbReg.empty()
	    && !pattern.materialAmbReg.empty()
	    && !pattern.litReg.empty()
	    && !pattern.resultReg.empty();
	return ok;
}

void CodeGenerator::emitDirLightSpecSoftwarePipelineLoop( const DirLightSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vf00 = "VF00";
	const std::string inputStep = integerText(p.inputStep);
	const std::string inputStepOne = integerText(p.inputStep);
	const std::string inputStepTwo = integerText(p.inputStep * 2);
	const std::string inputStepThree = integerText(p.inputStep * 3);
	std::list<std::string> reserved;
	addScratchReservation(reserved, p.lightDirReg);
	addScratchReservation(reserved, p.onesReg);
	addScratchReservation(reserved, p.lightDiffReg);
	if( !p.materialDiffFromInput )
		addScratchReservation(reserved, p.materialDiffReg);
	addScratchReservation(reserved, p.halfAngleReg);
	addScratchReservation(reserved, p.localSpecReg);
	addScratchReservation(reserved, p.lightAmbReg);
	addScratchReservation(reserved, p.materialAmbReg);

	const std::string normal = reserveScratchReg(reserved);
	const std::string color = reserveScratchReg(reserved);
	const std::string diff = reserveScratchReg(reserved);
	const std::string diffClamp = reserveScratchReg(reserved);
	const std::string localDiff = reserveScratchReg(reserved);
	const std::string specProd = reserveScratchReg(reserved);
	const std::string specSwizzle = reserveScratchReg(reserved);
	const std::string specScratch = reserveScratchReg(reserved);
	const std::string specIntensity = reserveScratchReg(reserved);
	const std::string specPower = reserveScratchReg(reserved);
	const std::string lit = reserveScratchReg(reserved);
	const std::string result = reserveScratchReg(reserved);
	const std::string materialDiff = p.materialDiffFromInput ? reserveScratchReg(reserved)
	                                                        : p.materialDiffReg;
	const std::string mainOutputStep = p.postIncrementStore ? vuInstr(VU_OP_NOP) : vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3";
	const std::string mainColorLoad = p.postIncrementStore ? offsetBase(0, out) : offsetBase(-2, out);
	const std::string mainStore = p.postIncrementStore
	                            ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + result + ", (" + out + "++)"
	                            : vuInstrPrefixFields(VU_OP_SQ, "xyz") + result + ", " + offsetBase(-2, out);
	const std::string prologOneMaterialLoad = p.materialDiffFromInput
	                                        ? vuInstrPrefixFields(VU_OP_LQ, "xyz") + materialDiff + ", "
	                                          + offsetBase(p.materialDiffOffset - p.inputStep, in)
	                                        : vuInstr(VU_OP_NOP);
	const std::string pipelinedMaterialLoad = p.materialDiffFromInput
	                                        ? vuInstrPrefixFields(VU_OP_LQ, "xyz") + materialDiff + ", "
	                                          + offsetBase(p.materialDiffOffset - (2 * p.inputStep), in)
	                                        : vuInstr(VU_OP_NOP);

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + specProd + ", " + p.halfAngleReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + diff + ", " + p.lightDirReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MR32, "xyw") + specSwizzle + ", " + specProd);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "x", "w") + "ACC, " + specSwizzle + ", " + fieldArg(specSwizzle, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologOneLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "y", "w") + specScratch + ", " + vf00 + ", " + fieldArg(specSwizzle, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + diff + ", " + fieldArg(diff, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + diff + ", " + p.onesReg + ", " + fieldArg(diff, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + specIntensity + ", " + specScratch + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + diffClamp + ", " + diff + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + specProd + ", " + p.halfAngleReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MR32, "xyw") + specSwizzle + ", " + specProd);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity,
	                  prologOneMaterialLoad);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + diff + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + localDiff + ", " + p.lightDiffReg + ", " + fieldArg(diffClamp, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "x", "w") + "ACC, " + specSwizzle + ", " + fieldArg(specSwizzle, "x"),
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologTwoLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "y", "w") + specScratch + ", " + vf00 + ", " + fieldArg(specSwizzle, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + diff + ", " + fieldArg(diff, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + diff + ", " + p.onesReg + ", " + fieldArg(diff, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + specIntensity + ", " + specScratch + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + localDiff + ", " + materialDiff,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + diffClamp + ", " + diff + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + specProd + ", " + p.halfAngleReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(specPower, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg,
	                  vuInstrPrefixFields(VU_OP_MR32, "xyw") + specSwizzle + ", " + specProd);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + diff + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + localDiff + ", " + p.lightDiffReg + ", " + fieldArg(diffClamp, "z"),
	                  pipelinedMaterialLoad);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity,
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackThreeLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "x", "w") + "ACC, " + specSwizzle + ", " + fieldArg(specSwizzle, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "y", "w") + specScratch + ", " + vf00 + ", " + fieldArg(specSwizzle, "y"),
	                  mainOutputStep);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + diff + ", " + fieldArg(diff, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + diff + ", " + p.onesReg + ", " + fieldArg(diff, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + specIntensity + ", " + specScratch + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + localDiff + ", " + materialDiff,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + diffClamp + ", " + diff + ", " + fieldArg(vf00, "x"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + mainColorLoad);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + specProd + ", " + p.halfAngleReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + result + ", " + color + ", " + lit, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(specPower, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg,
	                  vuInstrPrefixFields(VU_OP_MR32, "xyw") + specSwizzle + ", " + specProd);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + diff + ", " + p.lightDirReg + ", " + normal,
	                  mainStore);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + localDiff + ", " + p.lightDiffReg + ", " + fieldArg(diffClamp, "z"),
	                  pipelinedMaterialLoad);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specIntensity + ", " + specIntensity + ", " + specIntensity,
	                  vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "x", "w") + "ACC, " + specSwizzle + ", " + fieldArg(specSwizzle, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepOne);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepTwo);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackThreeLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	emitDirLightSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitDirLightSpecScalarFallbackLoop( const DirLightSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string color = p.colorReg;
	const std::string diff = p.diffProductReg;
	const std::string localDiff = p.localDiffReg;
	const std::string specProd = p.specProductReg;
	const std::string specScratch = p.specScratchReg;
	const std::string specIntensity = p.specIntensityReg;
	const std::string specPower = p.specPowerReg;
	const std::string lit = p.litReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";
	const std::string inputStep = integerText(p.inputStep);
	const std::string scalarColorLoad = offsetBase(p.colorOffset, out);
	const std::string scalarStore = p.postIncrementStore
	                              ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + result + ", (" + out + "++)"
	                              : vuInstrPrefixFields(VU_OP_SQ, "xyz") + result + ", " + offsetBase(1, out);
	const std::string scalarOutputStep = p.postIncrementStore ? vuInstr(VU_OP_NOP) : vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3";

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + scalarColorLoad);
	if( p.materialDiffFromInput )
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + p.materialDiffReg + ", " + offsetBase(p.materialDiffOffset, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + diff + ", " + p.lightDirReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + specProd + ", " + p.halfAngleReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MR32, "xyw") + p.specSwizzleReg + ", " + specProd);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + diff + ", " + fieldArg(diff, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + diff + ", " + p.onesReg + ", " + fieldArg(diff, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + diff + ", " + diff + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "x", "w") + "ACC, " + p.specSwizzleReg + ", " + fieldArg(p.specSwizzleReg, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "y", "w") + specScratch + ", " + vf00 + ", " + fieldArg(p.specSwizzleReg, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + localDiff + ", " + p.lightDiffReg + ", " + fieldArg(diff, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + specIntensity + ", " + specScratch + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + localDiff + ", " + p.materialDiffReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specIntensity + ", " + specIntensity, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + specPower + ", " + specPower + ", " + specPower, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(specPower, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + result + ", " + color + ", " + lit, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), scalarStore);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), scalarOutputStep);

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::collectDirLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                              std::list<Token>::iterator end,
                                                              DirLightNoSpecLoopPipelinePattern& pattern )
{
	pattern = DirLightNoSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologOneLabel = pattern.sourceLabel + "__PRO1";
	pattern.prologTwoLabel = pattern.sourceLabel + "__PRO2";
	pattern.prologThreeLabel = pattern.sourceLabel + "__PRO3";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__FALLBACK2";
	pattern.fallbackThreeLabel = pattern.sourceLabel + "__FALLBACK3";
	pattern.fallbackFourLabel = pattern.sourceLabel + "__FALLBACK4";
	pattern.drainLabel = pattern.sourceLabel + "__DRAIN_TAIL";
	pattern.scalarLabel = pattern.sourceLabel + "__SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveColorLoad = false;
	bool haveMul = false;
	bool haveAdday = false;
	bool haveMaddx = false;
	bool haveMaxx = false;
	bool haveMulz = false;
	bool haveMula = false;
	bool haveMadd = false;
	bool haveAdd = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == vuInstr(VU_OP_NOP) )
			continue;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(token, 0, pattern.inputReg)
			    || !getRegisterArgKey(token, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				return false;

			if( pattern.inputReg.empty() || base == pattern.inputReg )
			{
				pattern.inputReg = base;
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( pattern.outputReg.empty() || base == pattern.outputReg )
			{
				pattern.outputReg = base;
				pattern.colorReg = dst;
				pattern.colorOffset = offset;
				haveColorLoad = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				return false;
			if( dst != src )
				return false;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				return false;
			if( !pattern.inputReg.empty() && dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
				continue;
			}
			if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( !pattern.normalReg.empty() && right == pattern.normalReg )
			{
				pattern.productReg = dst;
				pattern.lightDirReg = left;
				haveMul = true;
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_ADDA) && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 1, src) || src != pattern.productReg )
				return false;
			haveAdday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( dst != pattern.productReg || product != pattern.productReg )
				return false;
			pattern.dotBaseReg = src;
			haveMaddx = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				return false;
			if( dst != pattern.productReg || src != pattern.productReg )
				return false;
			haveMaxx = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == Token::Z
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				return false;
			if( product != pattern.productReg )
				return false;
			pattern.scaledReg = dst;
			pattern.lightColorReg = src;
			haveMulz = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				return false;
			if( src != pattern.scaledReg )
				return false;
			pattern.materialMulReg = material;
			haveMula = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string material;
			std::string ambient;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, material)
			    || !getRegisterArgKey(token, 2, ambient) )
				return false;
			pattern.litReg = dst;
			pattern.materialAddReg = material;
			pattern.ambientReg = ambient;
			haveMadd = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				return false;
			if( left != pattern.colorReg || right != pattern.litReg )
				return false;
			pattern.resultReg = dst;
			haveAdd = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				return false;
			if( base != pattern.outputReg || src != pattern.resultReg )
				return false;
			pattern.storeOffset = offset;
			haveStore = true;
			continue;
		}

		return false;
	}

	if( !(haveBranch
	      && haveInputIncrement
	      && haveOutputIncrement
	      && haveNormalLoad
	      && haveColorLoad
	      && haveMul
	      && haveAdday
	      && haveMaddx
	      && haveMaxx
	      && haveMulz
	      && haveMula
	      && haveMadd
	      && haveAdd
	      && haveStore
	      && pattern.inputStep == 3
	      && pattern.outputStep == 3
	      && pattern.normalOffset == 1
	      && pattern.colorOffset == 1
	      && pattern.storeOffset == 1) )
		return false;

	std::list<std::string> used;
	used.push_back(pattern.normalReg);
	used.push_back(pattern.colorReg);
	used.push_back(pattern.lightDirReg);
	used.push_back(pattern.productReg);
	used.push_back(pattern.dotBaseReg);
	used.push_back(pattern.lightColorReg);
	used.push_back(pattern.materialMulReg);
	used.push_back(pattern.materialAddReg);
	used.push_back(pattern.ambientReg);
	used.push_back(pattern.scaledReg);
	used.push_back(pattern.litReg);
	used.push_back(pattern.resultReg);

	const char* candidates[] = { "VF24", "VF25", "VF26", "VF27", "VF28", "VF29", "VF30", "VF31" };
	for( unsigned int i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i )
	{
		if( containsKey(used, candidates[i]) )
			continue;
		if( pattern.dotReg.empty() )
		{
			pattern.dotReg = candidates[i];
			used.push_back(pattern.dotReg);
			continue;
		}
		pattern.dotNextReg = candidates[i];
		break;
	}

	return !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.lightDirReg.empty()
	    && !pattern.productReg.empty()
	    && !pattern.dotBaseReg.empty()
	    && !pattern.lightColorReg.empty()
	    && !pattern.materialMulReg.empty()
	    && !pattern.materialAddReg.empty()
	    && !pattern.ambientReg.empty()
	    && !pattern.scaledReg.empty()
	    && !pattern.litReg.empty()
	    && !pattern.resultReg.empty()
	    && !pattern.dotReg.empty()
	    && !pattern.dotNextReg.empty();
}

void CodeGenerator::emitDirLightNoSpecSoftwarePipelineLoop( const DirLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string color = p.colorReg;
	const std::string product = p.productReg;
	const std::string dot = p.dotReg;
	const std::string dotNext = p.dotNextReg;
	const std::string scaled = p.scaledReg;
	const std::string lit = p.litReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologOneLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + product + ", " + fieldArg(product, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + dot + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + dot + ", " + dot + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologTwoLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + product + ", " + fieldArg(product, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"),
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackThreeLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologThreeLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + product + ", " + fieldArg(product, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + scaled + ", " + p.materialMulReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.materialAddReg + ", " + p.ambientReg,
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackFourLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + product + ", " + fieldArg(product, "y"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + dotNext + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + result + ", " + color + ", " + lit,
	                  vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + scaled + ", " + p.materialMulReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal,
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.materialAddReg + ", " + p.ambientReg,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + scaled + ", " + p.lightColorReg + ", " + fieldArg(dot, "z"),
	                  vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + dot + ", " + dotNext + ", " + fieldArg(vf00, "x"),
	                  vuInstrPrefixFields(VU_OP_SQ, "xyz") + result + ", " + offsetBase(-2, out));

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 12");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 6");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackThreeLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 9");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackFourLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 12");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	emitDirLightNoSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitDirLightNoSpecScalarFallbackLoop( const DirLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string color = p.colorReg;
	const std::string product = p.productReg;
	const std::string scaled = p.scaledReg;
	const std::string lit = p.litReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + product + ", " + p.lightDirReg + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + product + ", " + fieldArg(product, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + product + ", " + p.dotBaseReg + ", " + fieldArg(product, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "z") + product + ", " + product + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "z", "xyz") + scaled + ", " + p.lightColorReg + ", " + fieldArg(product, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + scaled + ", " + p.materialMulReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.materialAddReg + ", " + p.ambientReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + result + ", " + color + ", " + lit, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + result + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3");

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitPtLightSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                            std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "pt_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	PtLightSpecLoopPipelinePattern pattern;
	if( !collectPtLightSpecLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitPtLightSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectPtLightSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                           std::list<Token>::iterator end,
                                                           PtLightSpecLoopPipelinePattern& pattern )
{
	pattern = PtLightSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__SPEC_ENTRY_POINT";
	pattern.prologOneLabel = pattern.sourceLabel + "__SPEC_PRO1";
	pattern.prologTwoLabel = pattern.sourceLabel + "__SPEC_PRO2";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.drainLabel = pattern.sourceLabel + "__SPEC_DRAIN_TAIL";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__SPEC_FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__SPEC_FALLBACK2";
	pattern.fallbackThreeLabel = pattern.sourceLabel + "__SPEC_FALLBACK3";
	pattern.scalarLabel = pattern.sourceLabel + "__SPEC_SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__SPEC_EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveVertexLoad = false;
	bool haveMaterialDiffInputLoad = false;
	bool haveColorLoad = false;
	bool haveSub = false;
	bool haveDistanceMul = false;
	bool haveDistanceAdday = false;
	bool haveDistanceMaddx = false;
	bool haveSqrt = false;
	bool haveDistanceSetOne = false;
	bool haveDistanceSetLength = false;
	bool haveInverseDistanceDiv = false;
	bool haveNormalize = false;
	bool haveAttenProduct = false;
	bool haveAttenMulax = false;
	bool haveAttenMadday = false;
	bool haveAttenMaddz = false;
	bool haveHalfAdd = false;
	bool haveHalfEsadd = false;
	bool haveHalfMfpLength = false;
	bool haveHalfErsqrt = false;
	bool haveHalfMfpInv = false;
	bool haveHalfNormalize = false;
	bool haveNormalProduct = false;
	bool haveIntensityMulax = false;
	bool haveIntensityMadday = false;
	bool haveIntensityMaddz = false;
	bool haveDiffuseClamp = false;
	bool haveLocalDiffuse = false;
	bool haveDiffuseMaterial = false;
	bool haveSpecProduct = false;
	bool haveSpecMulax = false;
	bool haveSpecMadday = false;
	bool haveSpecMaddz = false;
	bool haveSpecClamp = false;
	bool haveSpecPower = false;
	bool haveSpecMaterial = false;
	bool haveAmbientMaterial = false;
	bool haveAttenuationDiv = false;
	bool haveAttenuateColor = false;
	bool haveAccumulate = false;
	bool haveStore = false;
	unsigned int specPowerCount = 0;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == vuInstr(VU_OP_NOP) || mnemonic == vuInstr(VU_OP_WAITP) || mnemonic == vuInstr(VU_OP_WAITQ) )
			continue;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				continue;

			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 1 )
			{
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 3 )
			{
				pattern.materialDiffReg = dst;
				pattern.materialDiffOffset = offset;
				pattern.materialDiffFromInput = true;
				haveMaterialDiffInputLoad = true;
				continue;
			}
			if( base != pattern.inputReg && (offset == 0 || offset == 1) )
			{
				if( pattern.outputReg.empty() || pattern.outputReg == base )
				{
					pattern.outputReg = base;
					pattern.currentColorReg = dst;
					pattern.colorOffset = offset;
					haveColorLoad = true;
				}
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst != src )
				continue;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				continue;
			if( dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
			}
			else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SUB) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, left)
			    && getRegisterArgKey(token, 2, right) && right == pattern.vertexReg )
			{
				pattern.toLightReg = dst;
				pattern.lightPosReg = left;
				haveSub = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( left == pattern.toLightReg && right == pattern.toLightReg )
			{
				pattern.attenReg = dst;
				haveDistanceMul = true;
				continue;
			}
			if( left == pattern.attenReg )
			{
				pattern.attenProductReg = dst;
				pattern.attenCoeffReg = right;
				haveAttenProduct = true;
				continue;
			}
			if( left == pattern.normalizedLightReg && right == pattern.normalReg )
			{
				pattern.normalProductReg = dst;
				haveNormalProduct = true;
				continue;
			}
			if( left == pattern.halfAngleReg && right == pattern.normalReg )
			{
				pattern.specProductReg = dst;
				haveSpecProduct = true;
				continue;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADDA) && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveDistanceAdday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && getRegisterArgKey(token, 2, product)
			    && dst == pattern.attenReg && product == pattern.attenReg )
			{
				pattern.onesReg = src;
				haveDistanceMaddx = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQRT) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveSqrt = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && token.broadcast() == Token::W && tokenHasFields(token, Token::X) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetOne = true;
			continue;
		}

		Token::Argument qArg("");
		const bool addReadsQ = getArg(token, 2, qArg) && qArg.type() == Token::Argument::Q;
		if( (mnemonic == vuInstr(VU_OP_ADDQ) || (mnemonic == vuInstr(VU_OP_ADD) && addReadsQ))
		    && tokenHasFields(token, Token::Y) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetLength = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				continue;
			if( denom == pattern.attenReg )
			{
				if( !haveInverseDistanceDiv )
					haveInverseDistanceDiv = true;
				else
					haveAttenuationDiv = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULQ) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.toLightReg )
			{
				pattern.normalizedLightReg = dst;
				haveNormalize = true;
			}
			else if( src == pattern.litColorReg )
			{
				pattern.attenuatedColorReg = dst;
				haveAttenuateColor = true;
			}
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_MULA) || mnemonic == vuInstrBroadcastName(VU_OP_MULA, "x")) && token.broadcast() == Token::X
		    && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 2, src) )
				continue;
			if( src == pattern.attenProductReg && !haveAttenMulax )
				haveAttenMulax = true;
			else if( src == pattern.normalProductReg && !haveIntensityMulax )
				haveIntensityMulax = true;
			else if( src == pattern.specProductReg )
				haveSpecMulax = true;
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_MADDA) || mnemonic == vuInstrBroadcastName(VU_OP_MADDA, "y")) && token.broadcast() == Token::Y
		    && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( !getRegisterArgKey(token, 2, src) )
				continue;
			if( src == pattern.attenProductReg && !haveAttenMadday )
				haveAttenMadday = true;
			else if( src == pattern.normalProductReg && !haveIntensityMadday )
				haveIntensityMadday = true;
			else if( src == pattern.specProductReg )
				haveSpecMadday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::Z && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 2, src) )
				continue;
			if( src == pattern.attenProductReg && !haveAttenMaddz )
			{
				if( dst == pattern.attenReg )
					haveAttenMaddz = true;
			}
			else if( src == pattern.normalProductReg && !haveIntensityMaddz )
			{
				pattern.intensityReg = dst;
				haveIntensityMaddz = true;
			}
			else if( src == pattern.specProductReg )
			{
				pattern.specIntensityReg = dst;
				haveSpecMaddz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.intensityReg )
			{
				pattern.clampedIntensityReg = dst;
				haveDiffuseClamp = true;
			}
			else if( src == pattern.specIntensityReg )
			{
				pattern.specIntensityReg = dst;
				haveSpecClamp = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( right == pattern.clampedIntensityReg )
			{
				pattern.localDiffuseReg = dst;
				pattern.lightDiffReg = left;
				haveLocalDiffuse = true;
			}
			else if( left == pattern.halfAngleReg && right == pattern.halfAngleReg )
			{
				haveHalfNormalize = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				continue;
			if( src == pattern.localDiffuseReg )
			{
				if( pattern.materialDiffFromInput )
				{
					if( material != pattern.materialDiffReg )
						continue;
				}
				else
				{
					pattern.materialDiffReg = material;
				}
				haveDiffuseMaterial = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( left == pattern.currentColorReg && right == pattern.attenuatedColorReg )
			{
				pattern.resultReg = dst;
				haveAccumulate = true;
			}
			else if( right == pattern.normalizedLightReg && !haveHalfAdd )
			{
				pattern.halfAngleReg = dst;
				pattern.viewDirReg = left;
				haveHalfAdd = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ESADD) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.halfAngleReg )
				haveHalfEsadd = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MFP) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.halfAngleReg )
			{
				if( !haveHalfMfpLength )
					haveHalfMfpLength = true;
				else
					haveHalfMfpInv = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ERSQRT) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.halfAngleReg )
				haveHalfErsqrt = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0 && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( left == right && (left == pattern.specIntensityReg || left == pattern.specPowerReg) )
			{
				pattern.specPowerReg = dst;
				++specPowerCount;
				haveSpecPower = true;
			}
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_MADDA) || mnemonic == vuInstrBroadcastName(VU_OP_MADDA, "w")) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) )
			{
				pattern.localSpecReg = src;
				haveSpecMaterial = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			pattern.litColorReg = dst;
			pattern.lightAmbReg = left;
			pattern.materialAmbReg = right;
			haveAmbientMaterial = true;
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_SQ) || mnemonic == vuInstr(VU_OP_SQI)) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			bool haveAddress = getIndirectBaseAndOffset(token, base, offset);
			Token::Argument addressArg("");
			if( !haveAddress && getArg(token, 1, addressArg)
			    && (addressArg.flags() & Token::Argument::POSTINC)
			    && getRegisterArgKey(token, 1, base) )
			{
				offset = 0;
				haveAddress = true;
			}
			if( !haveAddress || !getRegisterArgKey(token, 0, src) )
				continue;
			if( base == pattern.outputReg && src == pattern.resultReg )
			{
				if( mnemonic == vuInstr(VU_OP_SQI) || (getArg(token, 1, addressArg)
				    && (addressArg.flags() & Token::Argument::POSTINC)) )
					pattern.postIncrementStore = true;
				pattern.storeOffset = offset;
				haveStore = true;
			}
			continue;
		}
	}

	bool ok = haveBranch
	    && haveInputIncrement
	    && (haveOutputIncrement || pattern.postIncrementStore)
	    && haveNormalLoad
	    && haveVertexLoad
	    && haveColorLoad
	    && haveSub
	    && haveDistanceMul
	    && haveDistanceAdday
	    && haveDistanceMaddx
	    && haveSqrt
	    && haveDistanceSetOne
	    && haveDistanceSetLength
	    && haveInverseDistanceDiv
	    && haveNormalize
	    && haveAttenProduct
	    && haveAttenMulax
	    && haveAttenMadday
	    && haveAttenMaddz
	    && haveHalfAdd
	    && haveHalfEsadd
	    && haveHalfMfpLength
	    && haveHalfErsqrt
	    && haveHalfMfpInv
	    && haveHalfNormalize
	    && haveNormalProduct
	    && haveIntensityMulax
	    && haveIntensityMadday
	    && haveIntensityMaddz
	    && haveDiffuseClamp
	    && haveLocalDiffuse
	    && haveDiffuseMaterial
	    && haveSpecProduct
	    && haveSpecMulax
	    && haveSpecMadday
	    && haveSpecMaddz
	    && haveSpecClamp
	    && haveSpecPower
	    && specPowerCount >= 5
	    && haveSpecMaterial
	    && haveAmbientMaterial
	    && haveAttenuationDiv
	    && haveAttenuateColor
	    && haveAccumulate
	    && haveStore
	    && !pattern.materialDiffFromInput
	    && ((pattern.materialDiffFromInput && haveMaterialDiffInputLoad && pattern.inputStep == 4
	         && pattern.materialDiffOffset == 3)
	        || (!pattern.materialDiffFromInput && pattern.inputStep == 3))
	    && (pattern.postIncrementStore || pattern.outputStep == 3)
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && ((pattern.postIncrementStore && pattern.colorOffset == 0 && pattern.storeOffset == 0)
	        || (!pattern.postIncrementStore && pattern.colorOffset == 1 && pattern.storeOffset == 1))
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.lightPosReg.empty()
	    && !pattern.attenCoeffReg.empty()
	    && !pattern.onesReg.empty()
	    && !pattern.lightDiffReg.empty()
	    && !pattern.materialDiffReg.empty()
	    && !pattern.viewDirReg.empty()
	    && !pattern.localSpecReg.empty()
	    && !pattern.lightAmbReg.empty()
	    && !pattern.materialAmbReg.empty();
	return ok;
}

void CodeGenerator::emitPtLightSpecSoftwarePipelineLoop( const PtLightSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vf00 = "VF00";
	const std::string inputStep = integerText(p.inputStep);
	const std::string inputStepOne = integerText(p.inputStep);
	const std::string inputStepTwo = integerText(p.inputStep * 2);
	const std::string inputStepThree = integerText(p.inputStep * 3);
	const std::string mainOutputStep = p.postIncrementStore ? vuInstr(VU_OP_NOP) : vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3";
	const std::string mainColorLoad = p.postIncrementStore ? offsetBase(0, out) : offsetBase(-2, out);
	const std::string mainStore = p.postIncrementStore
	                            ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + "r22, (" + out + "++)"
	                            : vuInstrPrefixFields(VU_OP_SQ, "xyz") + "r22, " + offsetBase(-2, out);

	std::list<std::string> reserved;
	addScratchReservation(reserved, p.lightPosReg);
	addScratchReservation(reserved, p.attenCoeffReg);
	addScratchReservation(reserved, p.onesReg);
	addScratchReservation(reserved, p.lightDiffReg);
	addScratchReservation(reserved, p.materialDiffReg);
	addScratchReservation(reserved, p.viewDirReg);
	addScratchReservation(reserved, p.localSpecReg);
	addScratchReservation(reserved, p.lightAmbReg);
	addScratchReservation(reserved, p.materialAmbReg);

	const std::string r05 = reserveScratchReg(reserved);
	const std::string r06 = reserveScratchReg(reserved);
	const std::string r07 = reserveScratchReg(reserved);
	const std::string r14 = reserveScratchReg(reserved);
	const std::string r15 = reserveScratchReg(reserved);
	const std::string r16 = reserveScratchReg(reserved);
	const std::string r17 = reserveScratchReg(reserved);
	const std::string r18 = reserveScratchReg(reserved);
	const std::string r19 = reserveScratchReg(reserved);
	const std::string r20 = reserveScratchReg(reserved);
	const std::string r21 = reserveScratchReg(reserved);
	const std::string r22 = reserveScratchReg(reserved);
	const std::string r23 = reserveScratchReg(reserved);
	const std::string color = reserveScratchReg(reserved);

	const std::string store = p.postIncrementStore
	                        ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + r22 + ", (" + out + "++)"
	                        : vuInstrPrefixFields(VU_OP_SQ, "xyz") + r22 + ", " + offsetBase(-2, out);

	if( p.materialDiffFromInput )
	{
		const std::string materialLoad = vuInstrPrefixFields(VU_OP_LQ, "xyz") + r22 + ", "
		                               + offsetBase(p.materialDiffOffset - (2 * p.inputStep), in);
		const std::string normalLoad = vuInstrPrefixFields(VU_OP_LQ, "xyz") + r19 + ", "
		                             + offsetBase(p.normalOffset - p.inputStep, in);
		const std::string pvStore = p.postIncrementStore
		                          ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + r19 + ", (" + out + "++)"
		                          : vuInstrPrefixFields(VU_OP_SQ, "xyz") + r19 + ", " + offsetBase(-2, out);

		m_codeLines.push_back(p.entryLabel + ":");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + r16 + ", " + offsetBase(0, in));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r16 + ", " + p.lightPosReg + ", " + r16, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r15 + ", " + r16 + ", " + r16, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r15 + ", " + fieldArg(r15, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r15 + ", " + p.onesReg + ", " + fieldArg(r15, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r15, "z"));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r15 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r15, "y"));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r15 + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r16 + ", " + r16 + ", q",
		                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackOneLabel);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r15 + ", " + r15 + ", " + p.attenCoeffReg, vuInstr(VU_OP_NOP));

		m_codeLines.push_back(p.prologOneLabel + ":");
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r17 + ", " + p.viewDirReg + ", " + r16,
		                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r18 + ", " + offsetBase(0, in));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r18 + ", " + p.lightPosReg + ", " + r18,
		                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r17);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r18 + ", " + r18, normalLoad);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r20 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r20 + ", " + p.onesReg + ", " + fieldArg(r20, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r20 + ", " + vf00 + ", " + fieldArg(vf00, "w"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r20, "z"));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r20 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r20, "y"));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r15 + ", " + r20 + ", " + p.attenCoeffReg,
		                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r14 + ", " + r15);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r16 + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r16 + ", " + r18 + ", q",
		                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r18 + ", " + r17);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackTwoLabel);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "x"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

		m_codeLines.push_back(p.prologTwoLabel + ":");
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r17 + ", " + p.viewDirReg + ", " + r16,
		                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r22 + ", " + offsetBase(0, in));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r21 + ", " + r18 + ", " + fieldArg(r06, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r18 + ", " + p.lightPosReg + ", " + r22, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "y"),
		                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r17);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r06 + ", " + vf00 + ", " + fieldArg(r20, "z"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r21 + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r18 + ", " + r18, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r06 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r20 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r20 + ", " + p.onesReg + ", " + fieldArg(r20, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r07 + ", " + vf00 + ", " + fieldArg(r21, "z"), vuInstr(VU_OP_WAITP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r21 + ", " + p.lightDiffReg + ", " + fieldArg(r05, "w"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r14, "x"),
		                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r20, "z"));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r07 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r20 + ", " + vf00 + ", " + fieldArg(vf00, "w"),
		                  vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r05 + ", " + r05, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r20 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06,
		                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r20, "y"));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r14, "y"), normalLoad);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r05 + ", " + vf00 + ", " + fieldArg(r14, "z"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06, materialLoad);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r15 + ", " + r20 + ", " + p.attenCoeffReg,
		                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r14 + ", " + r15);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r16 + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r16 + ", " + r18 + ", q",
		                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r06 + ", " + r06,
		                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r18 + ", " + r17);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + r21 + ", " + r22,
		                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackThreeLabel);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "x"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

		m_codeLines.push_back(p.mainLabel + ":");
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r17 + ", " + p.viewDirReg + ", " + r16,
		                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r22 + ", " + offsetBase(0, in));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r07 + ", " + r07, mainOutputStep);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r21 + ", " + r18 + ", " + fieldArg(r06, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r18 + ", " + p.lightPosReg + ", " + r22,
		                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r17);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(r07, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r06 + ", " + vf00 + ", " + fieldArg(r20, "z"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r21 + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r18 + ", " + r18, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + r19 + ", " + p.lightAmbReg + ", " + p.materialAmbReg, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r06 + ", " + fieldArg(vf00, "x"),
		                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r05, "w"));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r20 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r20 + ", " + p.onesReg + ", " + fieldArg(r20, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r07 + ", " + vf00 + ", " + fieldArg(r21, "z"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + mainColorLoad);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r19 + ", " + r19 + ", q",
		                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r20, "z"));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r21 + ", " + p.lightDiffReg + ", " + fieldArg(r05, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r07 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r14, "x"),
		                  vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r19 + ", " + color + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r20 + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r05 + ", " + r05, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r20 + ", " + vf00 + ", q", materialLoad);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r14, "y"), pvStore);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r05 + ", " + vf00 + ", " + fieldArg(r14, "z"),
		                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r14 + ", " + r15);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06, normalLoad);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r15 + ", " + r20 + ", " + p.attenCoeffReg,
		                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r20, "y"));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + r21 + ", " + r22, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r16 + ", " + r19, vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r16 + ", " + r18 + ", q",
		                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
		emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r06 + ", " + r06,
		                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r18 + ", " + r17);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
		emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "x"),
		                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

		m_codeLines.push_back(p.drainLabel + ":");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

		m_codeLines.push_back(p.fallbackOneLabel + ":");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepOne);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

		m_codeLines.push_back(p.fallbackTwoLabel + ":");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepTwo);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

		m_codeLines.push_back(p.fallbackThreeLabel + ":");
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

		emitPtLightSpecScalarFallbackLoop(p);
		return;
	}

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + r17 + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r17 + ", " + p.lightPosReg + ", " + r17, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r16 + ", " + r17 + ", " + r17, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r16 + ", " + fieldArg(r16, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r16 + ", " + p.onesReg + ", " + fieldArg(r16, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r16, "z"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r16 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r16, "y"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r16 + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r17 + ", " + r17 + ", q",
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r16 + ", " + r16 + ", " + p.attenCoeffReg, vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologOneLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r18 + ", " + p.viewDirReg + ", " + r17,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r19 + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r19 + ", " + p.lightPosReg + ", " + r19,
	                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r18);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r19 + ", " + r19,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r20 + ", " + offsetBase(p.normalOffset - p.inputStep, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r21 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r21 + ", " + p.onesReg + ", " + fieldArg(r21, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r21 + ", " + vf00 + ", " + fieldArg(vf00, "w"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r21, "z"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r21 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r21, "y"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r16 + ", " + r21 + ", " + p.attenCoeffReg,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r15 + ", " + r16);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r17 + ", " + r20, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r17 + ", " + r19 + ", q",
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r19 + ", " + r18);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "x"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

	m_codeLines.push_back(p.prologTwoLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r18 + ", " + p.viewDirReg + ", " + r17,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r23 + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r22 + ", " + r19 + ", " + fieldArg(r06, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r19 + ", " + p.lightPosReg + ", " + r23, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "y"),
	                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r18);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r06 + ", " + vf00 + ", " + fieldArg(r21, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r22 + ", " + r20, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r19 + ", " + r19, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r21 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r21 + ", " + p.onesReg + ", " + fieldArg(r21, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r07 + ", " + vf00 + ", " + fieldArg(r20, "z"), vuInstr(VU_OP_WAITP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r06 + ", " + fieldArg(vf00, "x"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r15, "x"),
	                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r21, "z"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r15, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r07 + ", " + r07 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r20 + ", " + p.lightDiffReg + ", " + fieldArg(r05, "w"),
	                  vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r07 + ", " + r07, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r21 + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r21, "y"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r21 + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + r20 + ", " + p.materialDiffReg,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r20 + ", " + offsetBase(p.normalOffset - p.inputStep, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r05 + ", " + vf00 + ", " + fieldArg(r15, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r16 + ", " + r21 + ", " + p.attenCoeffReg,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r15 + ", " + r16);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r17 + ", " + r20, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r17 + ", " + r19 + ", q",
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r06 + ", " + r06,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r19 + ", " + r18);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackThreeLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "x"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r18 + ", " + p.viewDirReg + ", " + r17,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r23 + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r07 + ", " + r07, mainOutputStep);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r22 + ", " + r19 + ", " + fieldArg(r06, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + r19 + ", " + p.lightPosReg + ", " + r23,
	                  vuInstrPrefix(VU_OP_ESADD) + "p, " + r18);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(r07, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r06 + ", " + vf00 + ", " + fieldArg(r21, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r20 + ", " + r22 + ", " + r20, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r19 + ", " + r19, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + r22 + ", " + p.lightAmbReg + ", " + p.materialAmbReg,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r05, "w"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r05 + ", " + r06 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + r21 + ", " + fieldArg(r21, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + r21 + ", " + p.onesReg + ", " + fieldArg(r21, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r20, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r07 + ", " + vf00 + ", " + fieldArg(r20, "z"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r23 + ", " + r22 + ", q",
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + color + ", " + mainColorLoad);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r15, "x"),
	                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(r21, "z"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(r15, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + r07 + ", " + r07 + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + r20 + ", " + p.lightDiffReg + ", " + fieldArg(r05, "w"),
	                  vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(r06, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + r22 + ", " + color + ", " + r23, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + r21 + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r07 + ", " + r07, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + r21 + ", " + vf00 + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + r20 + ", " + p.materialDiffReg, store);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + r05 + ", " + vf00 + ", " + fieldArg(r15, "z"),
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r15 + ", " + r16);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + r20 + ", " + offsetBase(p.normalOffset - p.inputStep, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r16 + ", " + r21 + ", " + p.attenCoeffReg,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(r21, "y"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r06 + ", " + r06 + ", " + r06, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + r21 + ", " + r17 + ", " + r20, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + r17 + ", " + r19 + ", q",
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + inputStep);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + r07 + ", " + r06 + ", " + r06,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + r19 + ", " + r18);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(r21, "x"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + r06 + ", p");

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepOne);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepTwo);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackThreeLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", " + inputStepThree);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	emitPtLightSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitPtLightSpecScalarFallbackLoop( const PtLightSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string vf00 = "VF00";
	const std::string scalarOutputStep = p.postIncrementStore ? vuInstr(VU_OP_NOP) : vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3";
	const std::string scalarColorLoad = offsetBase(p.colorOffset, out);
	const std::string scalarStore = p.postIncrementStore
	                              ? vuInstrPrefixFields(VU_OP_SQI, "xyz") + p.resultReg + ", (" + out + "++)"
	                              : vuInstrPrefixFields(VU_OP_SQ, "xyz") + p.resultReg + ", " + offsetBase(p.storeOffset, out);
	std::list<std::string> reserved;
	addScratchReservation(reserved, p.vertexReg);
	addScratchReservation(reserved, p.normalReg);
	addScratchReservation(reserved, p.currentColorReg);
	addScratchReservation(reserved, p.toLightReg);
	addScratchReservation(reserved, p.attenReg);
	addScratchReservation(reserved, p.attenProductReg);
	addScratchReservation(reserved, p.normalizedLightReg);
	addScratchReservation(reserved, p.halfAngleReg);
	addScratchReservation(reserved, p.normalProductReg);
	addScratchReservation(reserved, p.intensityReg);
	addScratchReservation(reserved, p.clampedIntensityReg);
	addScratchReservation(reserved, p.localDiffuseReg);
	addScratchReservation(reserved, p.specProductReg);
	addScratchReservation(reserved, p.specIntensityReg);
	addScratchReservation(reserved, p.specPowerReg);
	addScratchReservation(reserved, p.litColorReg);
	addScratchReservation(reserved, p.attenuatedColorReg);
	addScratchReservation(reserved, p.resultReg);
	addScratchReservation(reserved, p.lightPosReg);
	addScratchReservation(reserved, p.attenCoeffReg);
	addScratchReservation(reserved, p.onesReg);
	addScratchReservation(reserved, p.lightDiffReg);
	addScratchReservation(reserved, p.materialDiffReg);
	addScratchReservation(reserved, p.viewDirReg);
	addScratchReservation(reserved, p.localSpecReg);
	addScratchReservation(reserved, p.lightAmbReg);
	addScratchReservation(reserved, p.materialAmbReg);
	const std::string scalarColor = reserveScratchReg(reserved);

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + p.vertexReg + ", " + offsetBase(p.vertexOffset, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + p.normalReg + ", " + offsetBase(p.normalOffset, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + scalarColor + ", " + scalarColorLoad);
	if( p.materialDiffFromInput )
		emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + p.materialDiffReg + ", "
		                         + offsetBase(p.materialDiffOffset, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", " + integerText(p.inputStep));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + p.toLightReg + ", " + p.lightPosReg + ", " + p.vertexReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + p.attenReg + ", " + p.toLightReg + ", " + p.toLightReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + p.attenReg + ", " + fieldArg(p.attenReg, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + p.attenReg + ", " + p.onesReg + ", " + fieldArg(p.attenReg, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + p.attenReg + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(p.attenReg, "z"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + p.attenReg + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(p.attenReg, "y"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + p.normalizedLightReg + ", " + p.toLightReg + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + p.attenProductReg + ", " + p.attenReg + ", " + p.attenCoeffReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(p.attenProductReg, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(p.attenProductReg, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.attenReg + ", " + vf00 + ", " + fieldArg(p.attenProductReg, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + p.halfAngleReg + ", " + p.viewDirReg + ", " + p.normalizedLightReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + p.normalProductReg + ", " + p.normalizedLightReg + ", " + p.normalReg,
	                  vuInstrPrefix(VU_OP_ESADD) + "p, " + p.halfAngleReg);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(p.normalProductReg, "x"), vuInstr(VU_OP_WAITP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(p.normalProductReg, "y"),
	                  vuInstrPrefixFields(VU_OP_MFP, "w") + p.halfAngleReg + ", p");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.intensityReg + ", " + vf00 + ", " + fieldArg(p.normalProductReg, "z"),
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(p.attenReg, "w"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ERSQRT) + "p, " + fieldArg(p.halfAngleReg, "w"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + p.clampedIntensityReg + ", " + p.intensityReg + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_WAITP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_MFP, "w") + p.halfAngleReg + ", p");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + p.localDiffuseReg + ", " + p.lightDiffReg + ", " + fieldArg(p.clampedIntensityReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + p.halfAngleReg + ", " + p.halfAngleReg + ", " + fieldArg(p.halfAngleReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + p.localDiffuseReg + ", " + p.materialDiffReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + p.specProductReg + ", " + p.halfAngleReg + ", " + p.normalReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(p.specProductReg, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(p.specProductReg, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.specIntensityReg + ", " + vf00 + ", " + fieldArg(p.specProductReg, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + p.specIntensityReg + ", " + p.specIntensityReg + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + p.specPowerReg + ", " + p.specIntensityReg + ", " + p.specIntensityReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + p.specPowerReg + ", " + p.specPowerReg + ", " + p.specPowerReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + p.specPowerReg + ", " + p.specPowerReg + ", " + p.specPowerReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + p.specPowerReg + ", " + p.specPowerReg + ", " + p.specPowerReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "w") + p.specPowerReg + ", " + p.specPowerReg + ", " + p.specPowerReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "w", "xyz") + "ACC, " + p.localSpecReg + ", " + fieldArg(p.specPowerReg, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + p.litColorReg + ", " + p.lightAmbReg + ", " + p.materialAmbReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + p.attenuatedColorReg + ", " + p.litColorReg + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + p.resultReg + ", " + scalarColor + ", " + p.attenuatedColorReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), scalarStore);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), scalarOutputStep);

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitPtLightNoSpecSoftwarePipelineLoop( std::list<Token>& tokens,
                                                              std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "pt_light_vert_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	PtLightNoSpecLoopPipelinePattern pattern;
	if( !collectPtLightNoSpecLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitPtLightNoSpecSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectPtLightNoSpecLoopPipelinePattern( std::list<Token>::iterator begin,
                                                             std::list<Token>::iterator end,
                                                             PtLightNoSpecLoopPipelinePattern& pattern )
{
	pattern = PtLightNoSpecLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.drainLabel = pattern.sourceLabel + "__DRAIN_TAIL";
	pattern.fallbackOneLabel = pattern.sourceLabel + "__FALLBACK1";
	pattern.fallbackTwoLabel = pattern.sourceLabel + "__FALLBACK2";
	pattern.scalarLabel = pattern.sourceLabel + "__SCALAR_FALLBACK";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveOutputIncrement = false;
	bool haveNormalLoad = false;
	bool haveVertexLoad = false;
	bool haveColorLoad = false;
	bool haveSub = false;
	bool haveDistanceMul = false;
	bool haveDistanceAdday = false;
	bool haveDistanceMaddx = false;
	bool haveSqrt = false;
	bool haveDistanceSetOne = false;
	bool haveDistanceSetLength = false;
	bool haveInverseDistanceDiv = false;
	bool haveNormalize = false;
	bool haveAttenProduct = false;
	bool haveAttenMulax = false;
	bool haveAttenMadday = false;
	bool haveAttenMaddz = false;
	bool haveNormalProduct = false;
	bool haveIntensityMulax = false;
	bool haveIntensityMadday = false;
	bool haveIntensityMaddz = false;
	bool haveClamp = false;
	bool haveLocalDiffuse = false;
	bool haveDiffuseMaterial = false;
	bool haveAmbientMaterial = false;
	bool haveAttenuationDiv = false;
	bool haveAttenuateColor = false;
	bool haveAccumulate = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);
		if( mnemonic == vuInstr(VU_OP_NOP) )
			continue;
		if( mnemonic == vuInstr(VU_OP_ESADD) || mnemonic == vuInstr(VU_OP_ERSQRT) || mnemonic == vuInstr(VU_OP_MFP) || mnemonic == vuInstr(VU_OP_WAITP) )
			return false;

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
			continue;

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst)
			    || !tokenHasFields(token, Token::X | Token::Y | Token::Z) )
				continue;

			if( base == pattern.inputReg && offset == 0 )
			{
				pattern.vertexReg = dst;
				pattern.vertexOffset = offset;
				haveVertexLoad = true;
				continue;
			}
			if( base == pattern.inputReg && offset == 1 )
			{
				pattern.normalReg = dst;
				pattern.normalOffset = offset;
				haveNormalLoad = true;
				continue;
			}
			if( base != pattern.inputReg && offset == 1 )
			{
				if( pattern.outputReg.empty() || pattern.outputReg == base )
				{
					pattern.outputReg = base;
					pattern.currentColorReg = dst;
					pattern.colorOffset = offset;
					haveColorLoad = true;
				}
				continue;
			}
			return false;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst != src )
				continue;
			long value = 0;
			if( !evaluateIntegerExpression(imm, value) )
				continue;
			if( dst == pattern.inputReg )
			{
				pattern.inputStep = value;
				haveInputIncrement = true;
			}
			else if( !pattern.outputReg.empty() && dst == pattern.outputReg )
			{
				pattern.outputStep = value;
				haveOutputIncrement = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SUB) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( right == pattern.vertexReg )
			{
				pattern.toLightReg = dst;
				pattern.lightPosReg = left;
				haveSub = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( !pattern.toLightReg.empty() && left == pattern.toLightReg && right == pattern.toLightReg )
			{
				pattern.attenReg = dst;
				haveDistanceMul = true;
				continue;
			}
			if( !pattern.attenReg.empty() && left == pattern.attenReg )
			{
				pattern.attenProductReg = dst;
				pattern.attenCoeffReg = right;
				haveAttenProduct = true;
				continue;
			}
			if( !pattern.normalizedLightReg.empty()
			    && left == pattern.normalizedLightReg
			    && right == pattern.normalReg )
			{
				pattern.normalProductReg = dst;
				haveNormalProduct = true;
				continue;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADDA) && token.broadcast() == Token::Y && tokenHasFields(token, Token::Z) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveDistanceAdday = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::X && tokenHasFields(token, Token::Z) )
		{
			std::string dst;
			std::string src;
			std::string product;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getRegisterArgKey(token, 2, product) )
				continue;
			if( dst == pattern.attenReg && product == pattern.attenReg )
			{
				pattern.onesReg = src;
				haveDistanceMaddx = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQRT) )
		{
			std::string src;
			if( getRegisterArgKey(token, 1, src) && src == pattern.attenReg )
				haveSqrt = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && token.broadcast() == Token::W && tokenHasFields(token, Token::X) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetOne = true;
			continue;
		}

		Token::Argument qArg("");
		const bool addReadsQ = getArg(token, 2, qArg) && qArg.type() == Token::Argument::Q;
		if( (mnemonic == vuInstr(VU_OP_ADDQ) || (mnemonic == vuInstr(VU_OP_ADD) && addReadsQ))
		    && tokenHasFields(token, Token::Y) )
		{
			std::string dst;
			if( getRegisterArgKey(token, 0, dst) && dst == pattern.attenReg )
				haveDistanceSetLength = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_DIV) )
		{
			std::string denom;
			if( !getRegisterArgKey(token, 2, denom) )
				continue;
			if( denom == pattern.attenReg )
			{
				if( !haveInverseDistanceDiv )
					haveInverseDistanceDiv = true;
				else
					haveAttenuationDiv = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULQ) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( src == pattern.toLightReg )
			{
				pattern.normalizedLightReg = dst;
				haveNormalize = true;
			}
			else if( src == pattern.litColorReg )
			{
				pattern.attenuatedColorReg = dst;
				haveAttenuateColor = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( getRegisterArgKey(token, 2, src) )
			{
				if( src == pattern.attenProductReg )
					haveAttenMulax = true;
				else if( src == pattern.normalProductReg )
					haveIntensityMulax = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADDA) && token.broadcast() == Token::Y && tokenHasFields(token, Token::W) )
		{
			std::string src;
			if( getRegisterArgKey(token, 2, src) )
			{
				if( src == pattern.attenProductReg )
					haveAttenMadday = true;
				else if( src == pattern.normalProductReg )
					haveIntensityMadday = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == Token::Z && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 2, src) )
				continue;
			if( src == pattern.attenProductReg )
			{
				if( dst != pattern.attenReg )
					continue;
				haveAttenMaddz = true;
			}
			else if( src == pattern.normalProductReg )
			{
				pattern.intensityReg = dst;
				haveIntensityMaddz = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MAX) && token.broadcast() == Token::X && tokenHasFields(token, Token::W) )
		{
			std::string dst;
			std::string src;
			if( getRegisterArgKey(token, 0, dst) && getRegisterArgKey(token, 1, src)
			    && src == pattern.intensityReg )
			{
				pattern.clampedIntensityReg = dst;
				haveClamp = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MUL) && token.broadcast() == Token::W
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( right == pattern.clampedIntensityReg )
			{
				pattern.localDiffuseReg = dst;
				pattern.lightDiffReg = left;
				haveLocalDiffuse = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MULA) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string src;
			std::string material;
			if( !getRegisterArgKey(token, 1, src) || !getRegisterArgKey(token, 2, material) )
				continue;
			if( src == pattern.localDiffuseReg )
			{
				pattern.materialDiffReg = material;
				haveDiffuseMaterial = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_MADD) && token.broadcast() == 0
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			pattern.litColorReg = dst;
			pattern.lightAmbReg = left;
			pattern.materialAmbReg = right;
			haveAmbientMaterial = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_ADD) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string left;
			std::string right;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, left)
			    || !getRegisterArgKey(token, 2, right) )
				continue;
			if( left == pattern.currentColorReg && right == pattern.attenuatedColorReg )
			{
				pattern.resultReg = dst;
				haveAccumulate = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;
			if( base == pattern.outputReg && src == pattern.resultReg )
			{
				pattern.storeOffset = offset;
				haveStore = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveOutputIncrement
	    && haveNormalLoad
	    && haveVertexLoad
	    && haveColorLoad
	    && haveSub
	    && haveDistanceMul
	    && haveDistanceAdday
	    && haveDistanceMaddx
	    && haveSqrt
	    && haveDistanceSetOne
	    && haveDistanceSetLength
	    && haveInverseDistanceDiv
	    && haveNormalize
	    && haveAttenProduct
	    && haveAttenMulax
	    && haveAttenMadday
	    && haveAttenMaddz
	    && haveNormalProduct
	    && haveIntensityMulax
	    && haveIntensityMadday
	    && haveIntensityMaddz
	    && haveClamp
	    && haveLocalDiffuse
	    && haveDiffuseMaterial
	    && haveAmbientMaterial
	    && haveAttenuationDiv
	    && haveAttenuateColor
	    && haveAccumulate
	    && haveStore
	    && pattern.inputStep == 3
	    && pattern.outputStep == 3
	    && pattern.vertexOffset == 0
	    && pattern.normalOffset == 1
	    && pattern.colorOffset == 1
	    && pattern.storeOffset == 1
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.outputReg.empty()
	    && !pattern.normalReg.empty()
	    && !pattern.vertexReg.empty()
	    && !pattern.currentColorReg.empty()
	    && !pattern.lightPosReg.empty()
	    && !pattern.toLightReg.empty()
	    && !pattern.attenReg.empty()
	    && !pattern.attenCoeffReg.empty()
	    && !pattern.attenProductReg.empty()
	    && !pattern.onesReg.empty()
	    && !pattern.normalizedLightReg.empty()
	    && !pattern.normalProductReg.empty()
	    && !pattern.intensityReg.empty()
	    && !pattern.clampedIntensityReg.empty()
	    && !pattern.lightDiffReg.empty()
	    && !pattern.localDiffuseReg.empty()
	    && !pattern.materialDiffReg.empty()
	    && !pattern.lightAmbReg.empty()
	    && !pattern.materialAmbReg.empty()
	    && !pattern.litColorReg.empty()
	    && !pattern.attenuatedColorReg.empty()
	    && !pattern.resultReg.empty();
}

void CodeGenerator::emitPtLightNoSpecSoftwarePipelineLoop( const PtLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string vertex = p.vertexReg;
	const std::string currentColor = p.currentColorReg;
	const std::string lightPos = p.lightPosReg;
	const std::string delta = p.toLightReg;
	const std::string atten = p.attenReg;
	const std::string attenProduct = p.attenProductReg;
	const std::string normalized = p.normalizedLightReg;
	const std::string lit = p.litColorReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + delta + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + lightPos + ", " + delta, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + atten + ", " + delta + ", " + delta, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + atten + ", " + fieldArg(atten, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(atten, "z"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + atten + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackOneLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + lightPos + ", " + vertex,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + vertex + ", " + delta);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"),
	                  vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + atten + ", " + delta + ", " + delta, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + vertex + ", " + vertex + ", q",
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(-5, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + atten + ", " + fieldArg(atten, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + vertex + ", " + vertex + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"),
	                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(atten, "z"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.materialAmbReg + ", " + vf00 + ", " + fieldArg(attenProduct, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(vertex, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(vertex, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.materialDiffReg + ", " + vf00 + ", " + fieldArg(vertex, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + atten + ", " + vf00 + ", q",
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.fallbackTwoLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + p.materialDiffReg + ", " + p.materialDiffReg + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 6");

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + atten + ", " + p.lightDiffReg + ", " + fieldArg(p.materialDiffReg, "w"),
	                  vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + lightPos + ", " + vertex,
	                  vuInstrPrefixFields(VU_OP_MOVE, "xyz") + vertex + ", " + delta);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + atten + ", " + p.materialDiffReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + atten + ", " + delta + ", " + delta, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + vertex + ", " + vertex + ", q",
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(p.materialAmbReg, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg,
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(-2, in));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + atten + ", " + fieldArg(atten, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + vertex + ", " + vertex + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.materialAmbReg + ", " + vf00 + ", " + fieldArg(attenProduct, "z"),
	                  vuInstrPrefixFields(VU_OP_LQ, "xyz") + attenProduct + ", " + offsetBase(-8, out));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + lit + ", " + lit + ", q",
	                  vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(atten, "z"));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(vertex, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(vertex, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + p.materialDiffReg + ", " + vf00 + ", " + fieldArg(vertex, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + attenProduct + ", " + attenProduct + ", " + lit, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + atten + ", " + vf00 + ", q",
	                  vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + p.materialDiffReg + ", " + p.materialDiffReg + ", " + fieldArg(vf00, "x"),
	                  vuInstrPrefixFields(VU_OP_SQ, "xyz") + attenProduct + ", " + offsetBase(-8, out));

	m_codeLines.push_back(p.drainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 6");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + out + ", " + out + ", 6");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.fallbackTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_ISUBIU) + in + ", " + in + ", 6");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	emitPtLightNoSpecScalarFallbackLoop(p);
}

void CodeGenerator::emitPtLightNoSpecScalarFallbackLoop( const PtLightNoSpecLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string out = p.outputReg;
	const std::string last = p.lastInputReg;
	const std::string normal = p.normalReg;
	const std::string vertex = p.vertexReg;
	const std::string currentColor = p.currentColorReg;
	const std::string delta = p.toLightReg;
	const std::string atten = p.attenReg;
	const std::string attenProduct = p.attenProductReg;
	const std::string normalized = p.normalizedLightReg;
	const std::string normalProduct = p.normalProductReg;
	const std::string intensity = p.intensityReg;
	const std::string clamped = p.clampedIntensityReg;
	const std::string localDiffuse = p.localDiffuseReg;
	const std::string lit = p.litColorReg;
	const std::string attenuated = p.attenuatedColorReg;
	const std::string result = p.resultReg;
	const std::string vf00 = "VF00";

	m_codeLines.push_back(p.scalarLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + normal + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + vertex + ", " + offsetBase(0, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + currentColor + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_SUB, "xyz") + delta + ", " + p.lightPosReg + ", " + vertex, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + atten + ", " + delta + ", " + delta, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADDA, "y", "z") + "ACC, " + atten + ", " + fieldArg(atten, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "x", "z") + atten + ", " + p.onesReg + ", " + fieldArg(atten, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_ADD, "w", "x") + atten + ", " + vf00 + ", " + fieldArg(vf00, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQRT) + "q, " + fieldArg(atten, "z"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADDQ, "y") + atten + ", " + vf00 + ", q", vuInstr(VU_OP_WAITQ));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + attenProduct + ", " + atten + ", " + p.attenCoeffReg,
	                  vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "y"));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(attenProduct, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + atten + ", " + vf00 + ", " + fieldArg(attenProduct, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + normalized + ", " + delta + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_DIV) + "q, " + fieldArg(vf00, "w") + ", " + fieldArg(atten, "w"));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MUL, "xyz") + normalProduct + ", " + normalized + ", " + normal, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MULA, "x", "w") + "ACC, " + vf00 + ", " + fieldArg(normalProduct, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADDA, "y", "w") + "ACC, " + vf00 + ", " + fieldArg(normalProduct, "y"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MADD, "z", "w") + intensity + ", " + vf00 + ", " + fieldArg(normalProduct, "z"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MAX, "x", "w") + clamped + ", " + intensity + ", " + fieldArg(vf00, "x"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixBroadcastFields(VU_OP_MUL, "w", "xyz") + localDiffuse + ", " + p.lightDiffReg + ", " + fieldArg(clamped, "w"), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULA, "xyz") + "ACC, " + localDiffuse + ", " + p.materialDiffReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MADD, "xyz") + lit + ", " + p.lightAmbReg + ", " + p.materialAmbReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MULQ, "xyz") + attenuated + ", " + lit + ", q", vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_ADD, "xyz") + result + ", " + currentColor + ", " + attenuated, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_SQ, "xyz") + result + ", " + offsetBase(1, out));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.scalarLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + out + ", " + out + ", 3");

	m_codeLines.push_back(p.exitLabel + ":");
}

bool CodeGenerator::tryEmitFinalColorSoftwarePipelineLoop( std::list<Token>& tokens,
                                                           std::list<Token>::iterator& token )
{
	if( token == tokens.end() )
		return false;
	if( token->label() != "final_loop_lid" )
		return false;

	std::list<Token>::iterator branch = tokens.end();
	for( std::list<Token>::iterator i = token; i != tokens.end(); ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == token->label() )
		{
			branch = i;
			break;
		}
		if( i != token && i->label().length() != 0 )
			return false;
	}
	if( branch == tokens.end() )
		return false;

	std::list<Token>::iterator afterBranch = branch;
	++afterBranch;
	if( afterBranch != tokens.end() && (afterBranch->flags() & Token::BRANCH_DELAY_FILLER) )
		++afterBranch;

	FinalColorLoopPipelinePattern pattern;
	if( !collectFinalColorLoopPipelinePattern(token, afterBranch, pattern) )
		return false;

	emitFinalColorSoftwarePipelineLoop(pattern);
	token = afterBranch;
	return true;
}

bool CodeGenerator::collectFinalColorLoopPipelinePattern( std::list<Token>::iterator begin,
                                                          std::list<Token>::iterator end,
                                                          FinalColorLoopPipelinePattern& pattern )
{
	pattern = FinalColorLoopPipelinePattern();
	pattern.sourceLabel = begin->label();
	pattern.entryLabel = pattern.sourceLabel + "__ENTRY_POINT";
	pattern.prologLabel = pattern.sourceLabel + "__PRO1";
	pattern.mainLabel = pattern.sourceLabel + "__MAIN_LOOP";
	pattern.epilogTwoLabel = pattern.sourceLabel + "__EPI0";
	pattern.epilogOneLabel = pattern.sourceLabel + "__EPI1";
	pattern.exitLabel = pattern.sourceLabel + "__EXIT_POINT";

	bool haveBranch = false;
	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		std::string target;
		if( branchTargetLabel(*i, target) && target == pattern.sourceLabel )
		{
			if( !getRegisterArgKey(*i, 0, pattern.inputReg)
			    || !getRegisterArgKey(*i, 1, pattern.lastInputReg) )
				return false;
			haveBranch = true;
			break;
		}
	}
	if( !haveBranch )
		return false;

	bool haveInputIncrement = false;
	bool haveLoad = false;
	bool haveMini = false;
	bool haveFtoi = false;
	bool haveStore = false;

	for( std::list<Token>::iterator i = begin; i != end; ++i )
	{
		const Token& token = *i;
		if( !token.operand() || (token.flags() & Token::IGNORED)
		    || (token.operand()->flags() & Operand::PREPROCESSOR) )
			continue;

		const std::string mnemonic = lowerVuTokenName(token);

		std::string target;
		if( branchTargetLabel(token, target) && target == pattern.sourceLabel )
		{
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_LQ) )
		{
			std::string base;
			long offset = 0;
			std::string dst;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, dst) )
				continue;
			if( !pattern.inputReg.empty()
			    && base == pattern.inputReg
			    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
			{
				pattern.colorReg = dst;
				if( pattern.colorReg == pattern.minReg )
				{
					pattern.minReg = "VF25";
					pattern.outputReg = "VF26";
				}
				else if( pattern.colorReg == pattern.outputReg )
				{
					pattern.outputReg = "VF26";
				}
				pattern.loadOffset = offset;
				haveLoad = true;
			}
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_IADDIU) )
		{
			std::string dst;
			std::string src;
			std::string imm;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src)
			    || !getImmediateArg(token, 2, imm) )
				continue;
			if( dst == src && !pattern.inputReg.empty() && dst == pattern.inputReg )
			{
				long value = 0;
				if( !evaluateIntegerExpression(imm, value) )
					continue;
				pattern.inputStep = value;
				haveInputIncrement = true;
			}
			continue;
		}

		if( (mnemonic == vuInstr(VU_OP_MINI) || mnemonic == vuInstr(VU_OP_MINII))
		    && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src == pattern.colorReg )
				haveMini = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_FTOI0) && tokenHasFields(token, Token::X | Token::Y | Token::Z) )
		{
			std::string dst;
			std::string src;
			if( !getRegisterArgKey(token, 0, dst) || !getRegisterArgKey(token, 1, src) )
				continue;
			if( !pattern.colorReg.empty() && dst == pattern.colorReg && src == pattern.colorReg )
				haveFtoi = true;
			continue;
		}

		if( mnemonic == vuInstr(VU_OP_SQ) )
		{
			std::string base;
			long offset = 0;
			std::string src;
			if( !getIndirectBaseAndOffset(token, base, offset)
			    || !getRegisterArgKey(token, 0, src) )
				continue;
			if( !pattern.inputReg.empty() && base == pattern.inputReg && src == pattern.colorReg )
			{
				pattern.storeOffsetAfterIncrement = offset;
				haveStore = true;
			}
			continue;
		}
	}

	return haveBranch
	    && haveInputIncrement
	    && haveLoad
	    && haveMini
	    && haveFtoi
	    && haveStore
	    && pattern.inputStep == 3
	    && pattern.loadOffset == 1
	    && pattern.storeOffsetAfterIncrement == -2
	    && !pattern.inputReg.empty()
	    && !pattern.lastInputReg.empty()
	    && !pattern.colorReg.empty()
	    && !pattern.minReg.empty()
	    && !pattern.outputReg.empty();
}

void CodeGenerator::emitFinalColorSoftwarePipelineLoop( const FinalColorLoopPipelinePattern& p )
{
	const std::string in = p.inputReg;
	const std::string last = p.lastInputReg;
	const std::string output = p.colorReg;
	const std::string minReg = p.minReg;
	const std::string load = p.outputReg;

	m_codeLines.push_back(p.entryLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + output + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MINII, "xyz") + minReg + ", " + output + ", i",
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.epilogOneLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.prologLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + load + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + output + ", " + minReg,
	                  vuInstrPrefix(VU_OP_IBEQ) + in + ", " + last + ", " + p.epilogTwoLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MINII, "xyz") + minReg + ", " + load + ", i", vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.mainLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefixFields(VU_OP_LQ, "xyz") + load + ", " + offsetBase(1, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_IADDIU) + in + ", " + in + ", 3");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + output + ", " + offsetBase(-8, in));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + output + ", " + minReg,
	                  vuInstrPrefix(VU_OP_IBNE) + in + ", " + last + ", " + p.mainLabel);
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_MINII, "xyz") + minReg + ", " + load + ", i", vuInstr(VU_OP_NOP));

	m_codeLines.push_back(p.epilogTwoLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + output + ", " + minReg,
	                  vuInstrPrefix(VU_OP_SQ) + output + ", " + offsetBase(-5, in));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_B) + p.exitLabel);
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + output + ", " + offsetBase(-2, in));

	m_codeLines.push_back(p.epilogOneLabel + ":");
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstrPrefixFields(VU_OP_FTOI0, "xyz") + output + ", " + minReg, vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstr(VU_OP_NOP));
	emitRawPairedLine(vuInstr(VU_OP_NOP), vuInstrPrefix(VU_OP_SQ) + output + ", " + offsetBase(-2, in));

	m_codeLines.push_back(p.exitLabel + ":");
}

void CodeGenerator::emitRawPairedLine( const std::string& upper, const std::string& lower )
{
	m_codeLines.push_back( formatRawPairedInstructionLine( upper, lower ) );
	++m_currentCycle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dual-pipe pairing helpers.
//
// VU1 issues two instructions per cycle: one upper-pipe (FMAC) and one
// lower-pipe (LSU / IALU / BRU / FDIV / RANDU / EFU).  Sony's vcl fills both
// slots per cycle for ~2x throughput; openvcl historically emits a single
// instruction per cycle with NOP on the unused pipe.  The scheduler now does
// a small, conservative straight-line lookahead to fill latency gaps and pair
// independent upper/lower instructions without crossing labels, control flow,
// memory side effects, or register/resource dependencies.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::tokensCanPair( const Token& a, const Token& b ) const
{
	if( !isVuEmittableInstruction(a) || !isVuEmittableInstruction(b) )
		return false;

	// PREORDERED tokens (raw .vsm passthrough) must keep their
	// emission position exactly as written.
	if( (a.flags() & Token::PREORDERED) || (b.flags() & Token::PREORDERED) )
		return false;

	// Must straddle the upper/lower pipe split.
	if( tokenIsLowerExecutionPath(a) == tokenIsLowerExecutionPath(b) )
		return false;

	// FMAC writes MAC with 4-cycle latency; clipw additionally writes
	// CLIP.  Keep same-flag readers out of the pair.  A non-clip FMAC
	// can still pair with a CLIP reader such as fcand once the previous
	// clipw result is latency-ready.
	bool aFMAC = (a.operand()->unit() == Operand::FMAC) || emitsAsUpperMove(a);
	bool bFMAC = (b.operand()->unit() == Operand::FMAC) || emitsAsUpperMove(b);
	return vuTokenPairResourcesAreIndependent(a, b, aFMAC, bFMAC);
}

std::string CodeGenerator::formatPairedLine( const Token& upper, const Token& lower )
{
	std::string upperInstr = generateInstruction(upper);
	std::string lowerInstr = generateInstruction(lower);
	return formatRawPairedInstructionLine( upperInstr, lowerInstr );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateInstruction(const Token& token)
{
	std::string codeLine;

	if( emitsAsUpperMove(token) )
		return generateUpperMoveInstruction(token);

	codeLine = generateOperand(token);
	codeLine += " ";

	for(std::list<Token::Argument>::const_iterator i = token.arguments().begin(); i != token.arguments().end(); ++i)
	{
		Token::Argument arg = *i;
		std::string currentArg;

		switch(arg.type())
		{
			case Token::Argument::FLOAT_REGISTER: currentArg = registerArg(arg,token); break;
			case Token::Argument::INTEGER_REGISTER: currentArg = registerArg(arg,token); break; 
			case Token::Argument::IMMEDIATE: currentArg = immediateArg(arg,token); break;
			case Token::Argument::Q: currentArg = "q"; break;
			case Token::Argument::ACCUMULATOR: currentArg = accumulatorArg(arg,token); break;
			case Token::Argument::P: currentArg = "p"; break;
			case Token::Argument::I: currentArg = "i"; break;
			case Token::Argument::R: currentArg = "r"; break;
			case Token::Argument::STRING: currentArg = immediateArg(arg,token); break;

			default: currentArg = "<<< ERROR : BUG IN TOKENIZER : ERROR >>>"; break;
		}

		// try to solve this some other way.
		i++;

		if(i != token.arguments().end())
			currentArg += ", "; 

		i--;

		codeLine += currentArg;
	}

	return codeLine;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateUpperMoveInstruction( const Token& token )
{
	static const char* fieldnames = "xyzw";
	std::string fields;
	for(unsigned int t = 0; t < 4; t++ )
	{
		if(token.fields() & (1<<t))
			fields += fieldnames[t];
	}
	// No suffix on the source means all four, so name them: emitting the empty mask
	// verbatim produces `max.` with nothing after the dot, which dvp-as rejects.
	if( fields.empty() )
		fields = fieldnames;

	std::list<Token::Argument>::const_iterator dst = token.arguments().begin();
	std::list<Token::Argument>::const_iterator src = dst;
	++src;

	return vuInstrFields( VU_OP_MAX, fields, registerArg(*dst, token) + ", "
	                                      + registerArg(*src, token) + ", "
	                                      + registerArg(*src, token) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::registerArg(const Token::Argument& arg, const Token& token)
{
	static const char* fieldnames = "xyzw";
	unsigned int fields = cleanFields( arg.fields(), arg.flags(), token );
	std::string argument;

	if( arg.flags() & Token::Argument::INDIRECT )
	{
		// Evaluate the immediate expression if EVALUATE flag is set
		if( arg.flags() & Token::Argument::EVALUATE )
		{
			Expression e;
			e.setCustomOperators( Math::mathOperators() );
			if( e.process( arg.immediate() ) && e.solve() )
			{
				// VU memory offsets must be integers - truncate like C integer division
				std::stringstream s;
				s << static_cast<long>(e.result());
				argument += s.str();
			}
			else
				argument += arg.immediate();
		}
		else
			argument += arg.immediate();
		argument += "(";
		if( arg.flags() & Token::Argument::PREDEC )
			argument += "--";
	}

	if( arg.content() == Token::Argument::ALIAS )
	{
		assert( arg.dependency() );
		assert( arg.dependency()->alias() );
		assert( arg.dependency()->alias()->allocatedRegister() );
		argument += arg.dependency()->alias()->allocatedRegister()->name();
	}
	else
	{
		const char* prefix = ((arg.type() == Token::Argument::FLOAT_REGISTER) ? "VF" : "VI");
		std::stringstream s;
		s << prefix << std::setw(2) << std::setfill('0') << arg.regNumber();
		argument += s.str();
	}

	if( arg.flags() & Token::Argument::INDIRECT )
	{
		if( arg.flags() & Token::Argument::POSTINC )
			argument += "++";
		argument += ")";
	}

	if( !(arg.flags() & Token::Argument::ROTATE) )
	{
		for(unsigned int t = 0; t < 4; t++)
		{
			if(fields & (1<<t))
				argument += fieldnames[t];
		}
	}

	return argument;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::immediateArg(const Token::Argument& arg, const Token& /*token*/)
{
	if( arg.flags() & Token::Argument::EVALUATE )
	{
		Expression e;

		e.setCustomOperators( Math::mathOperators() );

		if( !e.process( arg.immediate() ) )
			return arg.immediate();

		if( !e.solve() )
			return arg.immediate();

		std::stringstream s;
		if( arg.flags() & Token::Argument::RAW )
		{
			// RAW flag (for LOI): output float as IEEE 754 hex representation
			float f = static_cast<float>(e.result());
			union { float f; unsigned int u; } conv;
			conv.f = f;
			s << "0x" << std::hex << std::setfill('0') << std::setw(8) << conv.u;
		}
		else
		{
			// Integer immediate: truncate like C integer division
			s << static_cast<long>(e.result());
		}
		return s.str();
	}
	else return arg.immediate();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::accumulatorArg( const Token::Argument& arg, const Token& token )
{
	static const char* fieldnames = "xyzw";
	unsigned int fields = cleanFields( arg.fields(), arg.flags(), token );
	std::string argument;

	argument = "ACC";

	for(unsigned int t = 0; t < 4; t++)
	{
		if(fields & (1<<t))
			argument += fieldnames[t];
	}

	return argument;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int CodeGenerator::cleanFields( unsigned int fields, unsigned int flags, const Token& token )
{
	if( fields )
	{
		// remove unnecessary fields


		if( !(flags & (Token::Argument::BROADCAST|Token::Argument::FLAG)) )
		{
			if( token.fields() )
			{
				if( (token.fields() == fields) )
					fields = 0;
			}
			if( (token.operand()->flags() & Operand::XYZ) == Operand::XYZ )
			{
//				if( fields == (Token::X|Token::Y|Token::Z) )
//					fields = 0;
			}
			else if( token.operand()->flags() & Operand::DEST )
			{
				if( fields == (Token::X|Token::Y|Token::Z|Token::W) )
					fields = 0;
			}
		}
	}
	else if( token.broadcast() && !(flags & Token::Argument::FLAG) && (flags & Token::Argument::BROADCAST) )
	{
		// set the broadcast-field for the register

		fields = token.broadcast();
	}

	return fields;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string CodeGenerator::generateOperand(const Token& token)
{
	static const char* fieldnames = "xyzw";
	std::string operand = "";

	if(!token.operand())
		return operand; 

	operand = token.operand()->name();
	makelower(operand);

	//add broadcast
	if((token.operand()->flags() & Operand::BROADCAST) == Operand::BROADCAST)
	{
		for(unsigned int t = 0; t < 4; t++ )
		{
			if( token.broadcast() & (1<<t) )
				operand += fieldnames[t];
		}
	}

	// add flags
	if(token.flags() & (Token::E|Token::D|Token::T))
	{
		operand += "[";
		if( token.flags() & Token::E) operand += "E";
		if( token.flags() & Token::D) operand += "D";
		if( token.flags() & Token::T) operand += "T";
		operand += "]";
	}

	unsigned int fields = token.fields();
	if( !fields
	    && token.operand()->unit() == Operand::FMAC
	    && (token.operand()->flags() & Operand::DEST) )
	{
		fields = Token::X | Token::Y | Token::Z | Token::W;
	}

	//generate fileds
	if(fields)
	{
		operand += ".";
		for(unsigned int t = 0; t < 4; t++ )
		{
			if(fields & (1<<t))
				operand += fieldnames[t];
		}
	}

	return operand;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CodeGenerator::write(std::ostream& stream)
{
	for(std::list<std::string>::const_iterator i = m_codeLines.begin(); i != m_codeLines.end(); ++i)
		stream << (*i) << std::endl;

	return true;
}

}
