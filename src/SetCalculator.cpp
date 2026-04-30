#include <iostream>
#include <fstream>
#include <memory>

#include <boost/algorithm/string/trim.hpp>

#include "SetCalculator.h"

SetCalculator::SetCalculator(InputOptions const& inputOptions, std::map<std::string, int> const& inputstreamToUsagecount)
		: inputOptions(inputOptions), inputstreamToUsagecount(inputstreamToUsagecount) {}

void SetCalculator::combine(set_t& baseValue, SetCombineOperation operation, set_t&& rightValue) const
{
	switch (operation)
	{
		case SetCombineOperation::Intersection:
		{
			set_t intersect(inputOptions.element_comp);
			for (element_t const& el : baseValue)
				if (rightValue.contains(el))
					intersect.insert(el);

			baseValue = std::move(intersect);
			break;
		}
		case SetCombineOperation::SymmetricDiff:
			for (element_t const& el : rightValue)
			{
				set_t::const_iterator it = baseValue.find(el);
				if (it != baseValue.end())
					baseValue.erase(it);
				else
					baseValue.insert(el);
			}
			break;
		case SetCombineOperation::Union:
			baseValue.merge(std::move(rightValue));
			break;
		case SetCombineOperation::SetDifference:
			for (element_t const& el : rightValue)
				baseValue.erase(el);
			break;
		default: // can never happen
			assert(false && "Internal error, a set comination type is not covered.");
	}
}

/**
\brief Returns all elements from file as a set.
\param inputStreamName name of input file with elements to parse
*/
set_t SetCalculator::inputStreamToSet(std::string const& inputStreamName)
{
	set_t result(inputOptions.element_comp);
	
	// handle caching of input streams in case they are needed multiple times
	std::map<std::string, int>::iterator usage_count = inputstreamToUsagecount.find(inputStreamName);
	assert(usage_count != inputstreamToUsagecount.end() && usage_count->second > 0
		&& "Internal error on caching input streams, file is not marked for caching");
	--usage_count->second;
	std::map<std::string, set_t>::iterator cached_input_it = inputstreamCache.find(inputStreamName);
	if (cached_input_it != inputstreamCache.end())
	{
		if (usage_count->second == 0)
		{
			result = std::move(cached_input_it->second); // not needed anymore
		}
		else
			result = cached_input_it->second; // copy
		return result;
	}
	
	// set input stream (can be std::cin)
	std::ifstream inputfile;
	if (inputStreamName != "-")
	{
		inputfile.open(inputStreamName);
		if (!inputfile)
			throw std::runtime_error("input file '" + inputStreamName + "' could not be opened.");
	}
	std::istream& inputstream = (inputStreamName == "-" ? std::cin : inputfile);

	// lambda for running adjust_element and inserting it right after (according to options)
	auto adjust_and_insert_element = [&result, this](element_t el_str, bool check_element_regex)
	{
		if (!check_element_regex || inputOptions.input_element_regex.empty() ||
			boost::regex_match(el_str, inputOptions.input_element_regex))
		{
			boost::trim_if(el_str, boost::is_any_of(inputOptions.trim_characters));
			if (!el_str.empty() || inputOptions.include_empty_elements)
				result.insert(std::move(el_str));
		}
	};

	// general idea: parse input according to regular expression describing an element or a separator

	bool use_separator_regex = inputOptions.input_element_regex.empty();
	boost::regex const& regex = (use_separator_regex ? inputOptions.input_separator_regex : inputOptions.input_element_regex);

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

	if (usage_count->second > 0)
		inputstreamCache[inputStreamName] = result; // cache result because it is needed later
	
	return result;
}

SetCalculator::~SetCalculator()
{
	for (auto const& [_, count] : inputstreamToUsagecount)
		assert(count == 0 && "Internal error on caching input streams, file is marked for caching but not needed");
}
