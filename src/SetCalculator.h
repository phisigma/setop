#pragma once

#include <map>
#include <string>

#include "BaseTypes.h"

/**
 * @brief Handles reading input streams to representing sets and combining sets via intersection, union etc.
 */
class SetCalculator
{
	InputOptions const& inputOptions;
	std::map<std::string, set_t> inputstreamCache; // in case an input is needed more than once, do not parse input file again but cache it
	std::map<std::string, int> inputstreamToUsagecount;

public:
	SetCalculator(InputOptions const& inputOptions, std::map<std::string, int> const& inputstreamToUsagecount);

	void combine(set_t& baseValue, SetCombineOperation operation, set_t&& rightValue) const;
	
	set_t inputStreamToSet(std::string const& inputStreamName);
	
	~SetCalculator();
};
