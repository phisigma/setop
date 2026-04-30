#pragma once

#include <string>
#include <set>
#include <functional>

#include <boost/regex.hpp>


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


enum class SetCombineOperation
{
	Intersection,
	SymmetricDiff,
	Union,
	SetDifference
};

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
};
