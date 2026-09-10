#ifndef __OPENVCL_COMMANDLINE_H__
#define __OPENVCL_COMMANDLINE_H__

/*
 * CommandLine.h
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <string>
#include <list>
#include <istream>
#include <utility>
#include <vector>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

class CommandLine
{
public:

	CommandLine();

	bool parse( int argc, char* argv[] );

	bool emitSource() const;
	bool reduceCode() const;
	bool dumbCode() const;
	bool generateE() const;
	bool alignCode() const;
	bool deleteTemp() const;
	bool unrollLoops() const;
	bool generateMpg() const;
	bool showUsage() const;
	bool showRegisterInfo() const;
	bool analyzeVsmCost() const;
	bool analyzeVsmCostJson() const;
	bool compareVsmCost() const;
	bool compareVsmCostJson() const;
	bool compareVsmCostMarkdown() const;
	bool compareVsmCostListMarkdown() const;
	bool compareVsmCostListCheck() const;
	bool dumpInstructionInfo() const;
	bool dumpInstructionInfoJson() const;
	bool dumpLoopPipelineInfo() const;
	bool dumpLoopPipelineInfoJson() const;
	bool dumpScheduleInfo() const;
	bool dumpScheduleInfoJson() const;
	bool knownLoopOptimizations() const;
	bool scheduleFlagReaders() const;
	bool fmacInterlock() const;
	bool pairBestOfCycles() const;
	bool sceLatencies() const;
	bool emitDelayFillers() const;
	bool branchInterlock() const;
	bool loopLivenessAlways() const;
	bool upperMoveWithW() const;
	bool coalesceFloatWrites() const;
	bool splitDeadFloatRanges() const;
	bool spreadWebsOnly() const;
	bool trimUncarriedRanges() const;
	bool sinkLoads() const;
	bool sinkLoadsAcrossStores() const;
	bool sinkLoadsIntoLoops() const;
	bool sinkLoadsPastBranches() const;
	bool sinkLoadsBestOf() const;
	bool dropDeadWrites() const;
	bool exemptFullClipMasks() const;
	bool clipExemptionBestOf() const;
	bool showPairMisses() const;
	bool pairBestOfTwo() const;
	bool pairBestOfMany() const;
	bool branchBubbleOnDependency() const;
	bool genericSoftwarePipelining() const;
	bool strictScheduleSlots() const;
	const std::vector< std::pair<std::string, unsigned int> >& costLoops() const;
	const std::string& costCompareBaseline() const;
	const std::string& costCompareListCheckMetric() const;

	void setRunGasp( bool runGasp );
	bool runGasp() const;
	void setRunCpp( bool runCpp );
	bool runCpp() const;
	void setNewSyntax( bool newSyntax );
	bool newSyntax() const;

	unsigned int threshold() const;
	unsigned int timeout() const;
	bool showVersion() const;

	std::list<std::string>& includes();
	
	std::string& input();
	std::string& output();

	std::string& label();
	std::string& gasp();
	std::string& cpp();

	void showUsage( std::ostream& stream );

private:

	enum Action	
	{
		// standard VCL options

		EMIT_SOURCECODE,
		REDUCE_CODE,
		DUMB_CODE,
		GENERATE_E,
		ALIGN_CODE,
		RUN_GASP,
		RUN_CPP,
		INCLUDE,
		DELETE_TEMP,
		UNROLL_LOOPS,
		GENERATE_MPG,
		NEW_SYNTAX,
		OUTPUT,
		SHOW_USAGE,
		TIMEOUT,
		LABEL,

		// extended options

		GASP_NAME,
		CPP_NAME,
		BRANCH_THRESHOLD,
		SHOW_VERSION,
		SHOW_REGISTER_INFO,
		ANALYZE_VSM_COST,
		ANALYZE_VSM_COST_JSON,
		ANALYZE_VSM_COST_LOOP,
		ANALYZE_VSM_COST_LOOP_PRESET,
		ANALYZE_VSM_COST_COMPARE,
		ANALYZE_VSM_COST_COMPARE_JSON,
		ANALYZE_VSM_COST_COMPARE_MARKDOWN,
		ANALYZE_VSM_COST_COMPARE_LIST_MARKDOWN,
		ANALYZE_VSM_COST_COMPARE_LIST_CHECK,
		DUMP_INSTRUCTION_INFO,
		DUMP_INSTRUCTION_INFO_JSON,
		DUMP_LOOP_PIPELINE_INFO,
		DUMP_LOOP_PIPELINE_INFO_JSON,
		DUMP_SCHEDULE_INFO,
		DUMP_SCHEDULE_INFO_JSON,
		ENABLE_KNOWN_LOOP_OPTIMIZATIONS,
		SCHEDULE_FLAG_READERS,
		FMAC_INTERLOCK,
		PAIR_BEST_OF_CYCLES,
		SCE_LATENCIES,
		EMIT_DELAY_FILLERS,
		BRANCH_INTERLOCK,
		LOOP_LIVENESS_ALWAYS,
		UPPER_MOVE_WITH_W,
		COALESCE_FLOAT_WRITES,
		SPLIT_DEAD_FLOAT_RANGES,
		SPREAD_WEBS_ONLY,
		TRIM_UNCARRIED_RANGES,
		SINK_LOADS,
		SINK_LOADS_ACROSS_STORES,
		SINK_LOADS_INTO_LOOPS,
		SINK_LOADS_PAST_BRANCHES,
		SINK_LOADS_BEST_OF,
		DROP_DEAD_WRITES,
		EXEMPT_FULL_CLIP_MASKS,
		CLIP_EXEMPTION_BEST_OF,
		SHOW_PAIR_MISSES,
		PAIR_BEST_OF_TWO,
		PAIR_BEST_OF_MANY,
		BRANCH_BUBBLE_ON_DEPENDENCY,
		DISABLE_KNOWN_LOOP_OPTIMIZATIONS,
		ENABLE_GENERIC_SOFTWARE_PIPELINING,
		DISABLE_GENERIC_SOFTWARE_PIPELINING,
		STRICT_SCHEDULE_SLOTS,

		IGNORE
	};

	class Option
	{
	public:

		Option( char s, const char* l, Action ac, bool a );
		Option( const Option& o );

		char shortName() const;
		const std::string& longName() const;
		Action action() const;
		bool argument() const;

	private:

		char m_shortName;
		std::string m_longName;
		Action m_action;
		bool m_argument;
	};

	bool m_emitSource;
	bool m_reduceCode;
	bool m_dumbCode;
	bool m_generateE;
	bool m_alignCode;
	bool m_runGasp;
	bool m_runCpp;
	bool m_deleteTemp;
	bool m_unrollLoops;
	bool m_generateMpg;
	bool m_newSyntax;
	bool m_showUsage;
	bool m_showRegisterInfo;
	bool m_analyzeVsmCost;
	bool m_analyzeVsmCostJson;
	bool m_compareVsmCost;
	bool m_compareVsmCostJson;
	bool m_compareVsmCostMarkdown;
	bool m_compareVsmCostListMarkdown;
	bool m_compareVsmCostListCheck;
	bool m_dumpInstructionInfo;
	bool m_dumpInstructionInfoJson;
	bool m_dumpLoopPipelineInfo;
	bool m_dumpLoopPipelineInfoJson;
	bool m_dumpScheduleInfo;
	bool m_dumpScheduleInfoJson;
	bool m_knownLoopOptimizations;
	bool m_scheduleFlagReaders;
	bool m_fmacInterlock;
	bool m_pairBestOfCycles;
	bool m_sceLatencies;
	bool m_emitDelayFillers;
	bool m_branchInterlock;
	bool m_loopLivenessAlways;
	bool m_upperMoveWithW;
	bool m_coalesceFloatWrites;
	bool m_splitDeadFloatRanges;
	bool m_spreadWebsOnly;
	bool m_trimUncarriedRanges;
	bool m_sinkLoads;
	bool m_sinkLoadsAcrossStores;
	bool m_sinkLoadsIntoLoops;
	bool m_sinkLoadsPastBranches;
	bool m_sinkLoadsBestOf;
	bool m_dropDeadWrites;
	bool m_exemptFullClipMasks;
	bool m_clipExemptionBestOf;
	bool m_showPairMisses;
	bool m_pairBestOfTwo;
	bool m_pairBestOfMany;
	bool m_branchBubbleOnDependency;
	bool m_genericSoftwarePipelining;
	bool m_strictScheduleSlots;
	std::vector< std::pair<std::string, unsigned int> > m_costLoops;
	std::string m_costCompareBaseline;
	std::string m_costCompareListCheckMetric;

	unsigned int m_threshold;
	unsigned int m_timeout;
	bool m_showVersion;

	std::vector<Option> m_options;

	std::list< std::string > m_includes;

	std::string m_input;
	std::string m_output;

	std::string m_label;
	std::string m_gasp;
	std::string m_cpp;
};

#include "CommandLine.inl"

}

#endif
