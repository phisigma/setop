#pragma once

#include "TermParser.h"
#include "BaseTypes.h"


/**
 * @brief Parses a formula for set combinations and return resulting set.
 * @details Given a sequence of input streams file1, file2, and file3 the formula "1 | 2 & 3" would calculate "set represented by file2"
 *	intersected with "set from file3" and finally merged with file1.
 */
class FormulaParser : public TermParser<set_t>
{
	class SetCalculator& setCalculator;
	std::vector<std::string> const& inputStreamNames;
public:
	FormulaParser(class SetCalculator& setCalculator, std::vector<std::string> const& inputStreamNames);
	void combine(set_t& baseValue, char operation, set_t&& rightValue) const override;
	set_t parseNextLiteral(StringIteratorType& formulaIt, StringIteratorType endIt) const override;
};
