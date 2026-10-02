#ifndef __OPENVCL_PARSER_H__
#define __OPENVCL_PARSER_H__

/*
 * Parser.h
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "Line.h"
#include "CommandLine.h"
#include "Tokenizer.h"
#include "Operand.h"
#include "CodeGenerator.h"
#include "RegisterAllocator.h"

#include <string>
#include <list>
#include <istream>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace vcl
{

class Parser
{
public:

	enum State
	{
		INVALID_STATE,
		SHOW_USAGE,
		SHOW_VERSION,

		READ_INPUT,
		ANALYZE_VSM_COST,
		ANALYZE_VSM_COST_COMPARE,
		ANALYZE_VSM_COST_COMPARE_LIST,
		DUMP_INSTRUCTION_INFO,
		DUMP_LOOP_PIPELINE_INFO,
		DUMP_SCHEDULE_INFO,
		PREPROCESS,
		TOKENIZE,
		ALLOCATE_REGISTERS,
		GENERATE_CODE,

		WRITE_OUTPUT,

		EXIT
	};

	enum PreParser
	{
		DISABLED,
		GASP,
		CPP
	};

	Parser();
	~Parser();

	bool create( int argc, char* argv[] );

	bool begin();
	bool run();
	bool end();

private:

	enum
	{
		TEMPFILE_ATTEMPTS = 10
	};

	void setupOperands();
	Operand* getOperand( const char* name, unsigned int flags );

	void setState( State state );

	bool showVersion();
	bool showUsage();
	bool analyzeVsmCost();
	bool analyzeVsmCostCompare();
	bool analyzeVsmCostCompareList();
	bool dumpInstructionInfo();
	bool dumpLoopPipelineInfo();
	bool dumpScheduleInfo();
	bool readInput();
	bool preProcess();
	bool tokenize();
	bool allocateRegisters();
	bool allocateRegistersAttempt();
	bool generateCode();
	bool writeOutput();

	// The body of generateCode(), against a caller-supplied generator instead of
	// m_codeGenerator, so --sink-loads-best-of can emit its second arm into one of
	// its own without disturbing the first.
	bool generateCodeInto( CodeGenerator& generator, const std::string& name,
	                       const std::list<Token>& tokens );

	// --sink-loads-best-of.  Compile the whole program a second time with load
	// sinking off and keep that output instead when it is the smaller one.
	void tryUnsunkArm();

	bool readInputStream( std::istream& stream );
	bool writeOutputStream( std::ostream& stream );

	std::string tempFilename();

	State m_state;
	CommandLine m_cmdLine;
	Tokenizer m_tokenizer;
	CodeGenerator m_codeGenerator;
	RegisterAllocator m_registerAllocator;

	PreParser m_preParser;

	unsigned int m_tempCounter;

	std::list< std::string > m_tempFiles;

	std::list<Line> m_lines;
	std::list<Operand> m_operands;

	std::map<std::string,File> m_files;

	std::string m_inputFile;
	std::string m_sourceFile;

	// --sink-loads-best-of.  When the unsunk arm wins, its finished text is parked
	// here and written in place of m_codeGenerator's.  The winning generator is not
	// kept alive instead, so that m_codeGenerator - the arm that runs first, and the
	// only one that is the compiler's ordinary output - is never touched by the flag.
	bool m_haveUnsunkOutput;
	std::string m_unsunkOutput;
};

}

#endif
