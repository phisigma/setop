/*
setop -- apply set operations to several input files and print resulting set to standard output
Copyright (C) 2015 Frank Stähr, GPL v2 or later

This program is free software:
you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You can find a copy of the GNU General Public License at <http://www.gnu.org/licenses/>.
*/


#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <regex>
#include <cstdlib>

#include <boost/program_options.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "BaseTypes.h"
#include "SetCalculator.h"
#include "FormulaParser.h"

/**
\file
\brief Program for parsing input files and streams for string elements and applying several set operations and special queries
\details For details how to use the program start it with `--help` and see help text.
\author Frank Stähr
\date Oct., 1st, 2015
*/

#define PROGRAM_NAME "setop" ///< official name of program
#define PROGRAM_VERSION "0.2" ///< version of setop
/** \brief used when query has negative result (e. g. element not part of input), but program flow has no (other) error */
#define EXIT_QUERY_NEGATIVE 3
static_assert((EXIT_QUERY_NEGATIVE != EXIT_SUCCESS) && (EXIT_QUERY_NEGATIVE != EXIT_FAILURE),
	"Macro EXIT_QUERY_NEGATIVE must not be equal to EXIT_SUCCESS or to EXIT_FAILURE.");

/** \brief all possible commutative set operations */
enum class SetConcat : unsigned char { UNION, INTERSECTION, SYM_DIFFERENCE, FORMULA };
/** \brief types of different return possibilities for program */
enum class SetQuery : unsigned char { RETURN_SET, CARDINALITY, ISEMPTY, SUBSET, SUPERSET, CONTAINS_ELEMENT, SET_EQUALITY };


/**
\brief Parses escape sequences like `\n` and `\t` to “real” characters (e. g. ``.\'\\\"`` gets to ``.'\"``).
\details Escape sequences ``\'``, `\"`, `\?`, `\\`, `\f`, `\n`, `\r`, `\t`, `\v` are supported.
\param escape_seq sequence to unescape
\note There should be an `std` or `boost` library for a simple task like unescaping, but it seems that there isn’t (yet).
	Replace this function in the future if possible.
\throws std::invalid_argument
*/
std::string unescape_sequence(std::string const& escape_seq)
{
	std::map<char, char> const escape_characters =
	{
		{ '\'', '\'' },
		{ '\"', '\"' },
		{ '?', '?' },
		{ '\\', '\\' },
		{ 'f', '\f' },
		{ 'n', '\n' },
		{ 'r', '\r' },
		{ 't', '\t' },
		{ 'v', '\v' }
	};

	std::string result;
	for (auto curr_char = escape_seq.begin(); curr_char != escape_seq.end(); ++curr_char)
	{
		if (*curr_char == '\\')
		{
			if (++curr_char == escape_seq.end())
				throw std::invalid_argument("Parsing failed: Backslash at end of \"" + escape_seq + "\" is invalid.");
			auto second_char = escape_characters.find(*curr_char);
			if (second_char != escape_characters.end())
				result.push_back(second_char->second);
			else
				throw std::invalid_argument(std::string("Parsing failed: ") +
					"Escape sequence \"\\" + *curr_char + "\" in argument \"" + escape_seq + "\" is not supported.");
		}
		else
		{
			result.push_back(*curr_char);
		}
	}
	
	return result;
}


/**
\brief Prints complete warning message to console `std::cerr` including hint, that this is a warning.
\param warning_message warning message without new line at end
*/
inline void print_warning(std::string const& warning_message)
{
	std::cerr << "Warning: " << warning_message << "\n";
}

/**
\brief Prints complete error message to console `std::cerr` including hint, that this is an error.
\param error_message error message without new line at end
\return exit code `EXIT_FAILURE` (normally 1)
*/
inline int print_error(std::string const& error_message)
{
	std::cerr << "Error: " << error_message << "\n";
	return EXIT_FAILURE;
}

/**
\brief Prints complete error message to console `std::cerr` that an option used a faulty parameter.
\param option option with faulty parameter, i. e. `combination` or `output` (without minus signs)
\param parameter faulty parameter (e. g. `intersect` instead of `intersection`)
\return exit code `EXIT_FAILURE` (normally 1)
*/
inline int print_unsupported_parameter_error(std::string const& option, std::string const& parameter)
{
	return print_error("option '--" + option + "' does not support a parameter '" + parameter + "'.");
}

/**
\brief Main function of program: take command line options and arguments and execute output.
\throws std::runtime_error
*/
int execute_setop(int argc, char* argv[])
{
	// needed variables, mainly options and arguments from command line
	InputOptions input_opts;
	bool quiet, verbose, ignore_case;
	std::string formula, inputstream_for_set_comparison, separator_format, element_format;
	element_t element_for_contains_check;
	std::vector<std::string> combine_parameters, input_filenames, setdifference_filenames, output_parameters;


	// PARSE COMMAND LINE

	namespace po = boost::program_options;
	po::options_description visible_options("Options");
	visible_options.add_options()
		("help", "produce this help message and exit")
		("version", "output name and version")

		("ignore-case,C", po::bool_switch(&ignore_case)->default_value(false), "handle input elements case-insensitive")
		("include-empty", po::bool_switch(&input_opts.include_empty_elements)->default_value(false), "don’t ignore empty elements (these can come from empty lines, trimming, etc.)")
		("input-separator,n", po::value(&separator_format), "describe the form of an input separator as regular expression in ECMAScript syntax; "
			"defaults to new line only if --input-element is not present; i. e. don’t forget to include the new line character \\n when you set the input element manually, when desired!")
		("input-element,l", po::value(&element_format), "describe the form of input elements as regular expression in ECMAScript syntax")
		("output-separator,o", po::value(&input_opts.output_separator)->default_value("\\n"), "string for separating output elements; escape sequences are allowed")
		("trim,t", po::value(&input_opts.trim_characters), "trim all given characters at beginning and end of elements (escape sequences allowed)")

		("combine", po::value(&combine_parameters)->multitoken()/*->default_value("union")*/, "define combination operation applied to given input streams; "
			"possible parameters are 'union' (default), 'intersection', 'symmetric-difference', and 'formula <value>'")
		("subtract", po::value(&setdifference_filenames)->multitoken(), "subtract all elements of all given streams from output set")

		("output", po::value(&output_parameters)->multitoken()/*->default_value("set")*/, "whether to output determined set or a certain information of this set instead; "
			"possible parameters are:\n"
			"set (default): stream all output elements\n"
			"count: just output number of (different) elements, don’t list them\n"
			"is-empty: check if resulting set is empty\n"
			"contains <element-string>: check if given element is contained in set\n"
			"equals <input-stream>: check set equality, i. e. check if output corresponds with content of <input-stream>\n"
			"has-subset <input-stream>: check if content of <input-stream> is subset of output set\n"
			"has-superset <input-stream>: check if content of <input-stream> is superset of output set")
		("quiet", po::bool_switch(&quiet)->default_value(false),
			"suppress all output messages in case of special queries (e. g. when check if element is contained in set)");

	po::options_description invisible_options("Invisible options");
	invisible_options.add_options()("inputfile", po::value(&input_filenames)->composing(), "");
	po::options_description deprecated_options("Deprecated options");
	deprecated_options.add_options()
		("union,u", "")
		("intersection,i", "")
		("symmetric-difference,s", "")
		("difference,d", po::value(&setdifference_filenames)->composing(), "")
		("verbose", "")
		("count,#", "")
		("is-empty", "")
		("contains,c", po::value(&element_for_contains_check), "")
		("equal,e", po::value(&inputstream_for_set_comparison), "")
		("subset,b", po::value(&inputstream_for_set_comparison), "")
		("superset,p", po::value(&inputstream_for_set_comparison), "");
	po::options_description all_options("All options");
	all_options.add(invisible_options).add(deprecated_options).add(visible_options);

	po::variables_map opt_map;
	po::positional_options_description arguments;
	arguments.add("inputfile", -1);
	try
	{
		po::parsed_options parsed = po::command_line_parser(argc, argv).options(all_options).positional(arguments).run();
		po::store(parsed, opt_map);
		po::notify(opt_map);
	}
	catch (po::error const& poexc)
	{
		return print_error(std::string("failed to process command line parameters: ") + poexc.what() +
			"\nTry calling the program with option '--help'.");
	}

	if (opt_map.contains("help"))
	{
		std::cout <<
			"Apply set operations like union, intersection, or set difference to input files "
			"and print resulting set (sorted and with unique string elements) to standard output or give answer to special queries like number of elements.\n\n"

			"Usage:\n"
			PROGRAM_NAME " [input-stream]* [-C] [--include-empty] [--input-separator <value>] [--input-element <value>] [--trim <value>] "
			"[--combine (union|intersection|symmetric-difference|formula <value>)] [--subtract [input-stream]*] "
			"[--output (set|count|is-empty|contains <value>|equals <input-stream>|has-subset <input-stream>|has-superset <input-stream>)] "
			"[--output-separator <value>] [--quiet]\n\n"

			<< visible_options

			<< "No input filename or '-' is equal to reading from standard input. When an input stream occurs multiple times in the calling command, "
			"it is read only once and cached.\n\n"

			<< "The sequence of events of " PROGRAM_NAME " is as follows:\n"
			"At first, all input files are parsed and combined according to the --combine option. "
			"After that, all inputs from option --subtract are parsed and removed from result of first step. "
			"Finally, the desired output given from option --output is printed to screen: "
			"the set itself, or its number of elements, or a comparison to another set etc.\n\n"
			
			"The combination type 'formula' needs an additional string mainly consisting of positive integers and combining operators. "
			"The integers 1, 2, etc. represent the first, second, etc. input stream; possible operators are '&' for intersection, "
			"'^' for symmetric difference, '|' for union, and '-' for set difference. At the moment, they are prioritized in that order (i. e. &, ^, |, -), "
			"but this may change in the future. Use brackets for being explicit about the order of evaluation. Example: "
			"--combine formula '(1 | 2) & 3' unites first and second input stream and intersects the result with third input stream.\n\n"

			"By default each line of an input stream is considered to be an element, you can change this by defining regular expressions "
			"within the options --input-separator or --input-element. When using both, the input stream is first split according to the separator "
			"and after that filtered by the desired input element form. "
			"After finding the elements they are finally trimmed according to the argument given with --trim.\n"
			"The option --ignore-case lets you treat Word and WORD equal, only the first occurrence of all input streams is considered. "
			"Note that --ignore-case does not affect the regular expressions used in --input-separator and --input-element.\n\n"
			"When describing strings and characters for the output separator or for the option --trim you can use escape sequences like "  R"(\t, \n, \" and \'. )"
			"But be aware that some of these sequences "  R"((especially \\ and \"))"  " might be interpreted by your shell before passing the string to "
			PROGRAM_NAME ". In that case you have to use "  R"(\\\\ respectively \\\" just for describing a \ or a ". )"
			"You can check your shell’s behavior with\n"
			R"(echo "\\ and \"")"  "\n\n"
			"Special boolean queries (e. g. check if element is contained in set) return exit code EXIT_SUCCESS (= " << std::to_string(EXIT_SUCCESS)
			<< ") when the answer is 'yes' and otherwise (e. g. element not contained in set) an exit code that is guaranteed to be unequal "
			<< "to EXIT_SUCCESS and to EXIT_FAILURE (= " << std::to_string(EXIT_FAILURE) << "). This way, " PROGRAM_NAME " can be used in the shell. "
			<< "With option --quiet a verbose result message is omitted for boolean queries.\n\n"

			"Examples:\n"
			PROGRAM_NAME R"( A.txt --subtract B.txt --output contains ":fooBAR-:" --trim ":-\t" --ignore-case)"
				"\n\t" "case-insensitive check if element 'foobar' is contained in A minus B\n"
			PROGRAM_NAME R"( A.txt - B.txt --combine intersection --input-element "\d+")"
				"\n\t" "output intersection of console, A, and B, where elements are recognized as strings of digits with at least one character; "
				"i. e. elements are non-negative integers\n"
			PROGRAM_NAME " A.txt B.txt --combine symmetric-difference --input-separator [[:space:]-]"
				"\n\t" "find all elements contained in either A or B, not both, where a whitespace"  R"( (i. e. \v \t \n \r \f or space) )"
				"or a minus is interpreted as a separator between elements\n\n"
			
			"For bug reports, use the issue tracker at\n"
			"https://github.com/phisigma/setop.\n";

		return EXIT_SUCCESS;
	}

	for (boost::shared_ptr<po::option_description> const& deprecated_optdesc : deprecated_options.options())
	{
		if (opt_map.contains(deprecated_optdesc->long_name()))
			print_warning("option '--" + deprecated_optdesc->long_name() + "' is deprecated and will be removed in later versions of "
				PROGRAM_NAME + ".");
	}
	
	if (opt_map.contains("version"))
	{
		std::cout << PROGRAM_NAME << " " << PROGRAM_VERSION << std::endl;
		return EXIT_SUCCESS;
	}

	
	// CHECK PLAUSIBILITY OF INPUT OPTIONS AND ARGUMENTS AND HANDLE THESE OPTIONS

	verbose = opt_map.contains("verbose");
	if (quiet & verbose)
	{
		print_warning("the options '--quiet' and '--verbose' must not be combined. Both ignored.");
		quiet = verbose = false;
	}
	
	std::map<std::string, int> inputstream_to_usagecount; // should in general be 1 for all entries
	
	// report and delete redundant entries for set differences
	std::map<std::string, int> setdifference_streams_usagecount;
	for (std::string const& inputstream : setdifference_filenames)
		++setdifference_streams_usagecount[inputstream];
	for (auto const& [setdifference_stream, usage_count] : setdifference_streams_usagecount)
	{
		++inputstream_to_usagecount[setdifference_stream];
		
		if (usage_count > 1)
		{
			print_warning("option '--" + std::string(opt_map.contains("subtract") ? "subtract" : "difference") + "' contains input stream '"
				+ setdifference_stream + "' multiple times. Redundant entries ignored.");
			for (int curr_redundant_entry_index = 1; curr_redundant_entry_index < usage_count; ++curr_redundant_entry_index)
			{
				std::vector<std::string>::const_iterator found_redundant_entry =
					std::ranges::find_last(setdifference_filenames, setdifference_stream).begin(); // remove from back so order stays preserved
				assert(found_redundant_entry != setdifference_filenames.cend() && "Internal error, handling of multiple set difference streams is faulty.");
				setdifference_filenames.erase(found_redundant_entry);
			}
		}
	}
	
	if (opt_map.count("combine") + opt_map.count("union") + opt_map.count("intersection") + opt_map.count("symmetric-difference") > 1)
		return print_error("the options '--combine', '--union', '--intersection', and '--symmetric difference' must not be combined.");
	SetConcat setconcat_type;
	if (opt_map.contains("combine"))
	{
		int number_parameters = combine_parameters.size();
		if (number_parameters < 1)
			return print_error("option '--combine' needs at least one parameter.");
		
		std::map<std::string, SetConcat> concattype_by_parameter =
			{{"union", SetConcat::UNION}, {"intersection", SetConcat::INTERSECTION},
			{"symmetric-difference", SetConcat::SYM_DIFFERENCE}, {"formula", SetConcat::FORMULA}};
		std::string const& combine_type_str = combine_parameters.front();
		std::map<std::string, SetConcat>::const_iterator found_concattype_it = concattype_by_parameter.find(combine_type_str);
		if (found_concattype_it == concattype_by_parameter.end())
			return print_unsupported_parameter_error("combine", combine_type_str);
		else
			setconcat_type = found_concattype_it->second;
		
		int needed_number_parameters = setconcat_type == SetConcat::FORMULA ? 2 : 1;
		if (number_parameters != needed_number_parameters)
		{
			return print_error(number_parameters < needed_number_parameters ?
				"option '--combine' with parameter '" + combine_type_str + "' needs an additional parameter." :
				"option '--combine' with parameter '" + combine_type_str + "' does not need additional parameters. Found unneeded value '" + combine_parameters[2] + "'.");
		}
		if (setconcat_type == SetConcat::FORMULA)
		{
			formula = combine_parameters[1];
			
			// analyze formula for missing input stream numbers and setup input stream usage counts
			std::regex integer_regex("[[:digit:]]+");
			std::map<unsigned long, int> inputstream_number_to_usagecount;
			std::sregex_iterator curr_found_integer(formula.begin(), formula.end(), integer_regex);
			for (; curr_found_integer != std::sregex_iterator() /*== end*/; ++curr_found_integer)
			{
				++inputstream_number_to_usagecount[std::stoul(curr_found_integer->str())];
			}
			
			if (inputstream_number_to_usagecount.begin()->first > 0 && inputstream_number_to_usagecount.rbegin()->first <= input_filenames.size())
			// out-of-range error is handled later (to show exact position in formula), just exclude this case here to avoid error on array access
			{
				for (std::size_t expected_inputstream_index = 0; expected_inputstream_index < input_filenames.size(); ++expected_inputstream_index)
					if (!inputstream_number_to_usagecount.contains(expected_inputstream_index + 1))
						print_warning("the number " + std::to_string(expected_inputstream_index + 1) + " is not used in the formula "
							"and thus the input stream '" + input_filenames[expected_inputstream_index] + "' is ignored.");
			
				for (auto const& [inputstream_number, usagecount] : inputstream_number_to_usagecount)
					inputstream_to_usagecount[input_filenames[inputstream_number - 1]] += usagecount;
			}
		}
	}
	else
	{
		setconcat_type =
			opt_map.contains("intersection") ? SetConcat::INTERSECTION :
			opt_map.contains("symmetric-difference") ? SetConcat::SYM_DIFFERENCE :
			SetConcat::UNION;
	}
	
	if (setconcat_type != SetConcat::FORMULA)
		for (std::string const& input_filename : input_filenames)
			++inputstream_to_usagecount[input_filename];
	if (!inputstream_for_set_comparison.empty())
		++inputstream_to_usagecount[inputstream_for_set_comparison];

	if (opt_map.count("difference") + opt_map.count("subtract") > 1)
		return print_error("the options '--difference' and '--subtract' must not be combined.");

	if (opt_map.count("output") + opt_map.count("count") + opt_map.count("is-empty") + opt_map.count("subset")
		+ opt_map.count("superset") + opt_map.count("contains") + opt_map.count("equal") > 1)
		return print_error("the options '--output', '--count', '--is-empty', '--subset', '--superset', '--contains', and '--equal' must not be combined.");
	SetQuery output_type;
	bool modern_outputoption_used = opt_map.contains("output");
	if (modern_outputoption_used)
	{
		int number_parameters = output_parameters.size();
		if (number_parameters < 1)
			return print_error("option '--output' needs at least one parameter.");
		
		std::map<std::string, SetQuery> outputtype_by_parameter =
			{{"set", SetQuery::RETURN_SET}, {"count", SetQuery::CARDINALITY}, {"is-empty", SetQuery::ISEMPTY}, {"contains", SetQuery::CONTAINS_ELEMENT},
			{"equals", SetQuery::SET_EQUALITY}, {"has-subset", SetQuery::SUBSET}, {"has-superset", SetQuery::SUPERSET}};
		std::string const& output_type_str = output_parameters.front();
		std::map<std::string, SetQuery>::const_iterator found_outputtype_it = outputtype_by_parameter.find(output_type_str);
		if (found_outputtype_it == outputtype_by_parameter.end())
			return print_unsupported_parameter_error("output", output_type_str);
		else
			output_type = found_outputtype_it->second;
		
		int needed_number_parameters = std::set({SetQuery::RETURN_SET, SetQuery::CARDINALITY, SetQuery::ISEMPTY}).contains(output_type) ? 1 : 2;
		if (number_parameters != needed_number_parameters)
		{
			return print_error(number_parameters < needed_number_parameters ?
				"option '--output' with parameter '" + output_type_str + "' needs an additional parameter." :
				"option '--output' with parameter '" + output_type_str + "' does not need additional parameters. Found unneeded value '" + output_parameters[2] + "'.");
		}
		if (number_parameters == 2)
			(output_type == SetQuery::CONTAINS_ELEMENT ? element_for_contains_check : inputstream_for_set_comparison) = output_parameters[1];
	}
	else
	{
		output_type =
			opt_map.contains("count") ? SetQuery::CARDINALITY :
			opt_map.contains("is-empty") ? SetQuery::ISEMPTY :
			opt_map.contains("subset") ? SetQuery::SUBSET :
			opt_map.contains("superset") ? SetQuery::SUPERSET :
			opt_map.contains("contains") ? SetQuery::CONTAINS_ELEMENT :
			opt_map.contains("equal") ? SetQuery::SET_EQUALITY :
			SetQuery::RETURN_SET;
	}

	// parse escape sequences of trim characters and output separator to "real" characters (e. g. ".\'\\" gets ".'\")
	try
	{
		input_opts.trim_characters = unescape_sequence(input_opts.trim_characters);
		input_opts.output_separator = unescape_sequence(input_opts.output_separator);
	}
	catch (std::invalid_argument const& e)
	{
		return print_error(e.what());
	}

	// check if regexes are ok
	// separator "\n" is default when nothing else is given
	if (element_format.empty() && separator_format.empty())
		separator_format = "\\n";
	for (auto [string_to_parse, resulting_regex_ref] : std::vector<std::pair<std::string const&, boost::regex&>>{
		{element_format, input_opts.input_element_regex},
		{separator_format, input_opts.input_separator_regex}})
	{
		boost::regex::flag_type regex_flags = boost::regex_constants::ECMAScript | boost::regex_constants::optimize;
		if (!string_to_parse.empty())
		{
			try
			{
				resulting_regex_ref = boost::regex(string_to_parse, regex_flags);
			}
			catch (boost::regex_error const&)
			{
				return print_error("\"" + string_to_parse + "\" is not a valid regular expression.");
			}
		}
	}

	// handle case-insensitive
	if (ignore_case)
		input_opts.element_comp = std::bind(
			boost::algorithm::ilexicographical_compare<element_t, element_t>, std::placeholders::_1, std::placeholders::_2, std::locale()
		);
	else
		input_opts.element_comp = boost::algorithm::lexicographical_compare<element_t, element_t>;

	// use console as input when no file given
	if (input_filenames.empty())
		input_filenames.push_back("-");
	
	// console must not be used multiple times (= inputstream "-")
	std::vector<std::string> list_of_inputstreams(input_filenames);
	list_of_inputstreams.insert(list_of_inputstreams.cend(), setdifference_filenames.cbegin(), setdifference_filenames.cend());
	if (!inputstream_for_set_comparison.empty())
		list_of_inputstreams.push_back(inputstream_for_set_comparison);
	bool found_console_as_input = false;
	for (std::string const& inputstream : list_of_inputstreams)
		if (inputstream == "-")
		{
			if (found_console_as_input)
				return print_error("reading from standard input via '-' is only allowed once within the calling " PROGRAM_NAME " command.");
			else
				found_console_as_input = true;
		}


	// PROCESS CALCULATIONS IN THREE STEPS

	// STEP 1/3: execute all commutative set combinations (union, intersection, symmetric difference) or formula

	SetCalculator set_calculator(input_opts, inputstream_to_usagecount);
	set_t output_set(input_opts.element_comp);
	if (setconcat_type == SetConcat::FORMULA)
	{
		FormulaParser formula_parser(set_calculator, input_filenames);
		std::string::const_iterator formula_it = formula.cbegin();
		try
		{
			output_set = formula_parser.parseCompleteTerm(formula_it, formula.cend());
		}
		catch (InvalidTermException const& formula_exception)
		{
			return print_error(std::string("invalid formula, ") + formula_exception.what() + "\n" + formula + "\n"
				+ std::string(formula_it - formula.cbegin(), ' ') + "^");
		}
	}
	else
	{
		for (auto curr_fn_it = input_filenames.cbegin(); curr_fn_it != input_filenames.cend(); ++curr_fn_it)
		{
			if (curr_fn_it == input_filenames.cbegin()) // if it is the first input stream
			{
				output_set = set_calculator.inputStreamToSet(*curr_fn_it);
			}
			else
			{
				SetCombineOperation operation = setconcat_type == SetConcat::UNION ? SetCombineOperation::Union :
					setconcat_type == SetConcat::INTERSECTION ? SetCombineOperation::Intersection :
					SetCombineOperation::SymmetricDiff;
				set_calculator.combine(output_set, operation, set_calculator.inputStreamToSet(*curr_fn_it));
			}
		}
	}


	// STEP 2/3: execute all set differences, that is erase all desired elements from current output set

	for (std::string const& filename : setdifference_filenames)
	{
		set_calculator.combine(output_set, SetCombineOperation::SetDifference, set_calculator.inputStreamToSet(filename));
	}


	// STEP 3/3: calculate output depending on set query

	// print success and failure messages from query and return exit code of program
	auto answer_query = [quiet, verbose, modern_outputoption_used](bool success, std::string const& success_msg, std::string const& unsuccess_msg) -> int
	{
		if (modern_outputoption_used)
		{
			if (!quiet)
				std::cout << (success ? success_msg : unsuccess_msg);
			return success ? EXIT_SUCCESS : EXIT_QUERY_NEGATIVE;
		}
		else
		{
			if (success)
			{
				if (verbose)
					std::cout << success_msg;
				return EXIT_SUCCESS;
			}
			else
			{
				if (!quiet)
					std::cout << unsuccess_msg;
				return EXIT_QUERY_NEGATIVE;
			}
		}
	};

	switch (output_type)
	{
	case SetQuery::RETURN_SET:
		for (element_t const& el : output_set)
			std::cout << el << input_opts.output_separator;
		return EXIT_SUCCESS;
	case SetQuery::CARDINALITY:
		std::cout << output_set.size() << "\n";
		return EXIT_SUCCESS;
	case SetQuery::ISEMPTY:
		return answer_query(
			output_set.empty(),
			"Resulting set is empty.\n",
			"Resulting set is not empty.\n");
	case SetQuery::CONTAINS_ELEMENT:
		boost::trim_if(element_for_contains_check, boost::is_any_of(input_opts.trim_characters));
		return answer_query(
			output_set.contains(element_for_contains_check),
			"\"" + element_for_contains_check + "\" is contained in set.\n",
			"Input does not contain element \"" + element_for_contains_check + "\".\n");
	case SetQuery::SET_EQUALITY:
		return answer_query(
			set_calculator.inputStreamToSet(inputstream_for_set_comparison) == output_set,
			"Resulting set is equal to input \"" + inputstream_for_set_comparison + "\".\n",
			"Resulting set is not equal to input \"" + inputstream_for_set_comparison + "\".\n");
	case SetQuery::SUBSET:
		return answer_query(
			std::ranges::includes(output_set, set_calculator.inputStreamToSet(inputstream_for_set_comparison)),
			"\"" + inputstream_for_set_comparison + "\" is a subset.\n",
			"\"" + inputstream_for_set_comparison + "\" is not a subset.\n");
	case SetQuery::SUPERSET:
		return answer_query(
			std::ranges::includes(set_calculator.inputStreamToSet(inputstream_for_set_comparison), output_set),
			"\"" + inputstream_for_set_comparison + "\" is a superset.\n",
			"\"" + inputstream_for_set_comparison + "\" is not a superset.\n");
	default:
		// never happens because all cases are handled above
		return EXIT_FAILURE;
	}
}


/**
\brief Executes execute_setop, but handles all possible general exceptions.
*/
int main(int argc, char* argv[])
{
	int exit_code;
	try
	{
		exit_code = execute_setop(argc, argv);
	}
	// too much complexity or stack overflow in ++regex_iterator
	// input file could not be read
	catch (std::runtime_error const& rt_exc)
	{
		return print_error(rt_exc.what());
	}
	// no more memory for more elements, or one element is too large because of faulty regular expression
	catch (std::bad_alloc const&)
	{
		return print_error("not enough memory available. Input data could be too large, or input element or separator regex could be erroneous.");
	}
#ifndef _DEBUG
	catch (std::exception const& exc)
	{
		return print_error(exc.what());
	}
	catch (...)
	{
		return print_error("unknown error occurred.");
	}
#endif
	
	return exit_code;
}
