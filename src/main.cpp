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
#include <set>
#include <map>
#include <vector>
#include <fstream>
#include <functional>
#include <memory>
#include <cstdlib>

#include <boost/program_options.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/regex.hpp>


/**
\file
\brief Program for parsing input files and streams for string elements and applying several set operations and special queries
\details For details how to use the program start it with `--help` and see help text.
\author Frank Stähr
\date Oct., 1st, 2015
*/

#define PROGRAM_NAME "setop" ///< official name of program
#define PROGRAM_VERSION "0.1" ///< version of setop
/** \brief used when query has negative result (e. g. element not part of input), but program flow has no (other) error */
#define EXIT_QUERY_NEGATIVE 3
static_assert((EXIT_QUERY_NEGATIVE != EXIT_SUCCESS) && (EXIT_QUERY_NEGATIVE != EXIT_FAILURE),
	"Macro EXIT_QUERY_NEGATIVE must not be equal to EXIT_SUCCESS or to EXIT_FAILURE.");

/** \brief all possible commutative set operations */
enum class SetConcat : unsigned char { UNION, INTERSECTION, SYM_DIFFERENCE };
/** \brief types of different return possibilities for program */
enum class SetQuery : unsigned char { RETURN_SET, CARDINALITY, ISEMPTY, SUBSET, SUPERSET, CONTAINS_ELEMENT, SET_EQUALITY };

using element_t = std::string; ///< basic type of element in sets, must base on character type char
using el_comp_t = std::function<bool(element_t const&, element_t const&)>; ///< type of function for comparing elements
/**
\brief basic type for sets
\details A hash set would be faster, but:
 - even with 1,000,000 elements only about with factor 1.7 or 1.8 (depending on several other influences)
 - no advantage in memory saving
 - output is not sorted (at least one more option necessary for letting user to decide if this is acceptable)
 - much more source code (sorted/unsorted output; overhead for case-insensitive hash function etc.)
*/
using set_t = std::set<element_t, el_comp_t>;

/** \brief Encapsulates all options for reading and parsing input streams. */
class InputOptions
{
public:
	el_comp_t element_comp; ///< comparator for set elements (e. g. case-insensitive comparison)
	bool include_empty_elements; ///< empty input elements are included instead of ignored
	boost::regex input_element_regex; ///< regular expression describing an input element (use `boost` instead of `std` because `match_partial` is needed)
	boost::regex input_separator_regex; ///< regular expression describing an input separator
	std::string output_separator; ///< string elements shall be separated with in output
	std::string trim_characters; ///< list of characters that shall be ignored in element at begin and end
} input_opts;


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
\brief Returns all elements from file as a set.
\param filename name of input file with elements to parse
*/
set_t file_to_set(std::string const& filename)
{
	set_t result(input_opts.element_comp);

	// set input stream (can be std::cin)
	std::ifstream inputfile;
	if (filename != "-")
	{
		inputfile.open(filename);
		if (!inputfile)
			throw std::runtime_error("input file '" + filename + "' could not be opened.");
	}
	std::istream& inputstream = (filename == "-" ? std::cin : inputfile);

	// lambda for running adjust_element and inserting it right after (according to options)
	auto adjust_and_insert_element = [&result](element_t el_str, bool check_element_regex)
	{
		if (!check_element_regex || input_opts.input_element_regex.empty() ||
			boost::regex_match(el_str, input_opts.input_element_regex))
		{
			boost::trim_if(el_str, boost::is_any_of(input_opts.trim_characters));
			if (!el_str.empty() || input_opts.include_empty_elements)
				result.insert(std::move(el_str));
		}
	};

	// general idea: parse input according to regular expression describing an element or a separator

	bool use_separator_regex = input_opts.input_element_regex.empty();
	boost::regex const& regex = (use_separator_regex ? input_opts.input_separator_regex : input_opts.input_element_regex);

	// effect of value of initial buffer size is practically unmeasurable, so just take a nice value of form 2^n,
	// at least it should be much bigger than expected size of elements
	std::size_t buffersize = 4096;
	std::size_t used_buffer = 0;
	// use unique pointer instead of "plain" pointer so that there is no memory leak in case of exception
	std::unique_ptr<char[]> buffer(new char[buffersize]);
	do
	{
		inputstream.read(buffer.get() + used_buffer, buffersize - used_buffer);
		char const* const buffer_end = buffer.get() + used_buffer + inputstream.gcount();
		char const* buffer_handled_until = buffer.get();

		// the whole following thing could be much easier by using a bidirectional input iterator here, but:
		// do not do this because input "file" could be a named pipe, stream or similar (no backwards iterating would be possible!)
		// so you have to manage the buffer (and release parts of it) yourself
		// for a more efficient solution (hopefully in the near future) see <https://svn.boost.org/trac/boost/ticket/11776>
		boost::cregex_iterator curr_match(buffer.get(), buffer_end, regex, boost::match_default | boost::match_partial);
		// add element to set when ...
		while (curr_match != boost::cregex_iterator() &&
			curr_match->begin()->matched && // ... match is a full match and ...
			(!inputstream || // ... when file is at end or ...
			// (see next line) when match does not touch end of buffer (otherwise element could be longer, e. g. partial match)
			!boost::regex_match(curr_match->begin()->first, buffer_end, regex, boost::match_default | boost::match_partial)))
		{
			if (use_separator_regex)
			{
				adjust_and_insert_element(element_t(buffer_handled_until, curr_match->begin()->first), true);
				buffer_handled_until = curr_match->begin()->second;
			}
			else
			{
				adjust_and_insert_element(curr_match->str(), false);
			}
			++curr_match;
		}
		if (!use_separator_regex)
			// the last match is always a partial match except full match touches buffer end (or buffer is empty)
			// so mark begin of last match as new begin of buffer when filling it up in next round of do-while-loop
			buffer_handled_until = (curr_match != boost::cregex_iterator() ? curr_match->begin()->first :
				buffer_end);

		used_buffer = buffer_end - buffer_handled_until;
		if (buffer_handled_until == buffer.get())
		{
			// if current element fills the whole buffer, buffer is too small and thus doubled
			buffersize *= 2;
			std::unique_ptr<char[]> new_buffer(new char[buffersize]);
			std::memmove(new_buffer.get(), buffer_handled_until, used_buffer);
			buffer = std::move(new_buffer);
		}
		else
		{
			// move the rest of new element (buffer_handled_until) to beginning of buffer and mark it as used
			std::memmove(buffer.get(), buffer_handled_until, used_buffer);
		}
	} while (inputstream);

	if (use_separator_regex && used_buffer > 0)
		adjust_and_insert_element(element_t(buffer.get(), used_buffer), true);

	return result;
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
	bool quiet, verbose, ignore_case;
	std::string combination_type, additional_output_parameter, separator_format, element_format;
	std::vector<std::string> input_filenames, setdifference_filenames, output_parameters;


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

		("combine", po::value(&combination_type)/*->default_value("union")*/, "define combination operation applied to given input streams; "
			"possible parameters are 'union' (default), 'intersection', and 'symmetric-difference'")
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
		("contains,c", po::value(&additional_output_parameter), "")
		("equal,e", po::value(&additional_output_parameter), "")
		("subset,b", po::value(&additional_output_parameter), "")
		("superset,p", po::value(&additional_output_parameter), "");
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
			"[--combine union|intersection|symmetric-difference] [--subtract [input-stream]*] "
			"[--output (set|count|is-empty|contains <value>|equals <input-stream>|has-subset <input-stream>|has-superset <input-stream>)] "
			"[--output-separator <value>] [--quiet]\n\n"

			<< visible_options

			<< "No input filename or '-' is equal to reading from standard input.\n\n"

			<< "The sequence of events of " PROGRAM_NAME " is as follows:\n"
			"At first, all input files are parsed and combined according to the --combine option. "
			"After that, all inputs from option --subtract are parsed and removed from result of first step. "
			"Finally, the desired output given from option --output is printed to screen: "
			"the set itself, or its number of elements, or a comparison to another set etc.\n\n"

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
				"or a minus is interpreted as a separator between elements\n";

		return EXIT_SUCCESS;
	}

	for (boost::shared_ptr<po::option_description> const& deprecated_optdesc : deprecated_options.options())
	{
		if (opt_map.contains(deprecated_optdesc->long_name()))
			std::cerr << "Warning: option '--" << deprecated_optdesc->long_name() << "' is deprecated and will be removed in later versions of "
				<< PROGRAM_NAME << ".\n";
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
		std::cerr << "Warning: the options '--quiet' and '--verbose' must not be combined. Both ignored.\n";
		quiet = verbose = false;
	}

	if (opt_map.count("combine") + opt_map.count("union") + opt_map.count("intersection") + opt_map.count("symmetric-difference") > 1)
		return print_error("the options '--combine', '--union', '--intersection', and '--symmetric difference' must not be combined.");
	SetConcat setconcat_type;
	if (opt_map.contains("combine"))
	{
		std::map<std::string, SetConcat> concattype_by_parameter =
			{{"union", SetConcat::UNION}, {"intersection", SetConcat::INTERSECTION}, {"symmetric-difference", SetConcat::SYM_DIFFERENCE}};
		std::map<std::string, SetConcat>::const_iterator found_concattype_it = concattype_by_parameter.find(combination_type);
		if (found_concattype_it == concattype_by_parameter.end())
			return print_unsupported_parameter_error("combine", combination_type);
		else
			setconcat_type = found_concattype_it->second;
	}
	else
	{
		setconcat_type =
			opt_map.contains("intersection") ? SetConcat::INTERSECTION :
			opt_map.contains("symmetric-difference") ? SetConcat::SYM_DIFFERENCE :
			SetConcat::UNION;
	}

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
			additional_output_parameter = output_parameters[1];
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


	// PROCESS CALCULATIONS IN THREE STEPS

	// STEP 1/3: execute all commutative set combinations (union, intersection, symmetric difference)

	set_t output_set(input_opts.element_comp);
	for (auto curr_fn_it = input_filenames.cbegin(); curr_fn_it != input_filenames.cend(); ++curr_fn_it)
	{
		set_t curr_set = file_to_set(*curr_fn_it);

		if (curr_fn_it == input_filenames.cbegin()) // if it is the first input stream
		{
			output_set = std::move(curr_set);
		}
		else
		{
			switch (setconcat_type)
			{
			case SetConcat::UNION:
				output_set.merge(std::move(curr_set));
				break;
			case SetConcat::INTERSECTION:
			{
				set_t intersect(input_opts.element_comp);
				for (element_t const& el : output_set)
					if (curr_set.contains(el))
						intersect.insert(el);

				output_set = std::move(intersect);
				break;
			}
			case SetConcat::SYM_DIFFERENCE:
				for (element_t const& el : curr_set)
				{
					set_t::const_iterator it = output_set.find(el);
					if (it != output_set.end())
						output_set.erase(it);
					else
						output_set.insert(el);
				}
			}
		}
	}


	// STEP 2/3: execute all set differences, that is erase all desired elements from current output set

	for (std::string const& filename : setdifference_filenames)
	{
		set_t curr_diff = file_to_set(filename);
		for (element_t const& el : curr_diff)
			output_set.erase(el);
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
	{
		element_t& element_to_check = additional_output_parameter;
		boost::trim_if(element_to_check, boost::is_any_of(input_opts.trim_characters));
		return answer_query(
			output_set.contains(element_to_check),
			"\"" + element_to_check + "\" is contained in set.\n",
			"Input does not contain element \"" + element_to_check + "\".\n");
	}
	case SetQuery::SET_EQUALITY:
		return answer_query(
			file_to_set(additional_output_parameter) == output_set,
			"Resulting set is equal to input \"" + additional_output_parameter + "\".\n",
			"Resulting set is not equal to input \"" + additional_output_parameter + "\".\n");
	case SetQuery::SUBSET:
		return answer_query(
			std::ranges::includes(output_set, file_to_set(additional_output_parameter)),
			"\"" + additional_output_parameter + "\" is a subset.\n",
			"\"" + additional_output_parameter + "\" is not a subset.\n");
	case SetQuery::SUPERSET:
		return answer_query(
			std::ranges::includes(file_to_set(additional_output_parameter), output_set),
			"\"" + additional_output_parameter + "\" is a superset.\n",
			"\"" + additional_output_parameter + "\" is not a superset.\n");
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
