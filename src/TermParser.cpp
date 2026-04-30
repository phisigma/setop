#include <cctype> // for std::isspace
#include <string>

#include "TermParser.h"
#include "BaseTypes.h"

template<class ValueType, IsCharConstIterator StringItType>
bool TermParser<ValueType, StringItType>::gotoNextNonwhitespace(StringItType& stringIt, StringItType endIt) const
{
	while (stringIt != endIt && std::isspace(*stringIt))
		++stringIt;
	return *stringIt;
}

template<class ValueType, IsCharConstIterator StringItType>
TermParser<ValueType, StringItType>::TermParser(std::vector<std::vector<char>> const& operatorsGroupedAscendingByPrecedence)
{
	int precedenceLevel = 0;
	if (operatorsGroupedAscendingByPrecedence.empty())
		throw std::invalid_argument("no possible operators are given to parser");
	for (std::vector<char> const& operatorsAtThisLevel : operatorsGroupedAscendingByPrecedence) {
		if (operatorsAtThisLevel.empty())
			throw std::invalid_argument("no possible operators are given to parser for level " + std::to_string(precedenceLevel));
		for (char operatorChar : operatorsAtThisLevel) {
			if (operatorPrecedences.contains(operatorChar)) {
				throw std::invalid_argument(std::string("operator '") + operatorChar
					+ "' is contained more than once in precedence list for construction of parser");
			}
			operatorPrecedences[operatorChar] = precedenceLevel;
		}
		++precedenceLevel;
	}
}

template<class ValueType, IsCharConstIterator StringItType>
typename TermParser<ValueType, StringItType>::ValueOperationPair TermParser<ValueType, StringItType>::parseNextTermOperationPair(
	StringItType& stringIt, StringItType endIt, bool withinBrackets) const
{
	ValueOperationPair result;
	
	// STEP A: parse next value
	if (!gotoNextNonwhitespace(stringIt, endIt))
		throw InvalidTermException("a term is missing");
	if (*stringIt == '(') {
		// either STEP A-1: can be value in brackets
		++stringIt;
		result.value = parseCompleteTerm(stringIt, endIt, true);
	}
	else
		// or STEP A-2: or "plain value"
		result.value = parseNextLiteral(stringIt, endIt);

	// STEP B: parse character after value
	if (gotoNextNonwhitespace(stringIt, endIt)) {
		// either STEP B-1: return from recursive call when end of subformula within brackets is reached
		if (*stringIt == ')') {
			if (!withinBrackets)
				throw InvalidTermException("found closing bracket without opening bracket");
			++stringIt;
		} else {
			// or STEP B-2: parse operation
			if (operatorPrecedences.find(*stringIt) == operatorPrecedences.end()) {
				std::string readableListOfOperators; // e. g. "+, -, *, /, or ^"
				for (std::map<char, int>::const_iterator operatorIt = operatorPrecedences.cbegin(); operatorIt != operatorPrecedences.cend();
					++operatorIt) {
					readableListOfOperators += operatorIt == operatorPrecedences.cbegin() ? "" : // no separator before first operator
						std::next(operatorIt) == operatorPrecedences.cend() ? ", or " : // separator with "or" before last operator
						", "; // otherwise default separator "comma"
					readableListOfOperators += std::string({operatorIt->first});
				}
				throw InvalidTermException("one of the operators " + readableListOfOperators + " is expected");
			}
			result.operation = *stringIt;
			++stringIt;
		}
	} else if (withinBrackets)
		throw InvalidTermException("a closing bracket is missing");
	
	return result;
}

template<class ValueType, IsCharConstIterator StringItType>
void TermParser<ValueType, StringItType>::combineAllOnSamePrecedenceLevel(StringItType& stringIt, StringItType endIt,
	typename TermParser<ValueType, StringItType>::ValueOperationPair& baseValueOpPair, int consideredPrecedence, bool withinBrackets) const
{
	while (baseValueOpPair.operation) {
		int currentPrecedence = operatorPrecedences.at(*baseValueOpPair.operation);
		if (currentPrecedence > consideredPrecedence)
			combineAllOnSamePrecedenceLevel(stringIt, endIt, baseValueOpPair, consideredPrecedence + 1, withinBrackets);
		else if (currentPrecedence < consideredPrecedence)
			return;
		else { // currentPrecedenceLevel == consideredPrecedenceLevel
			ValueOperationPair nextPair = parseNextTermOperationPair(stringIt, endIt, withinBrackets);
			if (nextPair.operation && operatorPrecedences.at(*nextPair.operation) > consideredPrecedence)
				combineAllOnSamePrecedenceLevel(stringIt, endIt, nextPair, consideredPrecedence + 1, withinBrackets);
			combine(baseValueOpPair.value, *baseValueOpPair.operation, std::move(nextPair.value));
			baseValueOpPair.operation = nextPair.operation;
		}
	}
}

template<class ValueType, IsCharConstIterator StringItType>
ValueType TermParser<ValueType, StringItType>::parseCompleteTerm(StringItType& stringIt, StringItType endIt, bool withinBrackets) const
{
	// a formula contains at least one term, so read it
	ValueOperationPair result = parseNextTermOperationPair(stringIt, endIt, withinBrackets);
	combineAllOnSamePrecedenceLevel(stringIt, endIt, result, 0, withinBrackets);
	return std::move(result.value);
}

template class TermParser<set_t>;
