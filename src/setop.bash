# setop(1) completion                                      -*- shell-script -*-

_setop() 
{
    local current previous beforeprevious options combinationtypes outputtypes
	_init_completion -s || return

	current="${COMP_WORDS[COMP_CWORD]}"
	previous="${COMP_WORDS[COMP_CWORD-1]}"
	beforeprevious="${COMP_WORDS[COMP_CWORD-2]}"
	options="--help --version --quiet --combine --subtract --output --ignore-case --include-empty --input-separator --input-element --trim --output-separator"
	combinationtypes="union intersection symmetric-difference"
	outputtypes="set count is-empty contains equals has-subset has-superset"

	if [[ ${current} == -* ]] ; then
		COMPREPLY=( $(compgen -W "${options}" -- ${current}) )
		return 0
	fi
	
	if [[ "${beforeprevious}" == --output ]] && [[ "${previous}" == contains ]] ; then
		COMPREPLY=()
		return 0
	fi
	
	case "${previous}" in
		--combine)
			COMPREPLY=( $(compgen -W "${combinationtypes}" -- ${current}) )
			;;
		--output)
			COMPREPLY=( $(compgen -W "${outputtypes}" -- ${current}) )
			;;
		-n|--input-separator|-l|--input-element|-o|--output-separator|-t|--trim)
			COMPREPLY=()
			;;
		*)
			_filedir
			;;
	esac
} &&
	complete -F _setop setop

# ex: filetype=sh
