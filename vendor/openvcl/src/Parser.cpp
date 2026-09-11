/*
 * Parser.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Parser.h"
#include "Error.h"
#include "OpenVclVersion.h"
#include "VsmCostAnalyzer.h"
#include "VuInstructionInfo.h"
#include "VuSchedulerAnalysis.h"
#include "VuSchedulingRules.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

#include <fcntl.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

namespace
{
	std::string trimManifestText( const std::string& text )
	{
		std::string::size_type first = 0;
		while( first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r' || text[first] == '\n') )
			++first;

		std::string::size_type last = text.size();
		while( last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r' || text[last - 1] == '\n') )
			--last;
		return text.substr( first, last - first );
	}

	std::string stripManifestComment( const std::string& line )
	{
		std::string::size_type pos = line.find( '#' );
		if( pos == std::string::npos )
			return line;
		return line.substr( 0, pos );
	}

	bool costCompareMetricValue( const VsmCostAnalyzer::Summary& summary,
	                             const std::string& metric,
	                             unsigned int& value )
	{
		if( metric == "static" || metric == "static_cycles" )
		{
			value = summary.staticCycles;
			return true;
		}
		if( metric == "estimated" || metric == "estimated_total_cycles" )
		{
			value = summary.estimatedTotalCycles;
			return true;
		}
		if( metric == "weighted-static" || metric == "weighted_static_cycles" )
		{
			value = summary.weightedStaticCycles;
			return true;
		}
		if( metric == "weighted-estimated" || metric == "weighted_estimated_total_cycles" )
		{
			value = summary.weightedEstimatedTotalCycles;
			return true;
		}
		if( metric == "affine-static-base" || metric == "affine_static_base_cycles" )
		{
			value = summary.affineStaticBaseCycles;
			return true;
		}
		if( metric == "affine-static-loop" || metric == "affine_static_loop_cycles" )
		{
			value = summary.affineStaticLoopCycles;
			return true;
		}
		if( metric == "affine-estimated-base" || metric == "affine_estimated_base_cycles" )
		{
			value = summary.affineEstimatedBaseCycles;
			return true;
		}
		if( metric == "affine-estimated-loop" || metric == "affine_estimated_loop_cycles" )
		{
			value = summary.affineEstimatedLoopCycles;
			return true;
		}
		return false;
	}

	struct FlagName
	{
		unsigned int flag;
		const char* name;
	};

	const FlagName kOperandFlags[] =
	{
		{ Operand::UPPER, "upper" },
		{ Operand::LOWER, "lower" },
		{ Operand::DEST, "dest" },
		{ Operand::BROADCAST, "broadcast" },
		{ Operand::XYZ, "xyz" },
		{ Operand::MULTI, "multi" },
		{ Operand::DYNAMIC, "dynamic" },
		{ Operand::IWRITE, "iwrite" },
		{ Operand::FILTERED, "filtered" },
		{ 0, 0 }
	};

	const FlagName kInstructionFlags[] =
	{
		{ VU_INSTR_BRANCH, "branch" },
		{ VU_INSTR_WAIT_Q, "wait_q" },
		{ VU_INSTR_WAIT_P, "wait_p" },
		{ VU_INSTR_WRITES_Q, "writes_q" },
		{ VU_INSTR_WRITES_P, "writes_p" },
		{ VU_INSTR_WRITES_I, "writes_i" },
		{ VU_INSTR_XGKICK, "xgkick" },
		{ VU_INSTR_UNCONDITIONAL_BRANCH, "unconditional_branch" },
		{ VU_INSTR_LINK_BRANCH, "link_branch" },
		{ VU_INSTR_REGISTER_BRANCH, "register_branch" },
		{ 0, 0 }
	};

	const FlagName kResourceFlags[] =
	{
		{ VU_RESOURCE_ACC, "acc" },
		{ VU_RESOURCE_I, "i" },
		{ VU_RESOURCE_Q, "q" },
		{ VU_RESOURCE_P, "p" },
		{ VU_RESOURCE_R, "r" },
		{ VU_RESOURCE_MAC, "mac" },
		{ VU_RESOURCE_CLIP, "clip" },
		{ 0, 0 }
	};

	const FlagName kMemoryFlags[] =
	{
		{ VU_MEMORY_FLAG_PREDEC, "predec" },
		{ VU_MEMORY_FLAG_POSTINC, "postinc" },
		{ 0, 0 }
	};

	const FlagName kBypassFlags[] =
	{
		{ VU_BYPASS_FTOI_TO_MTIR, "ftoi_to_mtir" },
		{ VU_BYPASS_LOAD_TO_FTOI, "load_to_ftoi" },
		{ VU_BYPASS_LOAD_TO_MINII, "load_to_minii" },
		{ 0, 0 }
	};

	const char* pipeName( VuPipelineSlot pipe )
	{
		switch( pipe )
		{
			case VU_PIPE_NOP: return "nop";
			case VU_PIPE_UPPER: return "upper";
			case VU_PIPE_LOWER: return "lower";
			default: return "unknown";
		}
	}

	const char* executionUnitName( VuExecutionUnit unit )
	{
		switch( unit )
		{
			case VU_EXEC_NOP: return "nop";
			case VU_EXEC_FMAC: return "fmac";
			case VU_EXEC_FDIV: return "fdiv";
			case VU_EXEC_LSU: return "lsu";
			case VU_EXEC_IALU: return "ialu";
			case VU_EXEC_BRU: return "bru";
			case VU_EXEC_RANDU: return "randu";
			case VU_EXEC_EFU: return "efu";
			default: return "unknown";
		}
	}

	const char* operandUnitName( Operand::Unit unit )
	{
		switch( unit )
		{
			case Operand::INVALID: return "invalid";
			case Operand::ENTER: return "enter";
			case Operand::EXIT: return "exit";
			case Operand::FMAC: return "fmac";
			case Operand::FDIV: return "fdiv";
			case Operand::LSU: return "lsu";
			case Operand::IALU: return "ialu";
			case Operand::BRU: return "bru";
			case Operand::RANDU: return "randu";
			case Operand::EFU: return "efu";
			default: return "unknown";
		}
	}

	const char* memoryKindName( VuMemoryKind kind )
	{
		switch( kind )
		{
			case VU_MEMORY_NONE: return "none";
			case VU_MEMORY_LOAD: return "load";
			case VU_MEMORY_STORE: return "store";
			case VU_MEMORY_XGKICK: return "xgkick";
			default: return "unknown";
		}
	}

	const char* basicBlockTerminatorKindName( VuBasicBlockTerminatorKind kind )
	{
		switch( kind )
		{
			case VU_BASIC_BLOCK_TERMINATOR_NONE: return "none";
			case VU_BASIC_BLOCK_TERMINATOR_BRANCH: return "branch";
			case VU_BASIC_BLOCK_TERMINATOR_XGKICK: return "xgkick";
			case VU_BASIC_BLOCK_TERMINATOR_BOUNDARY: return "boundary";
			case VU_BASIC_BLOCK_TERMINATOR_PREORDERED: return "preordered";
			default: return "unknown";
		}
	}

	void writeFlagListText( std::ostream& stream, unsigned int flags, const FlagName* names )
	{
		bool wrote = false;
		for( const FlagName* i = names; i->name; ++i )
		{
			if( (flags & i->flag) == i->flag )
			{
				if( wrote )
					stream << "|";
				stream << i->name;
				wrote = true;
			}
		}
		if( !wrote )
			stream << "-";
	}

	void writeJsonString( std::ostream& stream, const char* text )
	{
		stream << "\"";
		if( text )
		{
			for( const char* i = text; *i; ++i )
			{
				switch( *i )
				{
					case '\\': stream << "\\\\"; break;
					case '"': stream << "\\\""; break;
					case '\n': stream << "\\n"; break;
					case '\r': stream << "\\r"; break;
					case '\t': stream << "\\t"; break;
					default: stream << *i; break;
				}
			}
		}
		stream << "\"";
	}

	void writeFlagArrayJson( std::ostream& stream, unsigned int flags, const FlagName* names )
	{
		stream << "[";
		bool wrote = false;
		for( const FlagName* i = names; i->name; ++i )
		{
			if( (flags & i->flag) == i->flag )
			{
				if( wrote )
					stream << ", ";
				writeJsonString( stream, i->name );
				wrote = true;
			}
		}
		stream << "]";
	}

	void writeInstructionInfoText( std::ostream& stream )
	{
		stream << "OpenVCL VU instruction metadata" << std::endl;
		for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
		{
			const std::string parameters = vuInstructionParameterSummary( *info );
			stream << info->mnemonic
			       << " pipe=" << pipeName( info->pipe )
			       << " unit=" << executionUnitName( info->unit )
			       << " throughput=" << info->throughput
			       << " latency=" << info->latency
			       << " operand=" << info->operandName
			       << " args=" << info->arguments
			       << " parser_unit=" << operandUnitName( info->operandUnit )
			       << " parser_flags=";
			writeFlagListText( stream, info->operandFlags, kOperandFlags );
			stream << " pattern=\"" << info->operandPattern << "\""
			       << " parameters=\"" << parameters << "\""
			       << " description=\"" << vuInstructionDescription( *info ) << "\""
			       << " flags=";
			writeFlagListText( stream, info->flags, kInstructionFlags );
			stream << " reads=";
			writeFlagListText( stream, info->implicitReads, kResourceFlags );
			stream << " writes=";
			writeFlagListText( stream, info->implicitWrites, kResourceFlags );
			stream << " memory=" << memoryKindName( info->memoryKind )
			       << " memory_flags=";
			writeFlagListText( stream, info->memoryFlags, kMemoryFlags );
			stream << " branch_delay=" << info->branchDelaySlots
			       << " bypass=";
			writeFlagListText( stream, info->bypassFlags, kBypassFlags );
			stream << std::endl;
		}
	}

	void writeInstructionInfoJson( std::ostream& stream )
	{
		stream << "{\n  \"instructions\": [\n";
		for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
		{
			const std::string parameters = vuInstructionParameterSummary( *info );
			if( info != allVuInstructionInfos() )
				stream << ",\n";
			stream << "    {\n";
			stream << "      \"mnemonic\": "; writeJsonString( stream, info->mnemonic ); stream << ",\n";
			stream << "      \"description\": "; writeJsonString( stream, vuInstructionDescription( *info ) ); stream << ",\n";
			stream << "      \"pipe\": "; writeJsonString( stream, pipeName( info->pipe ) ); stream << ",\n";
			stream << "      \"unit\": "; writeJsonString( stream, executionUnitName( info->unit ) ); stream << ",\n";
			stream << "      \"throughput\": " << info->throughput << ",\n";
			stream << "      \"latency\": " << info->latency << ",\n";
			stream << "      \"operand\": {\n";
			stream << "        \"name\": "; writeJsonString( stream, info->operandName ); stream << ",\n";
			stream << "        \"arguments\": " << info->arguments << ",\n";
			stream << "        \"unit\": "; writeJsonString( stream, operandUnitName( info->operandUnit ) ); stream << ",\n";
			stream << "        \"flags\": "; writeFlagArrayJson( stream, info->operandFlags, kOperandFlags ); stream << ",\n";
			stream << "        \"pattern\": "; writeJsonString( stream, info->operandPattern ); stream << ",\n";
			stream << "        \"parameters\": "; writeJsonString( stream, parameters.c_str() ); stream << "\n";
			stream << "      },\n";
			stream << "      \"flags\": "; writeFlagArrayJson( stream, info->flags, kInstructionFlags ); stream << ",\n";
			stream << "      \"resources\": {\n";
			stream << "        \"implicit_reads\": "; writeFlagArrayJson( stream, info->implicitReads, kResourceFlags ); stream << ",\n";
			stream << "        \"implicit_writes\": "; writeFlagArrayJson( stream, info->implicitWrites, kResourceFlags ); stream << "\n";
			stream << "      },\n";
			stream << "      \"memory\": {\n";
			stream << "        \"kind\": "; writeJsonString( stream, memoryKindName( info->memoryKind ) ); stream << ",\n";
			stream << "        \"flags\": "; writeFlagArrayJson( stream, info->memoryFlags, kMemoryFlags ); stream << "\n";
			stream << "      },\n";
			stream << "      \"branch_delay_slots\": " << info->branchDelaySlots << ",\n";
			stream << "      \"bypass\": "; writeFlagArrayJson( stream, info->bypassFlags, kBypassFlags ); stream << "\n";
			stream << "    }";
		}
		stream << "\n  ]\n}" << std::endl;
	}

	void writeStringListText( std::ostream& stream, const std::list<std::string>& values )
	{
		bool wrote = false;
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( wrote )
				stream << "|";
			stream << *i;
			wrote = true;
		}
		if( !wrote )
			stream << "-";
	}

	void writeUnsignedVectorText( std::ostream& stream, const std::vector<unsigned int>& values )
	{
		if( values.empty() )
		{
			stream << "-";
			return;
		}

		for( unsigned int i = 0; i < values.size(); ++i )
		{
			if( i != 0 )
				stream << "|";
			stream << values[i];
		}
	}

	void writeStringListJson( std::ostream& stream, const std::list<std::string>& values )
	{
		stream << "[";
		bool wrote = false;
		for( std::list<std::string>::const_iterator i = values.begin(); i != values.end(); ++i )
		{
			if( wrote )
				stream << ", ";
			writeJsonString( stream, i->c_str() );
			wrote = true;
		}
		stream << "]";
	}

	void writeUnsignedVectorJson( std::ostream& stream, const std::vector<unsigned int>& values )
	{
		stream << "[";
		for( unsigned int i = 0; i < values.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << values[i];
		}
		stream << "]";
	}

	const char* loopQSchedulingStrategyName( VuLoopQSchedulingStrategy strategy )
	{
		switch( strategy )
		{
			case VU_LOOP_Q_SCHEDULE_LOCAL: return "local";
			case VU_LOOP_Q_SCHEDULE_LOOP_CARRIED: return "loop_carried";
			case VU_LOOP_Q_SCHEDULE_INSUFFICIENT: return "insufficient";
		}
		return "insufficient";
	}

	void writeQStageListText( std::ostream& stream, const std::vector<VuLoopQStage>& stages )
	{
		if( stages.empty() )
		{
			stream << "-";
			return;
		}

		for( unsigned int i = 0; i < stages.size(); ++i )
		{
			if( i != 0 )
				stream << "|";
			stream << stages[i].qProducerTokenIndex << "->";
			if( stages[i].qConsumerTokenIndices.empty() )
				stream << "-";
			else
			{
				for( unsigned int c = 0; c < stages[i].qConsumerTokenIndices.size(); ++c )
				{
					if( c != 0 )
						stream << ",";
					stream << stages[i].qConsumerTokenIndices[c];
				}
			}
			stream << "(latency=" << stages[i].qProducerLatency
			       << ",gap=" << stages[i].qProducerConsumerGapCycles
			       << ",deficit=" << stages[i].qProducerConsumerGapDeficitCycles
			       << ",loop_gap=" << stages[i].loopCarriedQGapCycles
			       << ",insert_gap=" << stages[i].qProducerInsertionGapCycles
			       << ",insert_deficit=" << stages[i].qProducerInsertionGapDeficitCycles
			       << ",strategy=" << loopQSchedulingStrategyName( stages[i].qSchedulingStrategy )
			       << ")";
		}
	}

	void writeQStageListJson( std::ostream& stream, const std::vector<VuLoopQStage>& stages )
	{
		stream << "[";
		for( unsigned int i = 0; i < stages.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << "{\"producer_token_index\": " << stages[i].qProducerTokenIndex
			       << ", \"consumer_token_indices\": ";
			writeUnsignedVectorJson( stream, stages[i].qConsumerTokenIndices );
			stream << ", \"producer_latency\": " << stages[i].qProducerLatency
			       << ", \"producer_consumer_gap_cycles\": " << stages[i].qProducerConsumerGapCycles
			       << ", \"producer_consumer_gap_deficit_cycles\": " << stages[i].qProducerConsumerGapDeficitCycles
			       << ", \"loop_carried_q_gap_cycles\": " << stages[i].loopCarriedQGapCycles
			       << ", \"producer_insertion_gap_cycles\": " << stages[i].qProducerInsertionGapCycles
			       << ", \"producer_insertion_gap_deficit_cycles\": " << stages[i].qProducerInsertionGapDeficitCycles
			       << ", \"q_scheduling_strategy\": ";
			writeJsonString( stream, loopQSchedulingStrategyName( stages[i].qSchedulingStrategy ) );
			stream
			       << "}";
		}
		stream << "]";
	}

	void writeRotationListText( std::ostream& stream, const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		bool wrote = false;
		for( std::vector<VuSoftwarePipelineRotation>::const_iterator i = rotations.begin(); i != rotations.end(); ++i )
		{
			if( wrote )
				stream << "|";
			stream << i->registerBase;
			if( i->hasScratchRegister )
				stream << "->" << i->scratchRegister;
			stream << "(in=";
			writeStringListText( stream, i->inputFields );
			stream << ",out=";
			writeStringListText( stream, i->outputFields );
			stream << ")";
			wrote = true;
		}
		if( !wrote )
			stream << "-";
	}

	void writeRotationListJson( std::ostream& stream, const std::vector<VuSoftwarePipelineRotation>& rotations )
	{
		stream << "[";
		for( unsigned int i = 0; i < rotations.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << "{";
			stream << "\"register\": "; writeJsonString( stream, rotations[i].registerBase.c_str() ); stream << ", ";
			stream << "\"scratch_available\": " << (rotations[i].hasScratchRegister ? "true" : "false") << ", ";
			stream << "\"scratch_register\": "; writeJsonString( stream, rotations[i].scratchRegister.c_str() ); stream << ", ";
			stream << "\"input_fields\": "; writeStringListJson( stream, rotations[i].inputFields ); stream << ", ";
			stream << "\"output_fields\": "; writeStringListJson( stream, rotations[i].outputFields );
			stream << "}";
		}
		stream << "]";
	}

	void writeSignedLongText( std::ostream& stream, long value )
	{
		if( value >= 0 )
			stream << "+";
		stream << value;
	}

	void writeSignedStepText( std::ostream& stream, const VuLoopInductionUpdate& update )
	{
		if( !update.stepKnown )
		{
			stream << "?";
			return;
		}
		writeSignedLongText( stream, update.step );
	}

	void writeInductionUpdateListText( std::ostream& stream, const std::vector<VuLoopInductionUpdate>& updates )
	{
		if( updates.empty() )
		{
			stream << "-";
			return;
		}

		for( unsigned int i = 0; i < updates.size(); ++i )
		{
			if( i != 0 )
				stream << "|";
			stream << updates[i].registerName << ":";
			writeSignedStepText( stream, updates[i] );
			stream << "@" << updates[i].tokenIndex;
		}
	}

	void writeInductionUpdateListJson( std::ostream& stream, const std::vector<VuLoopInductionUpdate>& updates )
	{
		stream << "[";
		for( unsigned int i = 0; i < updates.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << "{";
			stream << "\"register\": "; writeJsonString( stream, updates[i].registerName.c_str() ); stream << ", ";
			stream << "\"mnemonic\": "; writeJsonString( stream, updates[i].mnemonic.c_str() ); stream << ", ";
			stream << "\"immediate\": "; writeJsonString( stream, updates[i].immediate.c_str() ); stream << ", ";
			stream << "\"step_known\": " << (updates[i].stepKnown ? "true" : "false") << ", ";
			stream << "\"step\": " << updates[i].step << ", ";
			stream << "\"token_index\": " << updates[i].tokenIndex;
			stream << "}";
		}
		stream << "]";
	}

	void writePrefetchListText( std::ostream& stream, const std::vector<VuSoftwarePipelinePrefetch>& prefetches )
	{
		if( prefetches.empty() )
		{
			stream << "-";
			return;
		}

		for( unsigned int i = 0; i < prefetches.size(); ++i )
		{
			if( i != 0 )
				stream << "|";
			stream << prefetches[i].tokenIndex << ":" << prefetches[i].mnemonic
			       << "(" << memoryKindName( prefetches[i].memoryKind );
			if( prefetches[i].hasMemoryBase )
				stream << ",base=" << prefetches[i].memoryBaseRegister;
			if( prefetches[i].hasMemoryOffset )
			{
				stream << ",offset=";
				writeSignedLongText( stream, prefetches[i].memoryOffset );
			}
			stream << ",reads_induction=" << (prefetches[i].readsInductionRegister ? "yes" : "no");
			if( prefetches[i].readsInductionRegister )
				stream << ":" << prefetches[i].inductionRegister;
			if( prefetches[i].hasNextIterationOffset )
			{
				stream << ",next_offset=";
				writeSignedLongText( stream, prefetches[i].nextIterationOffset );
			}
			stream << ")";
		}
	}

	void writePrefetchListJson( std::ostream& stream, const std::vector<VuSoftwarePipelinePrefetch>& prefetches )
	{
		stream << "[";
		for( unsigned int i = 0; i < prefetches.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << "{";
			stream << "\"token_index\": " << prefetches[i].tokenIndex << ", ";
			stream << "\"mnemonic\": "; writeJsonString( stream, prefetches[i].mnemonic.c_str() ); stream << ", ";
			stream << "\"memory\": "; writeJsonString( stream, memoryKindName( prefetches[i].memoryKind ) ); stream << ", ";
			stream << "\"memory_base\": "; writeJsonString( stream, prefetches[i].hasMemoryBase ? prefetches[i].memoryBaseRegister.c_str() : "" ); stream << ", ";
			stream << "\"memory_offset_known\": " << (prefetches[i].hasMemoryOffset ? "true" : "false") << ", ";
			stream << "\"memory_offset\": " << prefetches[i].memoryOffset << ", ";
			stream << "\"reads_induction_register\": " << (prefetches[i].readsInductionRegister ? "true" : "false") << ", ";
			stream << "\"induction_register\": "; writeJsonString( stream, prefetches[i].inductionRegister.c_str() ); stream << ", ";
			stream << "\"next_iteration_offset_known\": " << (prefetches[i].hasNextIterationOffset ? "true" : "false") << ", ";
			stream << "\"next_iteration_offset\": " << prefetches[i].nextIterationOffset;
			stream << "}";
		}
		stream << "]";
	}

	void writeSuffixStoreListText( std::ostream& stream, const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		if( stores.empty() )
		{
			stream << "-";
			return;
		}

		for( unsigned int i = 0; i < stores.size(); ++i )
		{
			if( i != 0 )
				stream << "|";
			stream << stores[i].tokenIndex << ":" << stores[i].mnemonic << "(";
			if( stores[i].hasMemoryBase )
				stream << "base=" << stores[i].memoryBaseRegister;
			else
				stream << "base=?";
			if( stores[i].hasMemoryOffset )
			{
				stream << ",offset=";
				writeSignedLongText( stream, stores[i].memoryOffset );
			}
			stream << ",induction=" << (stores[i].usesInductionRegister ? "yes" : "no");
			if( stores[i].usesInductionRegister )
				stream << ":" << stores[i].inductionRegister;
			if( stores[i].hasNextIterationOffset )
			{
				stream << ",next_offset=";
				writeSignedLongText( stream, stores[i].nextIterationOffset );
			}
			if( stores[i].hasStoredValueRegister )
			{
				stream << ",value=" << stores[i].storedValueRegister << ",fields=";
				writeStringListText( stream, stores[i].storedValueFields );
			}
			else
			{
				stream << ",value=?";
			}
			stream << ",value_rotation=" << (stores[i].requiresValueRotation ? "yes" : "no");
			if( stores[i].hasValueScratchRegister )
				stream << ":" << stores[i].valueScratchRegister;
			stream << ",drain_candidate=" << (stores[i].drainCandidate ? "yes" : "no");
			stream << ",delayed_drain=" << (stores[i].delayedDrain ? "yes" : "no");
			if( stores[i].rotateValueBeforePrefetch )
				stream << ",rotate_before_prefetch=yes";
			if( stores[i].rotateValueAtStore )
				stream << ",rotate_at_store=yes";
			stream << ")";
		}
	}

	void writeSuffixStoreListJson( std::ostream& stream, const std::vector<VuSoftwarePipelineSuffixStore>& stores )
	{
		stream << "[";
		for( unsigned int i = 0; i < stores.size(); ++i )
		{
			if( i != 0 )
				stream << ", ";
			stream << "{";
			stream << "\"token_index\": " << stores[i].tokenIndex << ", ";
			stream << "\"mnemonic\": "; writeJsonString( stream, stores[i].mnemonic.c_str() ); stream << ", ";
			stream << "\"memory_base\": "; writeJsonString( stream, stores[i].hasMemoryBase ? stores[i].memoryBaseRegister.c_str() : "" ); stream << ", ";
			stream << "\"memory_offset_known\": " << (stores[i].hasMemoryOffset ? "true" : "false") << ", ";
			stream << "\"memory_offset\": " << stores[i].memoryOffset << ", ";
			stream << "\"uses_induction_register\": " << (stores[i].usesInductionRegister ? "true" : "false") << ", ";
			stream << "\"induction_register\": "; writeJsonString( stream, stores[i].inductionRegister.c_str() ); stream << ", ";
			stream << "\"next_iteration_offset_known\": " << (stores[i].hasNextIterationOffset ? "true" : "false") << ", ";
			stream << "\"next_iteration_offset\": " << stores[i].nextIterationOffset << ", ";
			stream << "\"stored_value_register\": "; writeJsonString( stream, stores[i].hasStoredValueRegister ? stores[i].storedValueRegister.c_str() : "" ); stream << ", ";
			stream << "\"stored_value_fields\": "; writeStringListJson( stream, stores[i].storedValueFields ); stream << ", ";
			stream << "\"value_rotation_required\": " << (stores[i].requiresValueRotation ? "true" : "false") << ", ";
			stream << "\"value_scratch_register\": "; writeJsonString( stream, stores[i].hasValueScratchRegister ? stores[i].valueScratchRegister.c_str() : "" ); stream << ", ";
			stream << "\"drain_candidate\": " << (stores[i].drainCandidate ? "true" : "false") << ", ";
			stream << "\"delayed_drain\": " << (stores[i].delayedDrain ? "true" : "false") << ", ";
			stream << "\"rotate_value_before_prefetch\": " << (stores[i].rotateValueBeforePrefetch ? "true" : "false") << ", ";
			stream << "\"rotate_value_at_store\": " << (stores[i].rotateValueAtStore ? "true" : "false");
			stream << "}";
		}
		stream << "]";
	}

	const VuSoftwarePipelineRewritePlan* findRewritePlanForOpportunity(
	    const std::vector<VuSoftwarePipelineRewritePlan>& plans,
	    const VuLoopPipelineOpportunity& opportunity )
	{
		for( std::vector<VuSoftwarePipelineRewritePlan>::const_iterator i = plans.begin(); i != plans.end(); ++i )
		{
			if( i->labelTokenIndex == opportunity.labelTokenIndex
			    && i->branchTokenIndex == opportunity.branchTokenIndex )
				return &*i;
		}
		return NULL;
	}

	void writeLoopPipelineInfoText( std::ostream& stream,
	                                const std::vector<VuLoopPipelineOpportunity>& opportunities,
	                                const std::vector<VuSoftwarePipelineRewritePlan>& rewritePlans )
	{
		stream << "OpenVCL VU loop pipeline opportunities" << std::endl;
		for( std::vector<VuLoopPipelineOpportunity>::const_iterator i = opportunities.begin(); i != opportunities.end(); ++i )
		{
			const VuSoftwarePipelineRewritePlan* rewritePlan = findRewritePlanForOpportunity( rewritePlans, *i );
			stream << i->label
			       << " q_producer_token=" << i->qProducerTokenIndex
			       << " q_producer_tokens=";
			writeUnsignedVectorText( stream, i->qProducerTokenIndices );
			stream << " q_stages=";
			writeQStageListText( stream, i->qStages );
			stream
			       << " first_q_consumer_token=" << i->firstQConsumerTokenIndex
			       << " last_q_consumer_token=" << i->lastQConsumerTokenIndex
			       << " q_consumers=" << i->qConsumerTokenIndices.size()
			       << " q_latency=" << i->qProducerLatency
			       << " q_producer_consumer_gap_cycles=" << i->qProducerConsumerGapCycles
			       << " q_producer_consumer_gap_deficit_cycles=" << i->qProducerConsumerGapDeficitCycles
			       << " loop_carried_q_gap_cycles=" << i->loopCarriedQGapCycles
			       << " q_producer_insertion_gap_cycles=" << i->qProducerInsertionGapCycles
			       << " q_producer_insertion_gap_deficit_cycles=" << i->qProducerInsertionGapDeficitCycles
			       << " q_scheduling_strategy=" << loopQSchedulingStrategyName( i->qSchedulingStrategy )
			       << " source_prefix_cycles=" << i->sourcePrefixCycles
			       << " source_suffix_cycles=" << i->sourceSuffixCycles
			       << " branch_delay_slots=" << i->branchDelaySlots
			       << " loop_cs_clid=" << i->loopCsClid
			       << " loop_cs_mlid=" << i->loopCsMlid
			       << " simple_counted_loop=" << (i->simpleCountedLoop ? "yes" : "no")
			       << " single_q_producer=" << (i->hasSingleQProducer ? "yes" : "no")
			       << " requires_prolog_epilog=" << (i->requiresPrologEpilog ? "yes" : "no")
			       << " requires_loop_carried_registers=" << (i->requiresLoopCarriedRegisters ? "yes" : "no")
			       << " q_live_out=" << (i->qLiveOut ? "yes" : "no")
			       << " memory_loads=" << i->memoryLoadCount
			       << " memory_stores=" << i->memoryStoreCount
			       << " memory_pre_post_increment=" << (i->hasMemoryPreOrPostIncrement ? "yes" : "no")
			       << " xgkick=" << (i->hasXgkick ? "yes" : "no")
			       << " eligible_single_q_pipeline=" << (i->eligibleSingleQSoftwarePipeline ? "yes" : "no")
			       << " pipeline_plan=" << (i->hasSoftwarePipelinePlan ? "yes" : "no")
			       << " pipeline_emittable=" << (i->canEmitSoftwarePipeline ? "yes" : "no")
			       << " eligible_multi_q_pipeline=" << (i->eligibleMultiQSoftwarePipeline ? "yes" : "no")
			       << " multi_q_pipeline_plan=" << (i->hasMultiQSoftwarePipelinePlan ? "yes" : "no")
			       << " multi_q_pipeline_emittable=" << (i->canEmitMultiQSoftwarePipeline ? "yes" : "no")
			       << " suffix_store_drain_plan=" << (i->hasSuffixStoreDrainPlan ? "yes" : "no")
			       << " suffix_store_drain_emittable=" << (i->canEmitSuffixStoreDrain ? "yes" : "no")
			       << " rewrite_plan=" << (rewritePlan ? "yes" : "no")
			       << " rewrite_prolog_label=" << (rewritePlan ? rewritePlan->prologLabel : "")
			       << " rewrite_main_label=" << (rewritePlan ? rewritePlan->mainLabel : "")
			       << " rewrite_drain_label=" << (rewritePlan ? rewritePlan->drainLabel : "")
			       << " rewrite_insert_prefetch_after=" << (rewritePlan ? rewritePlan->prefetchInsertAfterTokenIndex : 0)
			       << " rewrite_insert_q_after=" << (rewritePlan ? rewritePlan->qProducerInsertAfterTokenIndex : 0)
			       << " rewrite_q_in_branch_delay=" << (rewritePlan && rewritePlan->qProducerInBranchDelaySlot ? "yes" : "no")
			       << " rewrite_q_branch_delay_blocked_by_suffix_dependency="
			       << (rewritePlan && rewritePlan->qProducerBranchDelayBlockedBySuffixDependency ? "yes" : "no")
			       << " rewrite_q_branch_delay_suffix_blocker="
			       << (rewritePlan ? rewritePlan->qProducerBranchDelaySuffixBlockerTokenIndex : 0)
			       << " rewrite_emits_drain=" << (rewritePlan && rewritePlan->emitsDrain ? "yes" : "no")
			       << " rewrite_drains_suffix_stores=" << (rewritePlan && rewritePlan->drainsSuffixStores ? "yes" : "no")
			       << " rewrite_cyclic_prefix_before_branch="
			       << (rewritePlan && rewritePlan->cyclicPrefixBeforeBranch ? "yes" : "no")
			       << " rewrite_cyclic_prefix_guarded="
			       << (rewritePlan && rewritePlan->cyclicPrefixNeedsGuard ? "yes" : "no")
			       << " rewrite_cyclic_prefix_branch_delay="
			       << (rewritePlan && rewritePlan->cyclicPrefixLastTokenInBranchDelaySlot ? "yes" : "no")
			       << " rewrite_cyclic_prefix_insert_before="
			       << (rewritePlan ? rewritePlan->cyclicPrefixInsertBeforeTokenIndex : 0)
			       << " rewrite_prefetch_tokens=";
			if( rewritePlan )
				writeUnsignedVectorText( stream, rewritePlan->prefetchTokenIndices );
			else
				stream << "-";
			stream << " rewrite_suffix_store_descriptors=";
			if( rewritePlan )
				writeSuffixStoreListText( stream, rewritePlan->suffixStores );
			else
				stream << "-";
			stream << " prolog_tokens=";
			writeUnsignedVectorText( stream, i->prologTokenIndices );
			stream << " main_tokens=";
			writeUnsignedVectorText( stream, i->mainTokenIndices );
			stream << " drain_tokens=";
			writeUnsignedVectorText( stream, i->drainTokenIndices );
			stream << " blockers=";
			writeStringListText( stream, i->softwarePipelineBlockers );
			stream << " multi_q_prolog_tokens=";
			writeUnsignedVectorText( stream, i->multiQPrologTokenIndices );
			stream << " multi_q_main_tokens=";
			writeUnsignedVectorText( stream, i->multiQMainTokenIndices );
			stream << " multi_q_cyclic_prefix_tokens=";
			writeUnsignedVectorText( stream, i->multiQCyclicPrefixTokenIndices );
			stream << " multi_q_cyclic_prefix_rotations=";
			writeRotationListText( stream, i->multiQCyclicPrefixRotations );
			stream << " multi_q_cyclic_prefix_insert_before="
			       << i->multiQCyclicPrefixInsertBeforeTokenIndex;
			stream << " multi_q_cyclic_prefix_guarded="
			       << (i->multiQCyclicPrefixNeedsGuard ? "yes" : "no");
			stream << " multi_q_cyclic_prefix_branch_delay="
			       << (i->multiQCyclicPrefixLastTokenInBranchDelaySlot ? "yes" : "no");
			stream << " multi_q_blockers=";
			writeStringListText( stream, i->multiQSoftwarePipelineBlockers );
			stream << " suffix_store_drain_blockers=";
			writeStringListText( stream, i->suffixStoreDrainBlockers );
			stream << " rotated_registers=";
			writeStringListText( stream, i->softwarePipelineRotatedRegisters );
			stream << " rotation_descriptors=";
			writeRotationListText( stream, i->softwarePipelineRotations );
			stream << " prefetch_descriptors=";
			writePrefetchListText( stream, i->softwarePipelinePrefetches );
			stream << " suffix_store_descriptors=";
			writeSuffixStoreListText( stream, i->softwarePipelineSuffixStores );
			stream << " induction_registers=";
			writeStringListText( stream, i->inductionRegisters );
			stream << " induction_updates=";
			writeInductionUpdateListText( stream, i->inductionUpdates );
			stream << " loop_read_write_registers=";
			writeStringListText( stream, i->loopReadWriteRegisters );
			stream << " carried_q_inputs=";
			writeStringListText( stream, i->carriedQInputRegisters );
			stream << " carried_q_outputs=";
			writeStringListText( stream, i->carriedQOutputRegisters );
			stream << std::endl;
		}
	}

	void writeLoopPipelineInfoJson( std::ostream& stream,
	                                const std::vector<VuLoopPipelineOpportunity>& opportunities,
	                                const std::vector<VuSoftwarePipelineRewritePlan>& rewritePlans )
	{
		stream << "{\n  \"loop_pipeline_opportunities\": [\n";
		for( unsigned int i = 0; i < opportunities.size(); ++i )
		{
			const VuLoopPipelineOpportunity& opportunity = opportunities[i];
			const VuSoftwarePipelineRewritePlan* rewritePlan = findRewritePlanForOpportunity( rewritePlans, opportunity );
			if( i != 0 )
				stream << ",\n";
			stream << "    {\n";
			stream << "      \"label\": "; writeJsonString( stream, opportunity.label.c_str() ); stream << ",\n";
			stream << "      \"branch_token_index\": " << opportunity.branchTokenIndex << ",\n";
			stream << "      \"q_producer_token_index\": " << opportunity.qProducerTokenIndex << ",\n";
			stream << "      \"q_producer_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.qProducerTokenIndices ); stream << ",\n";
			stream << "      \"q_stages\": "; writeQStageListJson( stream, opportunity.qStages ); stream << ",\n";
			stream << "      \"first_q_consumer_token_index\": " << opportunity.firstQConsumerTokenIndex << ",\n";
			stream << "      \"last_q_consumer_token_index\": " << opportunity.lastQConsumerTokenIndex << ",\n";
			stream << "      \"q_consumer_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.qConsumerTokenIndices ); stream << ",\n";
			stream << "      \"q_producer_latency\": " << opportunity.qProducerLatency << ",\n";
			stream << "      \"q_producer_consumer_gap_cycles\": " << opportunity.qProducerConsumerGapCycles << ",\n";
			stream << "      \"q_producer_consumer_gap_deficit_cycles\": " << opportunity.qProducerConsumerGapDeficitCycles << ",\n";
			stream << "      \"loop_carried_q_gap_cycles\": " << opportunity.loopCarriedQGapCycles << ",\n";
			stream << "      \"q_producer_insertion_gap_cycles\": " << opportunity.qProducerInsertionGapCycles << ",\n";
			stream << "      \"q_producer_insertion_gap_deficit_cycles\": " << opportunity.qProducerInsertionGapDeficitCycles << ",\n";
			stream << "      \"q_scheduling_strategy\": "; writeJsonString( stream, loopQSchedulingStrategyName( opportunity.qSchedulingStrategy ) ); stream << ",\n";
			stream << "      \"source_prefix_cycles\": " << opportunity.sourcePrefixCycles << ",\n";
			stream << "      \"source_suffix_cycles\": " << opportunity.sourceSuffixCycles << ",\n";
			stream << "      \"branch_delay_slots\": " << opportunity.branchDelaySlots << ",\n";
			stream << "      \"loop_cs_clid\": " << opportunity.loopCsClid << ",\n";
			stream << "      \"loop_cs_mlid\": " << opportunity.loopCsMlid << ",\n";
			stream << "      \"simple_counted_loop\": " << (opportunity.simpleCountedLoop ? "true" : "false") << ",\n";
			stream << "      \"single_q_producer\": " << (opportunity.hasSingleQProducer ? "true" : "false") << ",\n";
			stream << "      \"requires_prolog_epilog\": " << (opportunity.requiresPrologEpilog ? "true" : "false") << ",\n";
			stream << "      \"requires_loop_carried_registers\": " << (opportunity.requiresLoopCarriedRegisters ? "true" : "false") << ",\n";
			stream << "      \"q_live_out\": " << (opportunity.qLiveOut ? "true" : "false") << ",\n";
			stream << "      \"memory_loads\": " << opportunity.memoryLoadCount << ",\n";
			stream << "      \"memory_stores\": " << opportunity.memoryStoreCount << ",\n";
			stream << "      \"memory_pre_post_increment\": " << (opportunity.hasMemoryPreOrPostIncrement ? "true" : "false") << ",\n";
			stream << "      \"xgkick\": " << (opportunity.hasXgkick ? "true" : "false") << ",\n";
			stream << "      \"eligible_single_q_pipeline\": " << (opportunity.eligibleSingleQSoftwarePipeline ? "true" : "false") << ",\n";
			stream << "      \"pipeline_plan\": {\n";
			stream << "        \"available\": " << (opportunity.hasSoftwarePipelinePlan ? "true" : "false") << ",\n";
			stream << "        \"emittable\": " << (opportunity.canEmitSoftwarePipeline ? "true" : "false") << ",\n";
			stream << "        \"blockers\": "; writeStringListJson( stream, opportunity.softwarePipelineBlockers ); stream << ",\n";
			stream << "        \"rotated_registers\": "; writeStringListJson( stream, opportunity.softwarePipelineRotatedRegisters ); stream << ",\n";
			stream << "        \"rotation_descriptors\": "; writeRotationListJson( stream, opportunity.softwarePipelineRotations ); stream << ",\n";
			stream << "        \"prefetch_descriptors\": "; writePrefetchListJson( stream, opportunity.softwarePipelinePrefetches ); stream << ",\n";
			stream << "        \"suffix_store_descriptors\": "; writeSuffixStoreListJson( stream, opportunity.softwarePipelineSuffixStores ); stream << ",\n";
			stream << "        \"prolog_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.prologTokenIndices ); stream << ",\n";
			stream << "        \"main_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.mainTokenIndices ); stream << ",\n";
			stream << "        \"drain_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.drainTokenIndices ); stream << "\n";
			stream << "      },\n";
			stream << "      \"suffix_store_drain_plan\": {\n";
			stream << "        \"available\": " << (opportunity.hasSuffixStoreDrainPlan ? "true" : "false") << ",\n";
			stream << "        \"emittable\": " << (opportunity.canEmitSuffixStoreDrain ? "true" : "false") << ",\n";
			stream << "        \"blockers\": "; writeStringListJson( stream, opportunity.suffixStoreDrainBlockers ); stream << "\n";
			stream << "      },\n";
			stream << "      \"multi_q_pipeline_plan\": {\n";
			stream << "        \"available\": " << (opportunity.hasMultiQSoftwarePipelinePlan ? "true" : "false") << ",\n";
			stream << "        \"eligible\": " << (opportunity.eligibleMultiQSoftwarePipeline ? "true" : "false") << ",\n";
			stream << "        \"emittable\": " << (opportunity.canEmitMultiQSoftwarePipeline ? "true" : "false") << ",\n";
			stream << "        \"blockers\": "; writeStringListJson( stream, opportunity.multiQSoftwarePipelineBlockers ); stream << ",\n";
			stream << "        \"prolog_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.multiQPrologTokenIndices ); stream << ",\n";
			stream << "        \"main_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.multiQMainTokenIndices ); stream << ",\n";
			stream << "        \"cyclic_prefix_token_indices\": "; writeUnsignedVectorJson( stream, opportunity.multiQCyclicPrefixTokenIndices ); stream << ",\n";
			stream << "        \"cyclic_prefix_rotations\": "; writeRotationListJson( stream, opportunity.multiQCyclicPrefixRotations ); stream << ",\n";
			stream << "        \"cyclic_prefix_insert_before_token_index\": "
			       << opportunity.multiQCyclicPrefixInsertBeforeTokenIndex << ",\n";
			stream << "        \"cyclic_prefix_guarded\": "
			       << (opportunity.multiQCyclicPrefixNeedsGuard ? "true" : "false") << ",\n";
			stream << "        \"cyclic_prefix_last_token_in_branch_delay_slot\": "
			       << (opportunity.multiQCyclicPrefixLastTokenInBranchDelaySlot ? "true" : "false") << "\n";
			stream << "      },\n";
			stream << "      \"rewrite_plan\": {\n";
			stream << "        \"available\": " << (rewritePlan ? "true" : "false") << ",\n";
			stream << "        \"prolog_label\": "; writeJsonString( stream, rewritePlan ? rewritePlan->prologLabel.c_str() : "" ); stream << ",\n";
			stream << "        \"main_label\": "; writeJsonString( stream, rewritePlan ? rewritePlan->mainLabel.c_str() : "" ); stream << ",\n";
			stream << "        \"drain_label\": "; writeJsonString( stream, rewritePlan ? rewritePlan->drainLabel.c_str() : "" ); stream << ",\n";
			stream << "        \"emits_drain\": " << (rewritePlan && rewritePlan->emitsDrain ? "true" : "false") << ",\n";
			stream << "        \"drains_suffix_stores\": " << (rewritePlan && rewritePlan->drainsSuffixStores ? "true" : "false") << ",\n";
			stream << "        \"cyclic_prefix_before_branch\": "
			       << (rewritePlan && rewritePlan->cyclicPrefixBeforeBranch ? "true" : "false") << ",\n";
			stream << "        \"cyclic_prefix_guarded\": "
			       << (rewritePlan && rewritePlan->cyclicPrefixNeedsGuard ? "true" : "false") << ",\n";
			stream << "        \"cyclic_prefix_last_token_in_branch_delay_slot\": "
			       << (rewritePlan && rewritePlan->cyclicPrefixLastTokenInBranchDelaySlot ? "true" : "false") << ",\n";
			stream << "        \"cyclic_prefix_insert_before_token_index\": "
			       << (rewritePlan ? rewritePlan->cyclicPrefixInsertBeforeTokenIndex : 0) << ",\n";
			stream << "        \"prefetch_insert_after_token_index\": "
			       << (rewritePlan ? rewritePlan->prefetchInsertAfterTokenIndex : 0) << ",\n";
			stream << "        \"q_producer_insert_after_token_index\": "
			       << (rewritePlan ? rewritePlan->qProducerInsertAfterTokenIndex : 0) << ",\n";
			stream << "        \"q_producer_in_branch_delay_slot\": "
			       << (rewritePlan && rewritePlan->qProducerInBranchDelaySlot ? "true" : "false") << ",\n";
			stream << "        \"q_producer_branch_delay_blocked_by_suffix_dependency\": "
			       << (rewritePlan && rewritePlan->qProducerBranchDelayBlockedBySuffixDependency ? "true" : "false") << ",\n";
			stream << "        \"q_producer_branch_delay_suffix_blocker_token_index\": "
			       << (rewritePlan ? rewritePlan->qProducerBranchDelaySuffixBlockerTokenIndex : 0) << ",\n";
			stream << "        \"prefetch_token_indices\": "; if( rewritePlan ) writeUnsignedVectorJson( stream, rewritePlan->prefetchTokenIndices ); else stream << "[]"; stream << ",\n";
			stream << "        \"cyclic_prefix_token_indices\": "; if( rewritePlan ) writeUnsignedVectorJson( stream, rewritePlan->cyclicPrefixTokenIndices ); else stream << "[]"; stream << ",\n";
			stream << "        \"cyclic_prefix_rotations\": "; if( rewritePlan ) writeRotationListJson( stream, rewritePlan->cyclicPrefixRotations ); else stream << "[]"; stream << ",\n";
			stream << "        \"suffix_store_descriptors\": "; if( rewritePlan ) writeSuffixStoreListJson( stream, rewritePlan->suffixStores ); else stream << "[]"; stream << ",\n";
			stream << "        \"prolog_token_indices\": "; if( rewritePlan ) writeUnsignedVectorJson( stream, rewritePlan->prologTokenIndices ); else stream << "[]"; stream << ",\n";
			stream << "        \"main_token_indices\": "; if( rewritePlan ) writeUnsignedVectorJson( stream, rewritePlan->mainTokenIndices ); else stream << "[]"; stream << ",\n";
			stream << "        \"drain_token_indices\": "; if( rewritePlan ) writeUnsignedVectorJson( stream, rewritePlan->drainTokenIndices ); else stream << "[]"; stream << "\n";
			stream << "      },\n";
			stream << "      \"induction_registers\": "; writeStringListJson( stream, opportunity.inductionRegisters ); stream << ",\n";
			stream << "      \"induction_updates\": "; writeInductionUpdateListJson( stream, opportunity.inductionUpdates ); stream << ",\n";
			stream << "      \"loop_read_write_registers\": "; writeStringListJson( stream, opportunity.loopReadWriteRegisters ); stream << ",\n";
			stream << "      \"carried_q_input_registers\": "; writeStringListJson( stream, opportunity.carriedQInputRegisters ); stream << ",\n";
			stream << "      \"carried_q_output_registers\": "; writeStringListJson( stream, opportunity.carriedQOutputRegisters ); stream << "\n";
			stream << "    }";
		}
		stream << "\n  ]\n}" << std::endl;
	}

	std::string scheduleTokenName( const Token* token )
	{
		if( !token )
			return "";
		std::string name;
		if( token->name().length() != 0 )
			name = normalizeVuMnemonic( token->name() );
		else if( token->label().length() != 0 )
			name = token->label() + ":";
		if( (token->flags() & Token::BRANCH_DELAY_FILLER) && name.length() != 0 )
			name += "[branch_delay]";
		return name;
	}

	void writeScheduleTokenText( std::ostream& stream,
	                             unsigned int tokenIndex,
	                             const Token* token )
	{
		if( tokenIndex == VU_SCHEDULED_TOKEN_INDEX_NONE || !token )
		{
			stream << "-";
			return;
		}

		stream << tokenIndex << ":" << scheduleTokenName( token );
	}

	void writeNullableTokenIndexJson( std::ostream& stream, unsigned int tokenIndex )
	{
		if( tokenIndex == VU_SCHEDULED_TOKEN_INDEX_NONE )
			stream << "null";
		else
			stream << tokenIndex;
	}

	void writeNullableTokenNameJson( std::ostream& stream, const Token* token )
	{
		if( token )
			writeJsonString( stream, scheduleTokenName( token ).c_str() );
		else
			stream << "null";
	}

	const char* scheduledPaddingKindName( VuScheduledPaddingKind kind )
	{
		switch( kind )
		{
			case VU_SCHEDULED_PADDING_NOP: return "nop";
			case VU_SCHEDULED_PADDING_WAITQ: return "waitq";
			case VU_SCHEDULED_PADDING_WAITP: return "waitp";
			case VU_SCHEDULED_PADDING_NONE:
			default: return "none";
		}
	}

	void writeScheduleInfoText( std::ostream& stream, const std::list<Token>& tokens )
	{
		const VuScheduledProgram program =
			scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );

		stream << "OpenVCL VU ready scheduler issue slots" << std::endl;
		stream << "program_cycle_count=" << program.cycleCount << std::endl;
		for( unsigned int blockIndex = 0; blockIndex < program.blocks.size(); ++blockIndex )
		{
			const VuScheduledBasicBlock& scheduledBlock = program.blocks[blockIndex];
			const VuBasicBlock& block = scheduledBlock.block;
			const std::vector<VuScheduledIssueSlot>& slots = scheduledBlock.issueSlots;
			stream << "block " << blockIndex
			       << " first_token=" << block.firstTokenIndex
			       << " terminator=" << basicBlockTerminatorKindName( block.terminatorKind )
			       << " first_issue_cycle=" << scheduledBlock.firstIssueCycle
			       << " cycle_count=" << scheduledBlock.cycleCount
			       << " slots=" << slots.size()
			       << std::endl;
			for( unsigned int slotIndex = 0; slotIndex < slots.size(); ++slotIndex )
			{
				const VuScheduledIssueSlot& slot = slots[slotIndex];
				stream << "  slot " << slotIndex
				       << " issue_cycle=" << slot.issueCycle
				       << " program_issue_cycle=" << (scheduledBlock.firstIssueCycle + slot.issueCycle)
				       << " cycle_count=" << slot.cycleCount
				       << " first=";
				writeScheduleTokenText( stream, slot.firstTokenIndex, slot.firstToken );
				stream << " second=";
				writeScheduleTokenText( stream, slot.secondTokenIndex, slot.secondToken );
				stream << " upper=";
				writeScheduleTokenText( stream, slot.upperTokenIndex, slot.upperToken );
				stream << " lower=";
				writeScheduleTokenText( stream, slot.lowerTokenIndex, slot.lowerToken );
				stream << " padding=" << (slot.padding ? "yes" : "no");
				stream << " padding_kind=" << scheduledPaddingKindName( slot.paddingKind );
				stream << " ignored_waw_resources=";
				writeFlagListText( stream, slot.ignoredImplicitWawResources, kResourceFlags );
				stream << std::endl;
			}
		}
	}

	void writeScheduleInfoJson( std::ostream& stream, const std::list<Token>& tokens )
	{
		const VuScheduledProgram program =
			scheduleVuProgramReadyIssueSlotsWithFlagLiveness( tokens );

		stream << "{\n  \"program_cycle_count\": " << program.cycleCount << ",\n";
		stream << "  \"scheduled_blocks\": [\n";
		for( unsigned int blockIndex = 0; blockIndex < program.blocks.size(); ++blockIndex )
		{
			const VuScheduledBasicBlock& scheduledBlock = program.blocks[blockIndex];
			const VuBasicBlock& block = scheduledBlock.block;
			const std::vector<VuScheduledIssueSlot>& slots = scheduledBlock.issueSlots;
			if( blockIndex != 0 )
				stream << ",\n";
			stream << "    {\n";
			stream << "      \"block_index\": " << blockIndex << ",\n";
			stream << "      \"first_token_index\": " << block.firstTokenIndex << ",\n";
			stream << "      \"terminator\": "; writeJsonString( stream, basicBlockTerminatorKindName( block.terminatorKind ) ); stream << ",\n";
			stream << "      \"first_issue_cycle\": " << scheduledBlock.firstIssueCycle << ",\n";
			stream << "      \"cycle_count\": " << scheduledBlock.cycleCount << ",\n";
			stream << "      \"issue_slots\": [\n";
			for( unsigned int slotIndex = 0; slotIndex < slots.size(); ++slotIndex )
			{
				const VuScheduledIssueSlot& slot = slots[slotIndex];
				if( slotIndex != 0 )
					stream << ",\n";
				stream << "        {\n";
				stream << "          \"slot_index\": " << slotIndex << ",\n";
				stream << "          \"issue_cycle\": " << slot.issueCycle << ",\n";
				stream << "          \"program_issue_cycle\": " << (scheduledBlock.firstIssueCycle + slot.issueCycle) << ",\n";
				stream << "          \"cycle_count\": " << slot.cycleCount << ",\n";
				stream << "          \"first_token_index\": "; writeNullableTokenIndexJson( stream, slot.firstTokenIndex ); stream << ",\n";
				stream << "          \"second_token_index\": "; writeNullableTokenIndexJson( stream, slot.secondTokenIndex ); stream << ",\n";
				stream << "          \"upper_token_index\": "; writeNullableTokenIndexJson( stream, slot.upperTokenIndex ); stream << ",\n";
				stream << "          \"lower_token_index\": "; writeNullableTokenIndexJson( stream, slot.lowerTokenIndex ); stream << ",\n";
				stream << "          \"first\": "; writeNullableTokenNameJson( stream, slot.firstToken ); stream << ",\n";
				stream << "          \"second\": "; writeNullableTokenNameJson( stream, slot.secondToken ); stream << ",\n";
				stream << "          \"upper\": "; writeNullableTokenNameJson( stream, slot.upperToken ); stream << ",\n";
				stream << "          \"lower\": "; writeNullableTokenNameJson( stream, slot.lowerToken ); stream << ",\n";
				stream << "          \"padding\": " << (slot.padding ? "true" : "false") << ",\n";
				stream << "          \"padding_kind\": "; writeJsonString( stream, scheduledPaddingKindName( slot.paddingKind ) ); stream << ",\n";
				stream << "          \"ignored_waw_resources\": "; writeFlagArrayJson( stream, slot.ignoredImplicitWawResources, kResourceFlags ); stream << "\n";
				stream << "        }";
			}
			stream << "\n      ]\n";
			stream << "    }";
		}
		stream << "\n  ]\n}" << std::endl;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Parser::Parser()
{
	m_state = INVALID_STATE;
	m_tempCounter = 0;

	m_preParser = DISABLED;

	m_haveUnsunkOutput = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Parser::~Parser()
{
	if( m_cmdLine.deleteTemp() )
	{
		for( std::list< std::string >::const_iterator i = m_tempFiles.begin(); i != m_tempFiles.end(); i++ )
			remove( (*i).c_str() );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::create( int argc, char* argv[] )
{
	if( !m_cmdLine.parse( argc, argv ) )
	{
		Error::Display( Error( "Invalid Arguments" ) );
		return false;
	}

	if( m_cmdLine.showVersion() )
		setState( SHOW_VERSION );
	else if( m_cmdLine.showUsage() )
		setState( SHOW_USAGE );
	else if( m_cmdLine.dumpInstructionInfo() )
		setState( DUMP_INSTRUCTION_INFO );
	else if( m_cmdLine.dumpLoopPipelineInfo() || m_cmdLine.dumpScheduleInfo() )
		setState( READ_INPUT );
	else if( m_cmdLine.compareVsmCostListMarkdown() || m_cmdLine.compareVsmCostListCheck() )
		setState( ANALYZE_VSM_COST_COMPARE_LIST );
	else if( m_cmdLine.compareVsmCost() )
		setState( ANALYZE_VSM_COST_COMPARE );
	else if( m_cmdLine.analyzeVsmCost() )
		setState( ANALYZE_VSM_COST );
	else
		setState( READ_INPUT );

	setupOperands();

	// set original input-file
	m_inputFile = m_cmdLine.input();
	m_sourceFile = m_cmdLine.input();

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::begin()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::run()
{
	switch( m_state )
	{
		case SHOW_VERSION: return showVersion();
		case SHOW_USAGE: return showUsage();
		case DUMP_INSTRUCTION_INFO: return dumpInstructionInfo();
		case DUMP_LOOP_PIPELINE_INFO: return dumpLoopPipelineInfo();
		case DUMP_SCHEDULE_INFO: return dumpScheduleInfo();
		case ANALYZE_VSM_COST: return analyzeVsmCost();
		case ANALYZE_VSM_COST_COMPARE: return analyzeVsmCostCompare();
		case ANALYZE_VSM_COST_COMPARE_LIST: return analyzeVsmCostCompareList();
		case READ_INPUT: return readInput();
		case PREPROCESS: return preProcess();
		case TOKENIZE: return tokenize();
		case ALLOCATE_REGISTERS: return allocateRegisters();
		case GENERATE_CODE : return generateCode();
		case WRITE_OUTPUT: return writeOutput();
		default: return false;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::end()
{
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Parser::setupOperands()
{
	// setup command templates

	// tokenizer operands

	m_operands.push_back( Operand( ".syntax", 			1, Operand::PREPROCESSOR, "'old'|'new'" ) );

	m_operands.push_back( Operand( ".vu",						0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".vsm",					0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".raw",					0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endvsm",				0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endraw",				0, Operand::PREPROCESSOR, "" ) );

	m_operands.push_back( Operand( ".rawloop",			0, Operand::PREPROCESSOR, "" ) );
	m_operands.push_back( Operand( ".endrawloop",		0, Operand::PREPROCESSOR, "" ) );

	// VCL operands

	m_operands.push_back( Operand( ".init_vf",			1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vf:noalias:range,..." ) );
	m_operands.push_back( Operand( ".init_vi",			1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vi:noalias:range,..." ) );
	m_operands.push_back( Operand( ".rem_vf",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vf:noalias:range,..." ) );
	m_operands.push_back( Operand( ".rem_vi",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED,	"vi:noalias:range,..." ) );
	m_operands.push_back( Operand( ".init_vf_all",	0, Operand::PREPROCESSOR|Operand::FILTERED,									"" ) );
	m_operands.push_back( Operand( ".init_vi_all",	0, Operand::PREPROCESSOR|Operand::FILTERED,									"" ) );

	m_operands.push_back( Operand( "--LoopCS",			2, Operand::PREPROCESSOR|Operand::FILTERED,	"imm:integer,imm:integer" ) );
	m_operands.push_back( Operand( "--LoopExtra",		1, Operand::PREPROCESSOR|Operand::FILTERED, "imm:integer" ) );
	m_operands.push_back( Operand( "--LoopAbs",			1, Operand::PREPROCESSOR|Operand::FILTERED, "imm:integer" ) );
	m_operands.push_back( Operand( "--barrier",			0, Operand::PREPROCESSOR|Operand::FILTERED, "" ) );
	m_operands.push_back( Operand( "--cont",				0, Operand::PREPROCESSOR|Operand::FILTERED, "" ) );
	m_operands.push_back( Operand( "--enter",				0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::ENTER ) );
	m_operands.push_back( Operand( "--endenter",		0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::ENTER ) );
	m_operands.push_back( Operand( "--exit",				0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::EXIT ) );
	m_operands.push_back( Operand( "--exitm",				1, Operand::PREPROCESSOR|Operand::MULTI|Operand::FILTERED, "imm",	Operand::EXIT ) );
	m_operands.push_back( Operand( "--endexit",			0, Operand::PREPROCESSOR|Operand::FILTERED, "",										Operand::EXIT ) );

	m_operands.push_back( Operand( "in_vi",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vi):noalias",		Operand::ENTER ) );
	m_operands.push_back( Operand( "in_vf",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vf):noalias",		Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_acc",			1, Operand::PREPROCESSOR|Operand::FILTERED, "acc",								Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_clip",		1, Operand::PREPROCESSOR|Operand::FILTERED, "'clip'",							Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_i",				1, Operand::PREPROCESSOR|Operand::FILTERED, "i",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_p",				1, Operand::PREPROCESSOR|Operand::FILTERED, "p",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_q",				1, Operand::PREPROCESSOR|Operand::FILTERED, "q",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_r",				1, Operand::PREPROCESSOR|Operand::FILTERED, "r",									Operand::ENTER ) );
	m_operands.push_back( Operand( "in_hw_status",	1, Operand::PREPROCESSOR|Operand::FILTERED, "'status'",						Operand::ENTER ) );

	m_operands.push_back( Operand( "out_vi",				1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vi):noalias",		Operand::EXIT ) );
	m_operands.push_back( Operand( "out_vf",				1, Operand::PREPROCESSOR|Operand::FILTERED, "imm(vf):noalias",		Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_acc",		1, Operand::PREPROCESSOR|Operand::FILTERED, "acc",								Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_clip",		1, Operand::PREPROCESSOR|Operand::FILTERED, "'clip'",							Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_i",			1, Operand::PREPROCESSOR|Operand::FILTERED, "i",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_p",			1, Operand::PREPROCESSOR|Operand::FILTERED, "p",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_q",			1, Operand::PREPROCESSOR|Operand::FILTERED, "q",									Operand::EXIT ) );
	m_operands.push_back( Operand( "out_hw_r",			1, Operand::PREPROCESSOR|Operand::FILTERED, "r",									Operand::EXIT ) );

	m_operands.push_back( Operand( ".mpg",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm" ) );
	m_operands.push_back( Operand( ".name",					1, Operand::PREPROCESSOR|Operand::FILTERED, "imm" ) );

	m_operands.push_back( Operand( ".global",				1, Operand::PREPROCESSOR, "imm" ) );

	// VU hardware instructions

	for( const VuInstructionInfo* info = allVuInstructionInfos(); info->mnemonic; ++info )
	{
		m_operands.push_back( Operand( info->operandName, info->arguments, info->operandFlags, info->operandPattern, info->operandUnit, info->throughput, info->latency ) );
	}

	// operand simplifications

	Operand* add = getOperand( "ADD", Operand::UPPER|Operand::DEST );
	add->addAlternative( add );
	add->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::BROADCAST ) );
	add->addAlternative( getOperand( "ADD",		Operand::UPPER|Operand::BROADCAST ) );
	add->addAlternative( getOperand( "ADDi",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDq",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDAi",	Operand::UPPER|Operand::DEST ) );
	add->addAlternative( getOperand( "ADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* adda = getOperand( "ADDA", Operand::UPPER|Operand::DEST );
	adda->addAlternative( adda );
	adda->addAlternative( getOperand( "ADDA",	Operand::UPPER|Operand::BROADCAST ) );
	adda->addAlternative( getOperand( "ADDAi",	Operand::UPPER|Operand::DEST ) );
	adda->addAlternative( getOperand( "ADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* sub = getOperand( "SUB", Operand::UPPER|Operand::DEST );
	sub->addAlternative( sub );
	sub->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::BROADCAST ) );
	sub->addAlternative( getOperand( "SUB",		Operand::UPPER|Operand::BROADCAST ) );
	sub->addAlternative( getOperand( "SUBi",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBq",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBAi",	Operand::UPPER|Operand::DEST ) );
	sub->addAlternative( getOperand( "SUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* suba = getOperand( "SUBA", Operand::UPPER|Operand::DEST );
	suba->addAlternative( suba );
	suba->addAlternative( getOperand( "SUBA",	Operand::UPPER|Operand::BROADCAST ) );
	suba->addAlternative( getOperand( "SUBAi",	Operand::UPPER|Operand::DEST ) );
	suba->addAlternative( getOperand( "SUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* mul = getOperand( "MUL", Operand::UPPER|Operand::DEST );
	mul->addAlternative( mul );
	mul->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::BROADCAST ) );
	mul->addAlternative( getOperand( "MUL",		Operand::UPPER|Operand::BROADCAST ) );
	mul->addAlternative( getOperand( "MULi",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULq",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULAi",	Operand::UPPER|Operand::DEST ) );
	mul->addAlternative( getOperand( "MULAq",	Operand::UPPER|Operand::DEST ) );

	Operand* mula = getOperand( "MULA", Operand::UPPER|Operand::DEST );
	mula->addAlternative( mula );
	mula->addAlternative( getOperand( "MULA",	Operand::UPPER|Operand::BROADCAST ) );
	mula->addAlternative( getOperand( "MULAi",	Operand::UPPER|Operand::DEST ) );
	mula->addAlternative( getOperand( "MULAq",	Operand::UPPER|Operand::DEST ) );

	Operand* madd = getOperand( "MADD", Operand::UPPER|Operand::DEST );
	madd->addAlternative( madd );
	madd->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::BROADCAST ) );
	madd->addAlternative( getOperand( "MADD",		Operand::UPPER|Operand::BROADCAST ) );
	madd->addAlternative( getOperand( "MADDi",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDq",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDAi",	Operand::UPPER|Operand::DEST ) );
	madd->addAlternative( getOperand( "MADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* madda = getOperand( "MADDA", Operand::UPPER|Operand::DEST );
	madda->addAlternative( madda );
	madda->addAlternative( getOperand( "MADDA",	Operand::UPPER|Operand::BROADCAST ) );
	madda->addAlternative( getOperand( "MADDAi",	Operand::UPPER|Operand::DEST ) );
	madda->addAlternative( getOperand( "MADDAq",	Operand::UPPER|Operand::DEST ) );

	Operand* msub = getOperand( "MSUB", Operand::UPPER|Operand::DEST );
	msub->addAlternative( msub );
	msub->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::BROADCAST ) );
	msub->addAlternative( getOperand( "MSUB",		Operand::UPPER|Operand::BROADCAST ) );
	msub->addAlternative( getOperand( "MSUBi",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBq",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBAi",	Operand::UPPER|Operand::DEST ) );
	msub->addAlternative( getOperand( "MSUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* msuba = getOperand( "MSUBA", Operand::UPPER|Operand::DEST );
	msuba->addAlternative( msuba );
	msuba->addAlternative( getOperand( "MSUBA",	Operand::UPPER|Operand::BROADCAST ) );
	msuba->addAlternative( getOperand( "MSUBAi",	Operand::UPPER|Operand::DEST ) );
	msuba->addAlternative( getOperand( "MSUBAq",	Operand::UPPER|Operand::DEST ) );

	Operand* max = getOperand( "MAX", Operand::UPPER|Operand::DEST );
	max->addAlternative( max );
	max->addAlternative( getOperand( "MAX", 		Operand::UPPER|Operand::BROADCAST ) );
	max->addAlternative( getOperand( "MAXi", 		Operand::UPPER|Operand::DEST ) );

	Operand* mini = getOperand( "MINI", Operand::UPPER|Operand::DEST );
	mini->addAlternative( mini );
	mini->addAlternative( getOperand( "MINI", 	Operand::UPPER|Operand::BROADCAST ) );
	mini->addAlternative( getOperand( "MINIi",	Operand::UPPER|Operand::DEST ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Parser::setState( State state )
{
	m_state = state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::readInput()
{
	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream stream( m_inputFile.c_str() );

		if( !stream.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		m_files[ m_sourceFile.c_str() ] = ( File( m_cmdLine.input().c_str() ) );

		return readInputStream( stream );
	}
	else
	{
		m_files[ "" ] = File("stdin");
		return readInputStream( std::cin );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCost()
{
	VsmCostAnalyzer analyzer;

	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream input( m_cmdLine.input().c_str() );
		if( !input.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		if( !analyzer.analyze( input, m_cmdLine.input() ) )
			return false;
	}
	else
	{
		if( !analyzer.analyze( std::cin, "stdin" ) )
			return false;
	}

	const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
	for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
		analyzer.setBlockRepeat( i->first, i->second );

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.analyzeVsmCostJson() )
			analyzer.writeJson( output );
		else
			analyzer.writeText( output );
	}
	else
	{
		if( m_cmdLine.analyzeVsmCostJson() )
			analyzer.writeJson( std::cout );
		else
			analyzer.writeText( std::cout );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCostCompare()
{
	VsmCostAnalyzer baselineAnalyzer;
	VsmCostAnalyzer candidateAnalyzer;

	std::ifstream baselineInput( m_cmdLine.costCompareBaseline().c_str() );
	if( !baselineInput.good() )
	{
		Error::Display( Error( "Could not open cost comparison baseline" ) );
		return false;
	}

	if( !baselineAnalyzer.analyze( baselineInput, m_cmdLine.costCompareBaseline() ) )
		return false;

	if( m_cmdLine.input().length() > 0 )
	{
		std::ifstream candidateInput( m_cmdLine.input().c_str() );
		if( !candidateInput.good() )
		{
			Error::Display( Error( "Could not open input" ) );
			return false;
		}

		if( !candidateAnalyzer.analyze( candidateInput, m_cmdLine.input() ) )
			return false;
	}
	else
	{
		if( !candidateAnalyzer.analyze( std::cin, "stdin" ) )
			return false;
	}

	const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
	for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
	{
		baselineAnalyzer.setBlockRepeat( i->first, i->second );
		candidateAnalyzer.setBlockRepeat( i->first, i->second );
	}

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.compareVsmCostJson() )
			VsmCostAnalyzer::writeComparisonJson( output, baselineAnalyzer, candidateAnalyzer );
		else if( m_cmdLine.compareVsmCostMarkdown() )
			VsmCostAnalyzer::writeComparisonMarkdown( output, baselineAnalyzer, candidateAnalyzer );
		else
			VsmCostAnalyzer::writeComparisonText( output, baselineAnalyzer, candidateAnalyzer );
	}
	else
	{
		if( m_cmdLine.compareVsmCostJson() )
			VsmCostAnalyzer::writeComparisonJson( std::cout, baselineAnalyzer, candidateAnalyzer );
		else if( m_cmdLine.compareVsmCostMarkdown() )
			VsmCostAnalyzer::writeComparisonMarkdown( std::cout, baselineAnalyzer, candidateAnalyzer );
		else
			VsmCostAnalyzer::writeComparisonText( std::cout, baselineAnalyzer, candidateAnalyzer );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::analyzeVsmCostCompareList()
{
	const bool writeMarkdown = m_cmdLine.compareVsmCostListMarkdown();
	const bool checkMetric = m_cmdLine.compareVsmCostListCheck();
	unsigned int ignoredMetricValue = 0;
	if( checkMetric
	    && !costCompareMetricValue( VsmCostAnalyzer::Summary(),
	                                m_cmdLine.costCompareListCheckMetric(),
	                                ignoredMetricValue ) )
	{
		Error::Display( Error( "Unknown cost comparison check metric" ) );
		return false;
	}

	std::ifstream manifestFile;
	std::istream* manifest = &std::cin;
	if( m_cmdLine.input().length() > 0 )
	{
		manifestFile.open( m_cmdLine.input().c_str() );
		if( !manifestFile.good() )
		{
			Error::Display( Error( "Could not open cost comparison list" ) );
			return false;
		}
		manifest = &manifestFile;
	}

	std::ofstream outputFile;
	std::ostream* output = &std::cout;
	if( m_cmdLine.output().length() > 0 )
	{
		outputFile.open( m_cmdLine.output().c_str() );
		if( !outputFile.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}
		output = &outputFile;
	}

	if( writeMarkdown )
		VsmCostAnalyzer::writeComparisonMarkdownHeader( *output );

	std::string line;
	unsigned int lineNumber = 0;
	unsigned int failedPairs = 0;
	while( std::getline( *manifest, line ) )
	{
		++lineNumber;
		const std::string stripped = trimManifestText( stripManifestComment( line ) );
		if( stripped.empty() )
			continue;

		std::stringstream fields( stripped );
		std::string baselinePath;
		std::string candidatePath;
		std::string extra;
		if( !(fields >> baselinePath >> candidatePath) || (fields >> extra) )
		{
			std::stringstream message;
			message << "Invalid cost comparison list entry at line " << lineNumber;
			Error::Display( Error( message.str() ) );
			return false;
		}

		VsmCostAnalyzer baselineAnalyzer;
		VsmCostAnalyzer candidateAnalyzer;

		std::ifstream baselineInput( baselinePath.c_str() );
		if( !baselineInput.good() )
		{
			Error::Display( Error( "Could not open cost comparison baseline" ) );
			return false;
		}
		if( !baselineAnalyzer.analyze( baselineInput, baselinePath ) )
			return false;

		std::ifstream candidateInput( candidatePath.c_str() );
		if( !candidateInput.good() )
		{
			Error::Display( Error( "Could not open cost comparison candidate" ) );
			return false;
		}
		if( !candidateAnalyzer.analyze( candidateInput, candidatePath ) )
			return false;

		const std::vector< std::pair<std::string, unsigned int> >& costLoops = m_cmdLine.costLoops();
		for( std::vector< std::pair<std::string, unsigned int> >::const_iterator i = costLoops.begin(); i != costLoops.end(); ++i )
		{
			baselineAnalyzer.setBlockRepeat( i->first, i->second );
			candidateAnalyzer.setBlockRepeat( i->first, i->second );
		}

		if( writeMarkdown )
			VsmCostAnalyzer::writeComparisonMarkdownRow( *output, baselineAnalyzer, candidateAnalyzer );

		if( checkMetric )
		{
			unsigned int baselineValue = 0;
			unsigned int candidateValue = 0;
			costCompareMetricValue( baselineAnalyzer.summary(), m_cmdLine.costCompareListCheckMetric(), baselineValue );
			costCompareMetricValue( candidateAnalyzer.summary(), m_cmdLine.costCompareListCheckMetric(), candidateValue );
			if( candidateValue > baselineValue )
			{
				++failedPairs;
				std::cerr << "Cost check failed for " << candidatePath
				          << " against " << baselinePath
				          << " metric=" << m_cmdLine.costCompareListCheckMetric()
				          << " baseline=" << baselineValue
				          << " candidate=" << candidateValue
				          << std::endl;
			}
		}
	}

	if( failedPairs > 0 )
	{
		std::stringstream message;
		message << "Cost comparison list check failed for " << failedPairs << " shader pair(s)";
		Error::Display( Error( message.str() ) );
		return false;
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::dumpInstructionInfo()
{
	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.dumpInstructionInfoJson() )
			writeInstructionInfoJson( output );
		else
			writeInstructionInfoText( output );
	}
	else
	{
		if( m_cmdLine.dumpInstructionInfoJson() )
			writeInstructionInfoJson( std::cout );
		else
			writeInstructionInfoText( std::cout );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::dumpLoopPipelineInfo()
{
	const std::vector<VuLoopPipelineOpportunity> opportunities = findVuLoopPipelineOpportunities( m_tokenizer.tokens() );
	const std::vector<VuSoftwarePipelineRewritePlan> rewritePlans = buildVuSoftwarePipelineRewritePlans( m_tokenizer.tokens() );

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.dumpLoopPipelineInfoJson() )
			writeLoopPipelineInfoJson( output, opportunities, rewritePlans );
		else
			writeLoopPipelineInfoText( output, opportunities, rewritePlans );
	}
	else
	{
		if( m_cmdLine.dumpLoopPipelineInfoJson() )
			writeLoopPipelineInfoJson( std::cout, opportunities, rewritePlans );
		else
			writeLoopPipelineInfoText( std::cout, opportunities, rewritePlans );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::dumpScheduleInfo()
{
	std::list<Token> scheduleTokens = m_tokenizer.tokens();
	if( !m_cmdLine.knownLoopOptimizations() && m_cmdLine.genericSoftwarePipelining() )
	{
		std::list<Token> transformedTokens =
			applyVuSoftwarePipelinePlansWithSafeStoreBaseAdvance( scheduleTokens );
		scheduleTokens.swap( transformedTokens );
	}

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream output( m_cmdLine.output().c_str() );
		if( !output.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		if( m_cmdLine.dumpScheduleInfoJson() )
			writeScheduleInfoJson( output, scheduleTokens );
		else
			writeScheduleInfoText( output, scheduleTokens );
	}
	else
	{
		if( m_cmdLine.dumpScheduleInfoJson() )
			writeScheduleInfoJson( std::cout, scheduleTokens );
		else
			writeScheduleInfoText( std::cout, scheduleTokens );
	}

	setState( EXIT );
	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::readInputStream( std::istream& stream )
{
	std::string buffer;
	std::string temp;
	unsigned int curr = 1;
	unsigned int original = 1;

	std::map<std::string,File>::iterator currFile = m_files.find( m_sourceFile );
	if( currFile == m_files.end() )
	{
		Error::Display( Error( "INTERNAL ERROR: Could not locate filename in map" ) );
		return false;
	}

	while( stream.good() )
	{
		std::getline( stream, buffer, '\n' );
		bool store = true;

		switch( m_preParser )
		{
			case DISABLED: original = curr; break;

			case GASP:
			{
				if( buffer.length() > 0 )
				{
					if( ';' == buffer[0] )
					{
						bool numbers = false;
						bool valid = true;
						std::string::size_type offset = 0, i;
						for( i = 1; valid && (i < buffer.length()); ++i )
						{
							if( numbers )
							{
								if( (' ' == buffer[i]) )
									break;

								if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
									continue;
							}
							else
							{
								if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
								{
									offset = i;
									numbers = true;
									continue;
								}
							}

							if( ' ' != buffer[i] )
								valid = false;
						}

						if( valid && numbers )
							original = strtoul( buffer.substr( offset, i-offset ).c_str(), NULL, 10 );

						store = false;
					}
				}
			}
			break;

			case CPP:
			{
				if( buffer.length() > 0 )
				{
					if( '#' == buffer[0] )
					{
						int number = 0;
						std::string name;
						bool valid = true;
						bool finished = false;
						std::string::size_type offset = 0, i;
						enum { START, LINE, STRING } mode = START;

						for( i = 1; !finished && valid && (i < buffer.length()); ++i )
						{
							switch( mode )
							{
								case START:
								{
									if( ' ' == buffer[i] )
										break;

									if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
									{
										mode = LINE;
										offset = i;
										break;
									}

									valid = false;
								}
								break;

								case LINE:
								{
									if( ('0' <= buffer[i]) && ('9' >= buffer[i]) )
										break;

									if( ' ' == buffer[i] )
									{
										mode = STRING;
										number = strtoul( buffer.substr( offset, i-offset ).c_str(), NULL, 10 );
										break;
									}

									valid = false;
								}
								break;

								case STRING:
								{
									if( ' ' == buffer[i] )
										break;

									if( '"' == buffer[i] )
									{
										offset = buffer.substr(i+1).find_last_of('"');

										if( offset != std::string::npos )
										{
											name = buffer.substr(i+1,offset);
											finished = true;
											break;
										}
									}

									valid = false;
								}
								break;
							}
						}

						if( valid && finished )
						{
							std::map<std::string,File>::iterator newFile = m_files.find( name );
							if( m_files.end() != newFile )
							{
								currFile = newFile;
								original = number-1;
							}
							else
							{
								m_files[ name ] = File(name);
								original = number-1;

								currFile = m_files.find( name );
								if( currFile == m_files.end() )
								{
									Error::Display( Error( "INTERNAL ERROR: Could not locate filename in map" ) );
									return false;
								}
							}
						}

						store = false;
						break;
					}
				}
				original++;
			}
			break;
		}

		if( store )
		{
			m_lines.push_back( Line( currFile->second, curr, original, buffer.substr( 0, buffer.find('\r') ) ) );
			curr++;
		}
	}

	if( m_cmdLine.runGasp() || m_cmdLine.runCpp() )
		setState( PREPROCESS );
	else
		setState( TOKENIZE );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::preProcess()
{
	// create temporary files

	std::string src = tempFilename();
	std::string dest = tempFilename();

	if( !src.length() || !dest.length() )
	{
		Error::Display( Error( "Failed creating unique filename" ) );
		return false;
	}

	m_tempFiles.push_back(src);
	m_tempFiles.push_back(dest);

	// write contents to file

	std::ofstream sf(src.c_str());
	if( !sf.good() )
	{
		Error::Display( Error( "Failed creating temporary file" ) );
		return false;
	}

	for( std::list<Line>::const_iterator line = m_lines.begin(); line != m_lines.end(); line++ )
		sf << (*line).content() << std::endl;

	sf.close();

	// run preprocessing tool

	std::string preprocessor;

	if( m_cmdLine.runCpp() )
	{
		preprocessor = m_cmdLine.cpp() + " -I. -nostdinc -x assembler-with-cpp";

		for( std::list<std::string>::const_iterator i = m_cmdLine.includes().begin(); i != m_cmdLine.includes().end(); i++ )
			preprocessor += " -I\"" + *i + "\"";

		preprocessor += " -o \"" + dest + "\" \"" + src + "\"";

		m_preParser = CPP;
		m_cmdLine.setRunCpp( false );
	}
	else
	{
		preprocessor = m_cmdLine.gasp() + " -p -s -c ';'";

		for( std::list<std::string>::const_iterator i = m_cmdLine.includes().begin(); i != m_cmdLine.includes().end(); i++ )
			preprocessor += " -I\"" + *i + "\"";

		preprocessor += " -o \"" + dest + "\" \"" + src + "\"";

		m_preParser = GASP;
		m_cmdLine.setRunGasp( false );
	}

	if( system( preprocessor.c_str() ) )
	{
		Error::Display( Error( "Preprocessor failed" ) );
		return false;
	}

	// restart input reader

	m_sourceFile = src;
	m_inputFile = dest;

	m_lines.clear();
	m_files.clear();
	setState( READ_INPUT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::tokenize()
{
	m_tokenizer.setNewSyntax( m_cmdLine.newSyntax() );
	m_tokenizer.setOperands( m_operands );

	// Scheduling policy, consulted by the free functions in VuSchedulingRules that
	// every scheduler entry point calls. Set here rather than next to the register
	// allocator because --dump-schedule-info leaves the state machine before
	// allocation runs, and a diagnostic that does not describe the code we would
	// emit is worse than no diagnostic.
	setVuScheduleFlagReadersEnabled( m_cmdLine.scheduleFlagReaders() );
	setVuFmacInterlockEnabled( m_cmdLine.fmacInterlock() );
	setVuPairBestOfCyclesEnabled( m_cmdLine.pairBestOfCycles() );
	setVuFlagVisibilityLatency( m_cmdLine.sceLatencies() ? 1u : 4u );
	setVuClipFlagVisibilityLatency( 4u );
	setVuClipFlagSchedulingLatency( 4u );
	setVuIntegerLoadReadyCycles( m_cmdLine.sceLatencies() ? 3u : 0u );
	setVuEmitDelayFillersEnabled( m_cmdLine.emitDelayFillers() );
	setVuBranchInterlockEnabled( m_cmdLine.branchInterlock() );
	setVuLoopLivenessAlwaysEnabled( m_cmdLine.loopLivenessAlways() );
	setVuUpperMoveWithWEnabled( m_cmdLine.upperMoveWithW() );
	setVuCoalesceFloatWritesEnabled( m_cmdLine.coalesceFloatWrites() );
	setVuSplitDeadFloatRangesEnabled( m_cmdLine.splitDeadFloatRanges() );
	setVuSpreadFloatRegistersEnabled( m_cmdLine.splitDeadFloatRanges() );
	setVuSpreadFloatRegistersWebsOnly( m_cmdLine.spreadWebsOnly() );
	setVuTrimUncarriedRangesEnabled( m_cmdLine.trimUncarriedRanges() );
	setVuSinkLoadsEnabled( m_cmdLine.sinkLoads() );
	setVuSinkLoadsAcrossStoresEnabled( m_cmdLine.sinkLoadsAcrossStores() );
	setVuSinkLoadsIntoLoopsEnabled( m_cmdLine.sinkLoadsIntoLoops() );
	setVuSinkLoadsPastBranchesEnabled( m_cmdLine.sinkLoadsPastBranches() );
	setVuDropDeadWritesEnabled( m_cmdLine.dropDeadWrites() );
	setVuExemptFullClipMasksEnabled( m_cmdLine.exemptFullClipMasks() );
	setVuClipExemptionBestOfEnabled( m_cmdLine.clipExemptionBestOf() );
	setVuShowPairMissesEnabled( m_cmdLine.showPairMisses() );
	setVuPairBestOfTwoEnabled( m_cmdLine.pairBestOfTwo() );
	setVuPairBestOfManyEnabled( m_cmdLine.pairBestOfMany() );
	setVuBranchBubbleOnDependencyEnabled( m_cmdLine.branchBubbleOnDependency() );

	for( std::list<Line>::const_iterator i = m_lines.begin(); i != m_lines.end(); i++ )
	{
		if( !m_tokenizer.parse( *i ) )
			return false;
	}

	if( m_cmdLine.dumpScheduleInfo() )
		setState( DUMP_SCHEDULE_INFO );
	else if( m_cmdLine.dumpLoopPipelineInfo() )
		setState( DUMP_LOOP_PIPELINE_INFO );
	else
		setState( ALLOCATE_REGISTERS );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// --split-dead-float-ranges costs registers, and three of the seventy programs
// run out.  Allocate with it, and if that runs out put the tokenizer's own output
// back and allocate again - the same bargain --coalesce-float-writes and
// --sink-loads-past-branches strike, so the flag can only ever add programs that
// compile.  Whatever this settles on stays settled for the REST of this program,
// because --sink-loads-best-of recompiles from the source lines and has to reach
// the same decision this attempt did.
//
// A LADDER rather than one all-or-nothing fallback, because measuring the three
// says the rename is not what runs out.  The peak number of simultaneously live
// float aliases is IDENTICAL with the split and without it in all three -
// vu0_rt_kernel 31, vu_script2_d_cl 22, vu_script2_td_cl 23, against 31 available
// - which is forced rather than lucky: splitting a name partitions its live
// range, so it can add names but never add values live at a line.  Two of the
// three run out nine registers below the ceiling.  What runs out is the placement
// rule the flag also turns on: preferSpreadRegister takes an untouched register
// outright, so the first thirty-one values each claim a fresh one and the
// long-lived constant `k0`, allocated later, finds all thirty-one busy somewhere
// inside its range.
//
// So the rungs back the SPREAD off, never the rename, and back it off by scope
// rather than by distance:
//
//   1  spread every float alias          - unchanged, and 67 of 70 stop here
//   2  spread only the split webs        - constants and un-split names first fit
//   3  no spread                         - the rename alone
//   4  no split                          - what the whole program did before
//
// Rung 2 is where all three land, and it is worth having: vu0_rt_kernel goes 1278
// -> 1066 modelled cycles and 470 -> 466 words, because the spread that program
// needs is over the twenty-odd names the split renamed, not over the six
// whole-program constants and the ray state that were never welded to begin with.
// Backing off by DISTANCE instead - "a register N rows away is as good as an empty
// one" - was built and measured and is not in the tree: it fixes the same three
// and costs 3491 modelled cycles across the other sixty-seven at N=4, because an
// empty register is exactly what the interleave needs.
//
// Rung 1 first, so a program that allocates today is emitted by the same code in
// the same order as before this ladder existed - byte for byte, all 70 checked.
bool Parser::allocateRegisters()
{
	const bool retrySplit = m_cmdLine.splitDeadFloatRanges();

	std::list<Token> pristine;
	const bool wasSuppressed = Error::Suppressed();
	if( retrySplit )
	{
		const std::list<Token>& current = m_tokenizer.tokens();
		pristine.insert( pristine.end(), current.begin(), current.end() );
		Error::SetSuppressed( true );
	}

	bool allocated = allocateRegistersAttempt();

	if( retrySplit && !allocated && !m_cmdLine.spreadWebsOnly() )
	{
		if( m_cmdLine.showRegisterInfo() )
			std::cerr << "Retrying allocation with the spread narrowed to the split webs" << std::endl;

		m_tokenizer.tokens().clear();
		m_tokenizer.tokens().insert( m_tokenizer.tokens().end(),
		                             pristine.begin(), pristine.end() );
		m_registerAllocator.reset();
		setVuSpreadFloatRegistersWebsOnly( true );

		allocated = allocateRegistersAttempt();
	}

	if( retrySplit && !allocated )
	{
		if( m_cmdLine.showRegisterInfo() )
			std::cerr << "Retrying allocation without the float register spread" << std::endl;

		m_tokenizer.tokens().clear();
		m_tokenizer.tokens().insert( m_tokenizer.tokens().end(),
		                             pristine.begin(), pristine.end() );
		m_registerAllocator.reset();
		setVuSpreadFloatRegistersEnabled( false );

		allocated = allocateRegistersAttempt();
	}

	if( retrySplit )
	{
		Error::SetSuppressed( wasSuppressed );

		if( !allocated )
		{
			if( m_cmdLine.showRegisterInfo() )
				std::cerr << "Retrying allocation without --split-dead-float-ranges" << std::endl;

			m_tokenizer.tokens().clear();
			m_tokenizer.tokens().insert( m_tokenizer.tokens().end(),
			                             pristine.begin(), pristine.end() );
			m_registerAllocator.reset();
			setVuSplitDeadFloatRangesEnabled( false );
			setVuSpreadFloatRegistersEnabled( false );

			allocated = allocateRegistersAttempt();
		}
	}

	if( !allocated )
		return false;

	setState( GENERATE_CODE );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::allocateRegistersAttempt()
{
	// --sink-loads-past-branches is not applied speculatively. Carrying a load
	// over a branch reorders the block it lands in, and the scheduler then does
	// something slightly different with the whole program - measured over the 45
	// generated TyraX microprograms it moved 26 of them and was worth two words
	// in either direction, three of them a row longer and one a row shorter, in
	// programs that had registers to spare and gained nothing for it. So the
	// allocation is run first exactly as it would run with the flag off, and the
	// flag only gets its turn when that runs out of registers. Same bargain the
	// --coalesce-float-writes retry inside the allocator makes: the flag can only
	// ever add programs that compile, never change one that already did.
	const bool retryPastBranches = m_cmdLine.sinkLoads() && m_cmdLine.sinkLoadsPastBranches();

	// Saved, not assumed false: allocateRegisters() may already have suppressed
	// on the way in for its own speculative attempt.
	const bool suppressedOnEntry = Error::Suppressed();

	std::list<Token> pristine;
	if( retryPastBranches )
	{
		// process() reorders the list, renumbers it and hangs dependencies off
		// its arguments, so the second attempt needs the tokenizer's own output
		// back, not the first attempt's leftovers. Copied an element at a time:
		// a Token holds a reference to the Line it was parsed from and so has no
		// assignment operator, only a copy constructor. The Lines are the
		// parser's own and outlive both attempts.
		const std::list<Token>& current = m_tokenizer.tokens();
		pristine.insert( pristine.end(), current.begin(), current.end() );
		setVuSinkLoadsPastBranchesEnabled( false );
		Error::SetSuppressed( true );
	}

	m_registerAllocator.setAvailableFloats( m_tokenizer.availableFloats() );
	m_registerAllocator.setAvailableIntegers( m_tokenizer.availableIntegers() );
	m_registerAllocator.setDynamicThreshold( m_cmdLine.threshold() );
	m_registerAllocator.setShowRegisterInfo( m_cmdLine.showRegisterInfo() );

	bool allocated = m_registerAllocator.process( m_tokenizer.tokens() );

	if( retryPastBranches )
	{
		Error::SetSuppressed( suppressedOnEntry );

		if( !allocated )
		{
			if( m_cmdLine.showRegisterInfo() )
				std::cerr << "Retrying allocation with --sink-loads-past-branches" << std::endl;

			m_tokenizer.tokens().clear();
			m_tokenizer.tokens().insert( m_tokenizer.tokens().end(),
			                             pristine.begin(), pristine.end() );
			m_registerAllocator.reset();
			setVuSinkLoadsPastBranchesEnabled( true );

			m_registerAllocator.setAvailableFloats( m_tokenizer.availableFloats() );
			m_registerAllocator.setAvailableIntegers( m_tokenizer.availableIntegers() );
			m_registerAllocator.setDynamicThreshold( m_cmdLine.threshold() );
			m_registerAllocator.setShowRegisterInfo( m_cmdLine.showRegisterInfo() );

			allocated = m_registerAllocator.process( m_tokenizer.tokens() );
		}
	}

	return allocated;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::generateCodeInto( CodeGenerator& generator, const std::string& name,
                               const std::list<Token>& tokens )
{
	generator.setEmitSource( m_cmdLine.emitSource() );
	generator.setKnownLoopOptimizations( m_cmdLine.knownLoopOptimizations() );
	generator.setGenericSoftwarePipelining( m_cmdLine.genericSoftwarePipelining() );
	generator.setStrictScheduleSlots( m_cmdLine.strictScheduleSlots() );
	generator.setName( name );

	return generator.beginProcess( tokens );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// --sink-loads-best-of.  Compile the program a second time with all four
// --sink-loads* flags off, and keep whichever whole program is smaller.
//
// The four sinking flags exist because without them eleven of the forty-five
// microprograms a TyraX project can generate run out of registers - sinking a load to
// its reader is what shortens the live range that does not fit.  But where the unsunk
// arm DOES allocate it is usually the better code: sinking moves a load next to the
// instruction that reads it, and the allocator, which runs before the scheduler, then
// writes an anti-dependence between the two that the scheduler cannot undo.  Over the
// fifty-nine programs that compile both ways the unsunk arm is smaller far more often
// than it is larger, so the flags were paying for eleven programs out of the pockets
// of the other fifty-nine.  Best-of stops that: the sinking arm becomes a fallback for
// exactly the programs that need it.
//
// Three things about the shape of this, each of which was the alternative:
//
// * The sinking arm runs FIRST and runs to completion - allocation in
//   allocateRegisters(), emission in generateCode() - through code this function does
//   not touch.  So with the flag on, a program the flag does not improve is emitted by
//   the same code, in the same order, as with the flag off.  The reverse order would
//   have made the ordinary output the second arm's, and the second arm is the one that
//   sees a heap the first arm has already churned; parts of the allocator iterate
//   std::set<Alias*>, which is address order.
//
// * The second arm re-runs the TOKENIZER rather than working from a copy of the token
//   list.  A copy would have to be taken before the first allocation and held alive
//   across the first emission, which changes what the first arm's own allocations look
//   like - the thing the point above exists to prevent.  m_lines outlives everything
//   and re-parsing it is free by comparison.
//
// * The choice is per PROGRAM, on the finished word count, exactly like
//   --clip-exemption-best-of.  A tie keeps the sinking arm, so the flag can only ever
//   be read as a win over the build without it.
void Parser::tryUnsunkArm()
{
	// A fresh tokenizer, allocator and generator, so nothing the first arm built -
	// aliases hung off token arguments, branch states, the emitted line list - can
	// leak into the second and quietly poison it.  A half-applied first pass would
	// not crash; it would emit a wrong program.
	Tokenizer tokenizer;
	tokenizer.setNewSyntax( m_cmdLine.newSyntax() );
	tokenizer.setOperands( m_operands );

	const bool sink = vuSinkLoadsEnabled();
	const bool sinkAcrossStores = vuSinkLoadsAcrossStoresEnabled();
	const bool sinkIntoLoops = vuSinkLoadsIntoLoopsEnabled();
	const bool sinkPastBranches = vuSinkLoadsPastBranchesEnabled();

	setVuSinkLoadsEnabled( false );
	setVuSinkLoadsAcrossStoresEnabled( false );
	setVuSinkLoadsIntoLoopsEnabled( false );
	setVuSinkLoadsPastBranchesEnabled( false );

	// Running out of registers is this arm's normal outcome for the eleven, and a
	// decision rather than a diagnosis - the same bargain the --sink-loads-past-branches
	// retry above makes.  Anything wrong with the INPUT was already reported by the
	// first arm, which ran unsuppressed and succeeded.
	//
	// SAVED AND RESTORED, not switched off at the end.  The two ladders in
	// allocateRegisters()/allocateRegistersAttempt() already do it that way, and
	// this one did not: it drove suppression to false unconditionally.  That is
	// harmless only because generateCode() is a different parser state from
	// allocateRegisters(), so this arm cannot run inside theirs today - exactly
	// the "latent, not reachable" shape as the unbounded alias walk in
	// RegisterAllocator.  A future caller that runs a speculative arm around
	// code generation would have its suppression silently dropped here, and the
	// errors it is entitled to swallow would fail the build instead.
	const bool suppressedOnEntry = Error::Suppressed();
	Error::SetSuppressed( true );

	bool built = true;
	for( std::list<Line>::const_iterator i = m_lines.begin(); built && i != m_lines.end(); ++i )
	{
		if( !tokenizer.parse( *i ) )
			built = false;
	}

	CodeGenerator generator;

	if( built )
	{
		RegisterAllocator allocator;
		allocator.setAvailableFloats( tokenizer.availableFloats() );
		allocator.setAvailableIntegers( tokenizer.availableIntegers() );
		allocator.setDynamicThreshold( m_cmdLine.threshold() );
		// Not setShowRegisterInfo: --show-reg-alloc describes the allocation that
		// produced the output, and this one usually does not.
		built = allocator.process( tokenizer.tokens() );

		if( built )
			built = generateCodeInto( generator, allocator.name(), tokenizer.tokens() );
	}

	Error::SetSuppressed( suppressedOnEntry );

	setVuSinkLoadsEnabled( sink );
	setVuSinkLoadsAcrossStoresEnabled( sinkAcrossStores );
	setVuSinkLoadsIntoLoopsEnabled( sinkIntoLoops );
	setVuSinkLoadsPastBranchesEnabled( sinkPastBranches );

	if( !built )
	{
		if( m_cmdLine.showPairMisses() )
			std::cerr << "[sinkbestof] unsunk arm did not compile, keeping the sunk one" << std::endl;
		return;
	}

	const unsigned int sunkWords = m_codeGenerator.emittedWordCount();
	const unsigned int unsunkWords = generator.emittedWordCount();

	if( m_cmdLine.showPairMisses() )
		std::cerr << "[sinkbestof] sunk=" << sunkWords
		          << " unsunk=" << unsunkWords
		          << " keep=" << ( unsunkWords < sunkWords ? "unsunk" : "sunk" ) << std::endl;

	if( unsunkWords >= sunkWords )
		return;

	std::stringstream text;
	generator.write( text );
	m_unsunkOutput = text.str();
	m_haveUnsunkOutput = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::generateCode()
{
	if( !generateCodeInto( m_codeGenerator, m_registerAllocator.name(), m_tokenizer.tokens() ) )
		return false;

	if( m_cmdLine.sinkLoads() && m_cmdLine.sinkLoadsBestOf() )
		tryUnsunkArm();

	setState( WRITE_OUTPUT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Parser::writeOutput()
{
	if( m_cmdLine.output() == "-" )
		return writeOutputStream( std::cout );

	if( m_cmdLine.output().length() > 0 )
	{
		std::ofstream stream( m_cmdLine.output().c_str() );

		if( !stream.good() )
		{
			Error::Display( Error( "Could not open output file" ) );
			return false;
		}

		return writeOutputStream( stream );
	}
	else
	{
		return writeOutputStream( std::cout );
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::writeOutputStream( std::ostream& stream )
{
	if( m_haveUnsunkOutput )
		stream << m_unsunkOutput;
	else
		m_codeGenerator.write( stream );

	setState( EXIT );

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::showVersion()
{
	std::cout << "OpenVCL Version " << OPENVCL_VERSION << std::endl;

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Parser::showUsage()
{
	m_cmdLine.showUsage( std::cout );

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::string Parser::tempFilename()
{
#ifdef _WIN32
	char path[MAX_PATH];
	char buffer[MAX_PATH];

	if( GetTempPath( sizeof( path ), path ) )
	{
		if( GetTempFileName( path, "vcl", 0, buffer ) )
		{
			return buffer;
		}
	}
#else
	for( unsigned int count = 0; count < TEMPFILE_ATTEMPTS; count++ )
	{
		std::stringstream buffer;
		
		buffer << "/tmp/vcl" << std::hex << std::setfill('0') << std::setw(8) << ((getpid()+rand()) << 8) + m_tempCounter++;
		std::string temp = buffer.str();

		std::ofstream file(temp.c_str(),std::ifstream::out);

		if(file.good())
		{
			file.close();
			return temp;
		}
	}
#endif
	return "";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Operand* Parser::getOperand( const char* name, unsigned int flags )
{
	for( std::list<Operand>::iterator i = m_operands.begin(); i != m_operands.end(); ++i )
	{
		if( !(*i).name().compare( name ) && ((*i).flags() == flags) )
			return &*i;
	}

	return NULL;
}

}
