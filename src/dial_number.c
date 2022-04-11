/**
 * @file dial_number.c  Dialing numbers helpers
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"
#include <ctype.h>


int clean_number(char* str)
{
	int i = 0, k = 0;

	/* only clean numeric numbers
	 * In other cases trust the user input
	 */
	while (str[i]) {
		if (isalpha(str[i]) != 0)
			return -1;
		else if (str[i] == '@')
			return -1;
		++i;
	}
	i = 0;

	/* remove (0) which is in some mal-formated numbers
	 * but only if trailed by another character
	 */
	if (str[0] == '+' || (str[0] == '0' && str[1] == '0'))
		while (str[i]) {
			if (str[i] == '('
				&& str[i + 1] == '0'
				&& str[i + 2] == ')'
				&& (str[i + 3] == ' '
					|| (str[i + 3] >= '0'
						&& str[i + 3] <= '9')
					)
				) {
				str[i + 1] = ' ';
				break;
			}
			++i;
		}
	i = 0;
	while (str[i]) {
		/* keep only '+' as first digit.
		 */
		if ((str[i] == '+' && k == 0)
			|| isdigit(str[i]) > 0
		)
			str[k++] = str[i++];
		else
			++i;
	}
	str[k] = '\0';
	return k;
}
