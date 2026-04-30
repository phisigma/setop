#pragma once

#include <concepts>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>


/**
 * @brief Indicates error while parsing a formula-like string with class TermParser.
 * @details Is thrown due to missing brackets, faulty literals, empty formula etc. See `what()` for more details.
 */
class InvalidTermException : public std::invalid_argument
{
public:
	InvalidTermException(std::string const& error_message)
		: invalid_argument(error_message) {}
};


template <class CharConstIt>
concept IsCharConstIterator = requires (CharConstIt it) {
	{ *it } -> std::same_as<char const&>;
	{ ++it } -> std::same_as<CharConstIt&>;
};


/**
 * @brief Generic class for parsing a formula-like string and evaluating result. Operator precedences and brackets are considered.
 * @details Terms like "1 + 3 * 2^(8-6)" can be handled and would result the correct value 13.
 *	For usage, derive from this class, overload abstract methods `parseNextLiteral` and `combine`, and reuse constructor
 *	by
 *	@code
 *	using TermParser<YourValueType>::TermParser;
 *	@endcode
 *	After that, you can initialize the parser e. g. via
 *	@code
 *	MyParser({{'+', '-'}, {'*', '/'}, {'^'}})
 *	@endcode
 *	Operators must only consist of one character, i. e. operators like `<<` are not possible.
 * @tparam ValueType e. g. `float` for typical calculator
 * @tparam StringItType Strings to parse are given by starting string position, e. g. `char const*`.
 */
template<class ValueType, IsCharConstIterator StringItType = std::string::const_iterator>
class TermParser
{
public:
	using StringIteratorType = StringItType;
	
	/**
	 * @brief Standard constructor of TermParser with initialization of all allowed operators and their precedences.
	 * @details Include
	 *	@code
	 *	using TermParser<YourValueType>::TermParser;
	 *	@endcode
	 *	in your class derived from TermParser to ensure correct initialization.
	 * @param operatorsGroupedAscendingByPrecedence list of all possible operators; example: `{{'+', '-'}, {'*', '/'}, {'^'}}` would
	 *	be used to indicate exponentiation (^) over multiplication/division (* and /) over addition and subtraction (+ and -)
	 * @exception std::invalid_argument `operatorsGroupedAscendingByPrecedence` must not be empty, contain empty groups, or contain
	 *	the same operator multiple times.
	 */
	TermParser(std::vector<std::vector<char>> const& operatorsGroupedAscendingByPrecedence);

	/**
	 * @brief Move `stringIt` forward so it shows to non-whitespace (= tab, space, new line).
	 * @return `true` if next character is valid, `false` if string is at end (= null character)
	 */
	virtual bool gotoNextNonwhitespace(StringItType& stringIt, StringItType endIt) const final;
	
	/**
	 * @brief Calculate result from formula.
	 * @param stringIt iterator of formula where evaluation shall start, is moved forward to next unread character
	 * @param endIt end iterator of formula
	 * @param withinBrackets for recursively evaluating "subformulas" within brackets;
	 *	when true, stops directly after closing bracket and returns
	 * @exception InvalidTermException
	 * @return value of evaluated formula
	 */
	virtual ValueType parseCompleteTerm(StringItType& stringIt, StringItType endIt, bool withinBrackets = false) const final;

protected:
	/**
	 * @brief Read next atomic literal from input.
	 * @exception InvalidTermException An error should trigger this exception in case the value could not be parsed. In that case
	 *	the string iterator should stay unchanged. You may expect a non-whitespace character, i. e. you do not need to implement
	 *	extra error handling for end-of-string, brackets etc.
	 * @param stringIt iterator of formula where evaluation shall start, is moved forward to next unread character
	 * @param endIt end iterator of formula
	 * @return E. g. "3 + 4" returns 3 and `stringIt` is moved to character '+' (or space before '+').
	 */
	virtual ValueType parseNextLiteral(StringItType& stringIt, StringItType endIt) const = 0;
	
	/**
	 * @brief Apply operation between two input values.
	 * @param baseValue first operand, is altered and set to result of operation
	 * @param operation character representing operation
	 * @param rightValue second operand
	 */
	virtual void combine(ValueType& baseValue, char operation, ValueType&& rightValue) const = 0;

private:
	class ValueOperationPair
	{
	public:
		ValueType value;
		std::optional<char> operation;
	};

	std::map<char, int> operatorPrecedences;

	/**
	 * @brief Calculate next smallest complete value represented by "plain" literal or by term in brackets plus a possible operation.
	 * @param stringIt iterator of formula where evaluation shall start, is moved forward to next unread character
	 * @param endIt
	 * @param withinBrackets
	 * @exception InvalidTermException A term is always expected, i. e. string must start with value or opening bracket and
		an empty string triggers an exception.
	 * @return E. g. "3 + 4" returns (3, +) and `stringIt` is moved to character '+', whereas "(3 + 4)" returns (7, none) and
		sets `stringIt` to end of string.
	 */
	ValueOperationPair parseNextTermOperationPair(StringItType& stringIt, StringItType endIt, bool withinBrackets) const;
	
	/**
	 * @brief Parse and evaluate term on given precedence level, if needed by recursive calls
	 * @example TermParser.h
	 *	Given precedences ``{{'+', '-'}, {'*', '/'}, {'^'}}``, input string "7-5*2^3*(-1)+13", and `consideredPrecedence`
	 *	of 1 (= level "multiplication/division"), the term is evaluated until '+' character, i. e. `baseValueOpPair` is combined
	 *	with 5*2^3*(-1) = -40, and `stringIt` is set to '+'.
	 * @param stringIt
	 * @param endIt
	 * @param baseValueOpPair initial value, first pair of value and operation
	 * @param consideredPrecedence Calling with value 0 parses complete formula, as this is the lowest precendence level.
	 * @param withinBrackets
	 */
	void combineAllOnSamePrecedenceLevel(StringItType& stringIt, StringItType endIt, ValueOperationPair& baseValueOpPair, int consideredPrecedence,
		bool withinBrackets) const;
};
