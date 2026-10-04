/*
 * CommandLine.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "CommandLine.h"

#include <string.h>
#include <stdlib.h>
#include <utility>

namespace vcl
{

namespace
{
	void appendCostLoop( std::vector< std::pair<std::string, unsigned int> >& loops,
	                     const char* label,
	                     unsigned int repeat )
	{
		loops.push_back( std::make_pair( std::string(label), repeat ) );
	}

	bool appendCostLoopPreset( const std::string& preset,
	                           std::vector< std::pair<std::string, unsigned int> >& loops )
	{
		if( preset != "ps2gl" && preset != "ps2gl-100" )
			return false;

		appendCostLoop( loops, "xform_loop_lid", 100 );
		appendCostLoop( loops, "dir_light_vert_loop_lid", 100 );
		appendCostLoop( loops, "pt_light_vert_loop_lid", 100 );
		appendCostLoop( loops, "final_loop_lid", 100 );
		return true;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CommandLine::CommandLine()
{
	m_emitSource	= false;
	m_reduceCode	= true;
	m_dumbCode	= false;
	m_generateE	= true;
	m_alignCode	= true;
	m_runGasp	= false;
	m_runCpp	= false;
	m_deleteTemp	= true;
	m_unrollLoops	= true;
	m_generateMpg	= false;
	m_newSyntax	= false;
	m_showUsage = false;

	m_threshold = 16;
	m_timeout	= 4;
	m_showVersion = false;
	m_showRegisterInfo = false;
	m_analyzeVsmCost = false;
	m_analyzeVsmCostJson = false;
	m_compareVsmCost = false;
	m_compareVsmCostJson = false;
	m_compareVsmCostMarkdown = false;
	m_compareVsmCostListMarkdown = false;
	m_compareVsmCostListCheck = false;
	m_dumpInstructionInfo = false;
	m_dumpInstructionInfoJson = false;
	m_dumpLoopPipelineInfo = false;
	m_dumpLoopPipelineInfoJson = false;
	m_dumpScheduleInfo = false;
	m_dumpScheduleInfoJson = false;
	m_knownLoopOptimizations = false;
	m_scheduleFlagReaders = false;
	m_fmacInterlock = false;
	m_pairBestOfCycles = false;
	m_sceLatencies = false;
	m_emitDelayFillers = false;
	m_branchInterlock = false;
	m_loopLivenessAlways = false;
	m_upperMoveWithW = false;
	m_coalesceFloatWrites = false;
	m_splitDeadFloatRanges = false;
	m_spreadWebsOnly = false;
	m_trimUncarriedRanges = false;
	m_sinkLoads = false;
	m_sinkLoadsAcrossStores = false;
	m_sinkLoadsIntoLoops = false;
	m_sinkLoadsPastBranches = false;
	m_sinkLoadsBestOf = false;
	m_dropDeadWrites = false;
	m_exemptFullClipMasks = false;
	m_clipExemptionBestOf = false;
	m_showPairMisses = false;
	m_pairBestOfTwo = false;
	m_pairBestOfMany = false;
	m_branchBubbleOnDependency = false;
	m_genericSoftwarePipelining = true;
	m_strictScheduleSlots = false;

	m_gasp = "gasp";
	m_cpp = "cpp";

	// setup arguments

	m_options.push_back(Option('c',NULL,EMIT_SOURCECODE,false));
	m_options.push_back(Option('C',NULL,REDUCE_CODE,false));
	m_options.push_back(Option('d',NULL,DUMB_CODE,false));
	m_options.push_back(Option('e',NULL,GENERATE_E,false));
	m_options.push_back(Option('f',NULL,ALIGN_CODE,false));
	m_options.push_back(Option('g',NULL,RUN_GASP,false));
	m_options.push_back(Option('G',NULL,RUN_CPP,false));
	m_options.push_back(Option('h',NULL,SHOW_USAGE,false));
	m_options.push_back(Option('I',NULL,INCLUDE,true));
	m_options.push_back(Option('K',NULL,DELETE_TEMP,false));
	m_options.push_back(Option('L',NULL,UNROLL_LOOPS,false));
	m_options.push_back(Option('m',NULL,GENERATE_MPG,false));
	m_options.push_back(Option('n',NULL,NEW_SYNTAX,false));
	m_options.push_back(Option('o',NULL,OUTPUT,true));
	m_options.push_back(Option('t',NULL,TIMEOUT,true));
	m_options.push_back(Option('u',NULL,LABEL,true));

	m_options.push_back(Option('M',NULL,IGNORE,false));
	m_options.push_back(Option('P',NULL,IGNORE,false));
	m_options.push_back(Option('Z',NULL,IGNORE,false));

	m_options.push_back(Option('\0',"gasp",GASP_NAME,true));
	m_options.push_back(Option('\0',"cpp",CPP_NAME,true));
	m_options.push_back(Option('\0',"bthres",BRANCH_THRESHOLD,true));
	m_options.push_back(Option('\0',"version",SHOW_VERSION,false));
	m_options.push_back(Option('\0',"show-reg-alloc",SHOW_REGISTER_INFO,false));
	m_options.push_back(Option('\0',"cost",ANALYZE_VSM_COST,false));
	m_options.push_back(Option('\0',"cost-json",ANALYZE_VSM_COST_JSON,false));
	m_options.push_back(Option('\0',"cost-loop",ANALYZE_VSM_COST_LOOP,true));
	m_options.push_back(Option('\0',"cost-loop-preset",ANALYZE_VSM_COST_LOOP_PRESET,true));
	m_options.push_back(Option('\0',"cost-compare",ANALYZE_VSM_COST_COMPARE,true));
	m_options.push_back(Option('\0',"cost-compare-json",ANALYZE_VSM_COST_COMPARE_JSON,true));
	m_options.push_back(Option('\0',"cost-compare-markdown",ANALYZE_VSM_COST_COMPARE_MARKDOWN,true));
	m_options.push_back(Option('\0',"cost-compare-list-markdown",ANALYZE_VSM_COST_COMPARE_LIST_MARKDOWN,false));
	m_options.push_back(Option('\0',"cost-compare-list-check",ANALYZE_VSM_COST_COMPARE_LIST_CHECK,true));
	m_options.push_back(Option('\0',"dump-instruction-info",DUMP_INSTRUCTION_INFO,false));
	m_options.push_back(Option('\0',"dump-instruction-info-json",DUMP_INSTRUCTION_INFO_JSON,false));
	m_options.push_back(Option('\0',"dump-loop-pipeline-info",DUMP_LOOP_PIPELINE_INFO,false));
	m_options.push_back(Option('\0',"dump-loop-pipeline-info-json",DUMP_LOOP_PIPELINE_INFO_JSON,false));
	m_options.push_back(Option('\0',"dump-schedule-info",DUMP_SCHEDULE_INFO,false));
	m_options.push_back(Option('\0',"dump-schedule-info-json",DUMP_SCHEDULE_INFO_JSON,false));
	m_options.push_back(Option('\0',"enable-known-loop-optimizations",ENABLE_KNOWN_LOOP_OPTIMIZATIONS,false));
	m_options.push_back(Option('\0',"schedule-flag-readers",SCHEDULE_FLAG_READERS,false));
	m_options.push_back(Option('\0',"fmac-interlock",FMAC_INTERLOCK,false));
	m_options.push_back(Option(0,"pair-best-of-cycles",PAIR_BEST_OF_CYCLES,false));
	m_options.push_back(Option(0,"sce-latencies",SCE_LATENCIES,false));
	m_options.push_back(Option(0,"emit-delay-fillers",EMIT_DELAY_FILLERS,false));
	m_options.push_back(Option(0,"branch-interlock",BRANCH_INTERLOCK,false));
	m_options.push_back(Option(0,"loop-liveness-always",LOOP_LIVENESS_ALWAYS,false));
	m_options.push_back(Option(0,"upper-move-with-w",UPPER_MOVE_WITH_W,false));
	m_options.push_back(Option(0,"coalesce-float-writes",COALESCE_FLOAT_WRITES,false));
	m_options.push_back(Option(0,"split-dead-float-ranges",SPLIT_DEAD_FLOAT_RANGES,false));
	m_options.push_back(Option(0,"spread-webs-only",SPREAD_WEBS_ONLY,false));
	m_options.push_back(Option(0,"trim-uncarried-ranges",TRIM_UNCARRIED_RANGES,false));
	m_options.push_back(Option(0,"sink-loads",SINK_LOADS,false));
	m_options.push_back(Option(0,"sink-loads-across-stores",SINK_LOADS_ACROSS_STORES,false));
	m_options.push_back(Option(0,"sink-loads-into-loops",SINK_LOADS_INTO_LOOPS,false));
	m_options.push_back(Option(0,"sink-loads-past-branches",SINK_LOADS_PAST_BRANCHES,false));
	m_options.push_back(Option(0,"sink-loads-best-of",SINK_LOADS_BEST_OF,false));
	m_options.push_back(Option(0,"drop-dead-writes",DROP_DEAD_WRITES,false));
	m_options.push_back(Option(0,"exempt-full-clip-masks",EXEMPT_FULL_CLIP_MASKS,false));
	m_options.push_back(Option(0,"clip-exemption-best-of",CLIP_EXEMPTION_BEST_OF,false));
	m_options.push_back(Option(0,"show-pair-misses",SHOW_PAIR_MISSES,false));
	m_options.push_back(Option(0,"pair-best-of-two",PAIR_BEST_OF_TWO,false));
	m_options.push_back(Option(0,"pair-best-of-many",PAIR_BEST_OF_MANY,false));
	m_options.push_back(Option(0,"branch-bubble-on-dependency",BRANCH_BUBBLE_ON_DEPENDENCY,false));
	m_options.push_back(Option('\0',"disable-known-loop-optimizations",DISABLE_KNOWN_LOOP_OPTIMIZATIONS,false));
	m_options.push_back(Option('\0',"enable-generic-software-pipelining",ENABLE_GENERIC_SOFTWARE_PIPELINING,false));
	m_options.push_back(Option('\0',"disable-generic-software-pipelining",DISABLE_GENERIC_SOFTWARE_PIPELINING,false));
	m_options.push_back(Option('\0',"strict-schedule-slots",STRICT_SCHEDULE_SLOTS,false));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CommandLine::parse( int argc, char* argv[] )
{
	for( int i = 1; i < argc; i++ )
	{
		const Option* option = NULL;
		std::string argument;
		bool separate = false;

		if( !strncmp("--",argv[i],2) )
		{
			std::string operand = &argv[i][2];

			if( (argc-1) > i )
				argument = argv[i+1];

			separate = true;

			for( std::vector<Option>::const_iterator j = m_options.begin(); j != m_options.end(); j++ )
			{
				if( operand == (*j).longName() )
				{
					option = &(*j);
					break;
				}
			}

			if( !option )
				return false;
		}
		else if( !strncmp("-",argv[i],1) )
		{
			char c = argv[i][1];

			if( !c )
				return false;

			if( !argv[i][2] )
			{
				if( (argc-1) > i )
					argument = argv[i+1];

				separate = true;
			}
			else
				argument = &argv[i][2];

			for( std::vector<Option>::const_iterator j = m_options.begin(); j != m_options.end(); j++ )
			{
				if( c == (*j).shortName() )
				{
					option = &(*j);
					break;
				}
			}

			if( !option )
				return false;
		}
		else
		{
			m_input = argv[i];
		}

		if( option )
		{
			if( !argument.length() && option->argument() )
				return false;

			switch( option->action() )
			{
				case EMIT_SOURCECODE: m_emitSource = true; break;
				case REDUCE_CODE: m_reduceCode = false; break;
				case DUMB_CODE: m_dumbCode = true; break;
				case GENERATE_E: m_generateE = false; break;
				case ALIGN_CODE: m_alignCode = false; break;
				case RUN_GASP: m_runGasp = true; break;
				case RUN_CPP: m_runCpp = true; break;
				case SHOW_USAGE: m_showUsage = true; break;
				case INCLUDE: m_includes.push_back( argument ); break;
				case DELETE_TEMP: m_deleteTemp = false; break;
				case UNROLL_LOOPS: m_unrollLoops = false; break;
				case GENERATE_MPG: m_generateMpg = true; break;
				case NEW_SYNTAX: m_newSyntax = true; break;
				case OUTPUT: m_output = argument; break;
				case TIMEOUT: m_timeout = strtoul(argument.c_str(),NULL,10); break;
				case LABEL: m_label = argument; break;

				case GASP_NAME: m_gasp = argument; break;
				case CPP_NAME: m_cpp = argument; break;
				case BRANCH_THRESHOLD: m_threshold = strtoul(argument.c_str(),NULL,10); break;
				case SHOW_VERSION: m_showVersion = true; break;
				case SHOW_REGISTER_INFO: m_showRegisterInfo = true; break;
				case ANALYZE_VSM_COST: m_analyzeVsmCost = true; break;
				case ANALYZE_VSM_COST_JSON: m_analyzeVsmCost = true; m_analyzeVsmCostJson = true; break;
				case ANALYZE_VSM_COST_COMPARE: m_compareVsmCost = true; m_compareVsmCostJson = false; m_compareVsmCostMarkdown = false; m_costCompareBaseline = argument; break;
				case ANALYZE_VSM_COST_COMPARE_JSON: m_compareVsmCost = true; m_compareVsmCostJson = true; m_compareVsmCostMarkdown = false; m_costCompareBaseline = argument; break;
				case ANALYZE_VSM_COST_COMPARE_MARKDOWN: m_compareVsmCost = true; m_compareVsmCostJson = false; m_compareVsmCostMarkdown = true; m_costCompareBaseline = argument; break;
				case ANALYZE_VSM_COST_COMPARE_LIST_MARKDOWN: m_compareVsmCostListMarkdown = true; break;
				case ANALYZE_VSM_COST_COMPARE_LIST_CHECK: m_compareVsmCostListCheck = true; m_costCompareListCheckMetric = argument; break;
				case DUMP_INSTRUCTION_INFO: m_dumpInstructionInfo = true; break;
				case DUMP_INSTRUCTION_INFO_JSON: m_dumpInstructionInfo = true; m_dumpInstructionInfoJson = true; break;
				case DUMP_LOOP_PIPELINE_INFO: m_dumpLoopPipelineInfo = true; break;
				case DUMP_LOOP_PIPELINE_INFO_JSON: m_dumpLoopPipelineInfo = true; m_dumpLoopPipelineInfoJson = true; break;
				case DUMP_SCHEDULE_INFO: m_dumpScheduleInfo = true; break;
				case DUMP_SCHEDULE_INFO_JSON: m_dumpScheduleInfo = true; m_dumpScheduleInfoJson = true; break;
				case ENABLE_KNOWN_LOOP_OPTIMIZATIONS: m_knownLoopOptimizations = true; break;
				case SCHEDULE_FLAG_READERS: m_scheduleFlagReaders = true; break;
				case FMAC_INTERLOCK: m_fmacInterlock = true; break;
				case PAIR_BEST_OF_CYCLES: m_pairBestOfCycles = true; break;
				case SCE_LATENCIES: m_sceLatencies = true; break;
				case EMIT_DELAY_FILLERS: m_emitDelayFillers = true; break;
				case BRANCH_INTERLOCK: m_branchInterlock = true; break;
				case LOOP_LIVENESS_ALWAYS: m_loopLivenessAlways = true; break;
				case UPPER_MOVE_WITH_W: m_upperMoveWithW = true; break;
				case COALESCE_FLOAT_WRITES: m_coalesceFloatWrites = true; break;
				case SPLIT_DEAD_FLOAT_RANGES: m_splitDeadFloatRanges = true; break;
				case SPREAD_WEBS_ONLY: m_spreadWebsOnly = true; break;
				case TRIM_UNCARRIED_RANGES: m_trimUncarriedRanges = true; break;
				case SINK_LOADS: m_sinkLoads = true; break;
				case SINK_LOADS_ACROSS_STORES: m_sinkLoadsAcrossStores = true; break;
				case SINK_LOADS_INTO_LOOPS: m_sinkLoadsIntoLoops = true; break;
				case SINK_LOADS_PAST_BRANCHES: m_sinkLoadsPastBranches = true; break;
				case SINK_LOADS_BEST_OF: m_sinkLoadsBestOf = true; break;
				case DROP_DEAD_WRITES: m_dropDeadWrites = true; break;
				case EXEMPT_FULL_CLIP_MASKS: m_exemptFullClipMasks = true; break;
				case CLIP_EXEMPTION_BEST_OF: m_clipExemptionBestOf = true; break;
				case SHOW_PAIR_MISSES: m_showPairMisses = true; break;
				case PAIR_BEST_OF_TWO: m_pairBestOfTwo = true; break;
				case PAIR_BEST_OF_MANY: m_pairBestOfMany = true; break;
				case BRANCH_BUBBLE_ON_DEPENDENCY: m_branchBubbleOnDependency = true; break;
				case DISABLE_KNOWN_LOOP_OPTIMIZATIONS: m_knownLoopOptimizations = false; break;
				case ENABLE_GENERIC_SOFTWARE_PIPELINING: m_genericSoftwarePipelining = true; break;
				case DISABLE_GENERIC_SOFTWARE_PIPELINING: m_genericSoftwarePipelining = false; break;
				case STRICT_SCHEDULE_SLOTS: m_strictScheduleSlots = true; break;
				case ANALYZE_VSM_COST_LOOP:
				{
					std::string::size_type separator = argument.find('=');
					if( separator == std::string::npos || separator == 0 || separator == argument.size() - 1 )
						return false;
					std::string label = argument.substr(0, separator);
					const char* countText = argument.c_str() + separator + 1;
					char* end = NULL;
					unsigned long repeat = strtoul(countText, &end, 10);
					if( !countText[0] || (end && *end) || repeat == 0 )
						return false;
					m_analyzeVsmCost = true;
					m_costLoops.push_back(std::make_pair(label, static_cast<unsigned int>(repeat)));
					break;
				}
				case ANALYZE_VSM_COST_LOOP_PRESET:
					m_analyzeVsmCost = true;
					if( !appendCostLoopPreset( argument, m_costLoops ) )
						return false;
					break;

				case IGNORE: break;
				break;

				default: return false;
			}

			if( separate && option->argument() )
				i++;
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CommandLine::showUsage( std::ostream& stream )
{
	stream << "OpenVCL [parameters] <input>" << std::endl;

	stream << std::endl << "  Parameters:" << std::endl;

	stream << "  -c              Emit nearly original source code as comments." << std::endl;
	stream << "  -C              Disable the code reduction pass." << std::endl;
	stream << "  -d              Dumb code is generated." << std::endl;
	stream << "  -e              Disable the generation of [E] bits at the end of the code." << std::endl;
	stream << "  -f              Disable the generation of alignment directives (.align n)" << std::endl;
	stream << "  -g              Run gasp on the input before any VCL-specific task is done" << std::endl;
	stream << "                    Commandline: \"-p -s -c ';'\". \"-I\" is also passed." << std::endl;
	stream << "  -G              Run the C preprocessor on the input." << std::endl;
	stream << "  -h              Print out the command-line help." << std::endl;
	stream << "  -I<includepath> To be used with \"-g\"; tells gasp where to find include-files." << std::endl;
	stream << "  -K              Temporary files created by pre-processors are not deleted." << std::endl;
	stream << "  -L              Globally disable loop code generation." << std::endl;
	stream << "  -M              Kept for compatibility with VCL." << std::endl;
	stream << "  -m              Generate \".mpg\" and DMA tags automatically." << std::endl;
	stream << "  -n              Enable new syntax." << std::endl;
	stream << "  -o<output>      Output filename." << std::endl;
	stream << "  -P              Kept for compatibility with VCL." << std::endl;
	stream << "  -t<timeout>     Specify optimizer timeout." << std::endl;
	stream << "  -u<string>      <string> is used as a unique string for label generation." << std::endl;
	stream << "  -Z              Kept for compatibility with VCL." << std::endl;

	stream << std::endl << "  OpenVCL exclusive parameters:" << std::endl;

	stream << "  --gasp <name>      Execute <name> instead of 'gasp' when preprocessing." << std::endl;
	stream << "  --cpp <name>       Execute <name> instead of 'cpp' when preprocessing." << std::endl;
	stream << "  --bthres <val>     Number of times a dynamic branch can be visited. (Default: 16)" << std::endl;
	stream << "  --version          Show program version." << std::endl;
	stream << "  --show-reg-alloc   Show register allocation information during compilation." << std::endl;
	stream << "  --cost             Analyze scheduled .vsm cost instead of compiling VCL." << std::endl;
	stream << "  --cost-json        Analyze scheduled .vsm cost and emit JSON." << std::endl;
	stream << "  --cost-loop L=N    Weight block/label L by N iterations in cost reports." << std::endl;
	stream << "  --cost-loop-preset ps2gl  Apply known ps2gl hot-loop weights." << std::endl;
	stream << "  --cost-compare <baseline>       Compare input .vsm cost against baseline." << std::endl;
	stream << "  --cost-compare-json <baseline>  Compare input .vsm cost against baseline as JSON." << std::endl;
	stream << "  --cost-compare-markdown <baseline>  Compare input .vsm cost against baseline as Markdown." << std::endl;
	stream << "  --cost-compare-list-markdown  Read baseline/candidate pairs and emit a Markdown table." << std::endl;
	stream << "  --cost-compare-list-check <metric>  Fail when any listed candidate exceeds its baseline." << std::endl;
	stream << "  --dump-instruction-info       Print the VU instruction metadata table." << std::endl;
	stream << "  --dump-instruction-info-json  Print the VU instruction metadata table as JSON." << std::endl;
	stream << "  --dump-loop-pipeline-info       Print loop-carried Q pipeline opportunities." << std::endl;
	stream << "  --dump-loop-pipeline-info-json  Print loop-carried Q pipeline opportunities as JSON." << std::endl;
	stream << "  --dump-schedule-info       Print generic ready-scheduler issue slots." << std::endl;
	stream << "  --dump-schedule-info-json  Print generic ready-scheduler issue slots as JSON." << std::endl;
	stream << "  --enable-known-loop-optimizations  Compile with ps2gl-shaped loop reference emitters." << std::endl;
	stream << "  --schedule-flag-readers            Let MAC/CLIP flag readers take part in scheduling." << std::endl;
	stream << "  --fmac-interlock                   Trust the VU FMAC interlock; do not pad VF waits with nops." << std::endl;
	stream << "  --pair-best-of-cycles              Also try each ready strategy with a stall-aware partner filter; take it when it is faster and no bigger." << std::endl;
	stream << "  --sce-latencies                    Non-interlocked latencies calibrated to what SCE vcl emits." << std::endl;
	stream << "  --disable-known-loop-optimizations  Kept for compatibility; generic compilation is the default." << std::endl;
	stream << "  --enable-generic-software-pipelining  Enable generic software-pipeline rewrites. (Default)" << std::endl;
	stream << "  --disable-generic-software-pipelining  Disable generic software-pipeline rewrites." << std::endl;
	stream << "  --strict-schedule-slots  Emit from scheduler-selected pairs without legacy lookahead fallbacks. (Default for generic codegen)" << std::endl;

	stream << std::endl << "  If no input or output file are specified, standard I/O will be used instead." << std::endl;

	stream << std::flush;
}

}
