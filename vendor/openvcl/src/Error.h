#ifndef __OPENVCL_ERROR_H__
#define __OPENVCL_ERROR_H__

#include "Token.h"

#include <string>

namespace vcl
{

class Error
{
public:

	Error();
	Error( const Error& e );
	Error( const std::string& string, bool warning = false );
	Error( const std::string& string, const Line& line, bool warning = false );
	Error( const std::string& string, const Token& token, bool warning = false );
	Error( const std::string& string, const Token& token, const Token::Argument& argument, bool warning = false );

	std::string toString() const;

	static void SetFilename( const std::string& filename );
	static void Display( const Error& error );

	// True when at least one non-warning Error has been Display()'d since
	// process start.  Used by main() to fail the build on any reported
	// error, instead of silently producing partial / empty output.
	static bool HasErrors();
	static void ResetErrorCount();

	// Silence Display() and stop it counting, for a pass whose failure is a
	// decision rather than a diagnosis.  Parser::allocateRegisters() allocates
	// once speculatively before retrying with --sink-loads-past-branches, and
	// the speculative attempt running out of registers is the normal way that
	// works, not something to print.  Any error that is NOT about running out
	// of registers depends on the input rather than on the allocation, so the
	// unsuppressed retry reports it just the same.
	static void SetSuppressed( bool suppressed );

	// Suppression has to NEST: --split-dead-float-ranges retries around the
	// --sink-loads-past-branches retry, and an outer attempt that restored
	// `false` on the way out would un-suppress the inner one.
	static bool Suppressed();

private:

	enum
	{
		STRING		= 0x01,
		LINE			= 0x02,
		TOKEN			= 0x04,
		ARGUMENT	= 0x08
	};

	unsigned int m_content;

	std::string m_string;
	const Line* m_line;
	const Token* m_token;
	const Token::Argument* m_argument;
	bool m_warning;

	static std::list<Error> ms_errors;
	static unsigned int ms_errorCount;
	static bool ms_suppressed;
};

}

#endif
