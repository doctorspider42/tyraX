#ifndef __OPENVCL_VSMCOSTANALYZER_H__
#define __OPENVCL_VSMCOSTANALYZER_H__

/*
 * VsmCostAnalyzer.h
 *
 * Static cost reporting for already-scheduled VU .vsm files.
 */

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace vcl
{

class VsmCostAnalyzer
{
public:
	struct Summary
	{
		Summary();

		std::string input;
		unsigned int staticCycles;
		unsigned int estimatedTotalCycles;
		unsigned int issueStallCycles;
		unsigned int fdivIssueStallCycles;
		unsigned int efuIssueStallCycles;
		unsigned int waitStallCycles;
		unsigned int waitqStallCycles;
		unsigned int waitpStallCycles;
		unsigned int weightedStaticCycles;
		unsigned int weightedEstimatedTotalCycles;
		unsigned int weightedIssueStallCycles;
		unsigned int weightedFdivIssueStallCycles;
		unsigned int weightedEfuIssueStallCycles;
		unsigned int weightedWaitStallCycles;
		unsigned int weightedWaitqStallCycles;
		unsigned int weightedWaitpStallCycles;
		unsigned int weightedInstructions;
		unsigned int weightedPairedCycles;
		unsigned int weightedNopOnlyCycles;
		unsigned int weightedNopSlots;
		unsigned int affineStaticBaseCycles;
		unsigned int affineStaticLoopCycles;
		unsigned int affineEstimatedBaseCycles;
		unsigned int affineEstimatedLoopCycles;
		unsigned int instructions;
		unsigned int upperInstructions;
		unsigned int lowerInstructions;
		unsigned int pairedCycles;
		unsigned int nopOnlyCycles;
		unsigned int nopSlots;
		unsigned int operationLatencyCycles;
		unsigned int longLatencyOps;
		unsigned int longLatencyCycles;
		unsigned int maxOpLatency;
	};

	VsmCostAnalyzer();

	bool analyze( std::istream& stream, const std::string& inputName );
	void setBlockRepeat( const std::string& label, unsigned int repeat );
	Summary summary() const;

	bool writeText( std::ostream& stream ) const;
	bool writeJson( std::ostream& stream ) const;
	static bool writeComparisonText( std::ostream& stream, const VsmCostAnalyzer& baseline, const VsmCostAnalyzer& candidate );
	static bool writeComparisonJson( std::ostream& stream, const VsmCostAnalyzer& baseline, const VsmCostAnalyzer& candidate );
	static bool writeComparisonMarkdown( std::ostream& stream, const VsmCostAnalyzer& baseline, const VsmCostAnalyzer& candidate );
	static bool writeComparisonMarkdownHeader( std::ostream& stream );
	static bool writeComparisonMarkdownRow( std::ostream& stream, const VsmCostAnalyzer& baseline, const VsmCostAnalyzer& candidate );

private:
	enum Unit
	{
		UNIT_UNKNOWN,
		UNIT_NOP,
		UNIT_UPPER,
		UNIT_LOWER,
		UNIT_BRANCH,
		UNIT_FDIV,
		UNIT_EFU,
		UNIT_WAITQ,
		UNIT_WAITP
	};

	struct Slot
	{
		Slot();

		bool present;
		bool nop;
		bool eBit;
		bool dBit;
		bool tBit;
		unsigned int latency;
		unsigned int throughput;
		std::string text;
		std::string mnemonic;
		Unit unit;
	};

	struct Block
	{
		Block( const std::string& blockLabel );

		std::string label;
		unsigned int cycles;
		unsigned int upperInstructions;
		unsigned int lowerInstructions;
		unsigned int pairedCycles;
		unsigned int singleUpperCycles;
		unsigned int singleLowerCycles;
		unsigned int nopOnlyCycles;
		unsigned int nopSlots;
		unsigned int issueStallCycles;
		unsigned int fdivIssueStallCycles;
		unsigned int efuIssueStallCycles;
		unsigned int waitqStallCycles;
		unsigned int waitpStallCycles;
		unsigned int operationLatencyCycles;
		unsigned int longLatencyOps;
		unsigned int longLatencyCycles;
	};

	struct WeightedBlock
	{
		std::string label;
		unsigned int cycles;
		unsigned int repeat;
		unsigned int weightedCycles;
		unsigned int pairedCycles;
		unsigned int nopOnlyCycles;
		unsigned int nopSlots;
		unsigned int weightedNopSlots;
		unsigned int weightedNopOnlyCycles;
		unsigned int estimatedCycles;
		unsigned int weightedEstimatedCycles;
		unsigned int waitStallCycles;
		unsigned int weightedWaitStallCycles;
		unsigned int issueStallCycles;
		unsigned int weightedIssueStallCycles;
	};

	struct BlockComparison
	{
		std::string label;
		WeightedBlock baseline;
		WeightedBlock candidate;
		long weightedEstimatedCyclesDelta;
		long weightedNopSlotsDelta;
		long weightedWaitStallCyclesDelta;
	};

	struct AffineCost
	{
		AffineCost();

		unsigned int staticBaseCycles;
		unsigned int staticLoopCycles;
		unsigned int estimatedBaseCycles;
		unsigned int estimatedLoopCycles;
	};

	void reset();
	void startBlock( const std::string& label );
	bool analyzeLine( const std::string& line, unsigned int lineNumber );
	bool parseCycle( const std::string& line, Slot& upper, Slot& lower ) const;
	void recordCycle( const Slot& upper, const Slot& lower );
	unsigned int blockRepeat( const Block& block ) const;
	bool blockIsAffineLoop( const Block& block ) const;
	bool blockIsAffineFallback( const Block& block ) const;
	AffineCost affineCost() const;
	std::vector<WeightedBlock> weightedBlocksByCycles() const;
	std::vector<WeightedBlock> weightedBlocksByEstimatedCycles() const;
	std::vector<WeightedBlock> weightedBlocksByIdleSlots() const;
	std::vector<WeightedBlock> weightedBlocksByWaitStalls() const;
	static unsigned int estimatedBlockCycles( const Block& block );
	static bool weightedBlockGreater( const WeightedBlock& a, const WeightedBlock& b );
	static bool weightedEstimatedBlockGreater( const WeightedBlock& a, const WeightedBlock& b );
	static bool weightedIdleBlockGreater( const WeightedBlock& a, const WeightedBlock& b );
	static bool weightedWaitBlockGreater( const WeightedBlock& a, const WeightedBlock& b );
	static WeightedBlock emptyWeightedBlock( const std::string& label );
	static std::string canonicalBlockLabel( const std::string& label );
	static std::vector<BlockComparison> blockComparisons( const VsmCostAnalyzer& baseline, const VsmCostAnalyzer& candidate );
	static bool estimatedBlockComparisonGreater( const BlockComparison& a, const BlockComparison& b );
	static bool idleBlockComparisonGreater( const BlockComparison& a, const BlockComparison& b );
	static bool waitBlockComparisonGreater( const BlockComparison& a, const BlockComparison& b );
	static void writeJsonLabelCost( std::ostream& stream,
	                                const Block& block,
	                                const std::string& canonicalLabel,
	                                bool affineLoop,
	                                bool affineFallback,
	                                unsigned int repeat );
	static void writeJsonWeightedBlock( std::ostream& stream, const WeightedBlock& block );
	static bool writeComparisonBlocksText( std::ostream& stream, const std::vector<BlockComparison>& comparisons );
	static bool writeComparisonBlocksJson( std::ostream& stream, const std::vector<BlockComparison>& comparisons );

	static std::string stripComment( const std::string& line );
	static std::string trim( const std::string& text );
	static std::string firstWord( const std::string& text );
	static std::string lower( const std::string& text );
	static std::string normalizeMnemonic( const std::string& word );
	static Unit classifyMnemonic( const std::string& mnemonic );
	static unsigned int instructionLatency( const std::string& mnemonic );
	static unsigned int instructionThroughput( const std::string& mnemonic );
	static bool writesQ( const std::string& mnemonic );
	static bool writesP( const std::string& mnemonic );
	static bool isKnownMnemonic( const std::string& mnemonic );
	static bool startsInstructionToken( const std::string& line, std::string::size_type pos );
	static bool isDirective( const std::string& line );
	static bool isLabelOnly( const std::string& line );
	static std::string affineExpression( unsigned int baseCycles, unsigned int loopCycles );
	static std::string jsonEscape( const std::string& text );

	std::string m_inputName;
	std::vector<Block> m_blocks;
	std::map<std::string, unsigned int> m_blockRepeats;
	unsigned int m_currentBlock;

	unsigned int m_staticCycles;
	unsigned int m_upperInstructions;
	unsigned int m_lowerInstructions;
	unsigned int m_pairedCycles;
	unsigned int m_singleUpperCycles;
	unsigned int m_singleLowerCycles;
	unsigned int m_nopOnlyCycles;
	unsigned int m_nopSlots;
	unsigned int m_branchCycles;
	unsigned int m_waitqCycles;
	unsigned int m_waitpCycles;
	unsigned int m_issueStallCycles;
	unsigned int m_fdivIssueStallCycles;
	unsigned int m_efuIssueStallCycles;
	unsigned int m_waitqStallCycles;
	unsigned int m_waitpStallCycles;
	unsigned int m_fdivOps;
	unsigned int m_efuOps;
	unsigned int m_eBitCycles;
	unsigned int m_dBitCycles;
	unsigned int m_tBitCycles;
	unsigned int m_unknownInstructions;
	unsigned int m_slotMismatches;
	unsigned int m_ignoredLines;
	unsigned int m_operationLatencyCycles;
	unsigned int m_longLatencyOps;
	unsigned int m_longLatencyCycles;
	unsigned int m_maxOpLatency;
	unsigned int m_estimatedCycles;
	unsigned int m_qReadyCycle;
	unsigned int m_pReadyCycle;
	unsigned int m_fdivIssueReadyCycle;
	unsigned int m_efuIssueReadyCycle;
};

}

#endif
