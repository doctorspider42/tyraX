/*
 * main.cpp
 *
 * Copyright (C) 2004 Jesper Svennevid, Daniel Collin
 *
 * Licensed under the AFL v2.0. See the file LICENSE included with this
 * distribution for licensing terms.
 *
 */

#include "Parser.h"
#include "Error.h"
#include <stdlib.h>

int main( int argc, char* argv[] )
{
	vcl::Parser parser;

	if( !parser.create( argc, argv ) )
		return EXIT_FAILURE;

	if( !parser.begin() )
		return EXIT_FAILURE;

	while( parser.run() );

	if( !parser.end() )
		return EXIT_FAILURE;

	// The parser may print Error::Display() diagnostics without bubbling
	// failure up through its return values (e.g. an invalid CLIP operand
	// is reported but tokenisation continues).  Treat any reported error
	// as a build failure so users notice instead of acting on empty output.
	if( vcl::Error::HasErrors() )
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
