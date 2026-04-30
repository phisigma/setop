#include "FormulaParser.h"
#include "SetCalculator.h"

std::map<char, SetCombineOperation> const combineOperationByOperator = {
	{'&', SetCombineOperation::Intersection},
	{'^', SetCombineOperation::SymmetricDiff},
	{'|', SetCombineOperation::Union},
	{'-', SetCombineOperation::SetDifference}
};

FormulaParser::FormulaParser(class SetCalculator& setCalculator, std::vector<std::string> const& inputStreamNames)
	: TermParser<set_t>({{'-'}, {'|'}, {'^'}, {'&'}}), setCalculator(setCalculator), inputStreamNames(inputStreamNames)
{}

set_t FormulaParser::parseNextLiteral(FormulaParser::StringIteratorType& formulaIt, StringIteratorType endIt) const
{
	set_t result;
	{
		std::string::size_type numberReadChars;
		unsigned long inputstreamIndex;
		try
		{
			inputstreamIndex = std::stoul(&*formulaIt, &numberReadChars); // throws std::invalid_argument or std::out_of_range
		}
		catch (std::logic_error const&)
		{
			throw InvalidTermException("a positive integer or opening bracket is expected");
		}
		if (inputstreamIndex < 1 || inputstreamIndex > inputStreamNames.size())
			throw InvalidTermException("input stream indices should be in the range from 1 to " + std::to_string(inputStreamNames.size()));
		result = setCalculator.inputStreamToSet(inputStreamNames[inputstreamIndex - 1]);
		formulaIt += numberReadChars;
	}
	return result;
}

void FormulaParser::combine(set_t& baseValue, char operation, set_t&& rightValue) const
{
	setCalculator.combine(baseValue, combineOperationByOperator.at(operation), std::move(rightValue));
}
