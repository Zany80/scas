#include "directives.h"
#include "errors.h"
#include "expression.h"
#include "stringop.h"
#include "objects.h"
#include "list.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#define ERROR(ERROR_CODE, COLUMN) add_error(state->errors, ERROR_CODE, \
		*(int*)stack_peek(state->line_number_stack), \
		state->line, COLUMN, stack_peek(state->file_name_stack));

struct directive {
	char *match;
	int(*function)(struct assembler_state *state, char **argv, int argc);
	int allow_space_delimiter;
};

char *join_args(char **argv, int argc) {
	int len = 0, i;
	for (i = 0; i < argc; ++i) {
		len += strlen(argv[i]) + 1;
	}
	char *res = malloc(len);
	len = 0;
	for (i = 0; i < argc; ++i) {
		strcpy(res + len, argv[i]);
		len += strlen(argv[i]);
		res[len++] = ' ';
	}
	res[len - 1] = '\0';
	return res;
}

int handle_nop(struct assembler_state *state, char **argv, int argc) {
	return 1;
}

int handle_area(struct assembler_state *state, char **argv, int argc) {
	if (argc != 1) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	area_t *area = NULL;
	int i;
	for (i = 0; i < state->object->areas->length; ++i) {
		area_t *a = state->object->areas->items[i];
		if (strcasecmp(a->name, argv[0]) == 0) {
			area = a;
			break;
		}
	}
	if (!area) {
		area = create_area(argv[0]);
		list_add(state->object->areas, area);
	}
	state->current_area = area;
	return 1;
}

int handle_ascii(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int i;
	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (argv[i][0] != '"' || argv[i][len - 1] != '"') {
			ERROR(ERROR_INVALID_DIRECTIVE, state->column);
			return 1;
		}
		argv[i][len - 1] = '\0';
		len -= 2;
		len = unescape_string(argv[i] + 1);
		append_to_area(state->current_area, (unsigned char*)(argv[i] + 1), len);
		state->PC += len;
	}
	return 1;
}

int handle_asciiz(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int i;
	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (argv[i][0] != '"' || argv[i][len - 1] != '"') {
			ERROR(ERROR_INVALID_DIRECTIVE, state->column);
			return 1;
		}
		argv[i][len - 1] = '\0';
		len -= 2;
		len = unescape_string(argv[i] + 1);
		append_to_area(state->current_area, (unsigned char*)(argv[i] + 1), len + 1 /* Includes the null terminator */);
		state->PC += len + 1;
	}
	return 1;
}

int handle_asciip(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int i;
	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (argv[i][0] != '"' || argv[i][len - 1] != '"') {
			ERROR(ERROR_INVALID_DIRECTIVE, state->column);
			return 1;
		}
		argv[i][len - 1] = '\0';
		len -= 2;
		len = unescape_string(argv[i] + 1);
		uint8_t _len = len;
		if (_len != len) {
			/* Would it be obvious that this is because the string is too long? */
			ERROR(ERROR_VALUE_TRUNCATED, state->column);
		}
		append_to_area(state->current_area, &_len, sizeof(uint8_t));
		append_to_area(state->current_area, (unsigned char*)(argv[i] + 1), len);
		state->PC += len + 1;
	}
	return 1;
}

int handle_block(struct assembler_state *state, char **argv, int argc) {
	if (argc != 1) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int error;
	uint64_t result;
	tokenized_expression_t *expression = parse_expression(argv[0]);
	if (expression == NULL) {
		error = EXPRESSION_BAD_SYNTAX;
	} else {
		result = evaluate_expression(expression, state->equates, &error);
	}
	if (error == EXPRESSION_BAD_SYMBOL) {
		ERROR(ERROR_UNKNOWN_SYMBOL, state->column);
	} else if (error == EXPRESSION_BAD_SYNTAX) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
	} else {
		uint8_t *buffer = calloc(256, sizeof(uint8_t));
		while (result) {
			append_to_area(state->current_area, buffer, result > 256 ? 256 : result);
			if (result > 256) {
				result -= 256;
				state->PC += 256;
			} else {
				state->PC += result;
				result = 0;
			}
		}
	}
	return 1;
}

int handle_bndry(struct assembler_state *state, char **argv, int argc) {
	if (argc != 1) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int error;
	uint64_t result;
	tokenized_expression_t *expression = parse_expression(argv[0]);
	if (expression == NULL) {
		error = EXPRESSION_BAD_SYNTAX;
	} else {
		result = evaluate_expression(expression, state->equates, &error);
	}
	if (error == EXPRESSION_BAD_SYMBOL) {
		ERROR(ERROR_UNKNOWN_SYMBOL, state->column);
	} else if (error == EXPRESSION_BAD_SYNTAX) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
	} else {
		if (state->PC % result != 0) {
			uint8_t *buf = calloc(1024, sizeof(uint8_t));
			int len = state->PC % result;
			while (len) {
				append_to_area(state->current_area, buf, len > 256 ? 256 : len);
				if (len > 256) {
					len -= 256;
					state->PC += 256;
				} else {
					state->PC += len;
					len = 0;
				}
			}
		}
	}
	return 1;
}

int handle_db(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int i;
	for (i = 0; i < argc; ++i) {
		int len = strlen(argv[i]);
		if (argv[i][0] == '"' && argv[i][len - 1] == '"') {
			/* TODO: Do we need to do anything fancy wrt encoding? */
			argv[i][len - 1] = '\0';
			len -= 2;
			len = unescape_string(argv[i] + 1);
			append_to_area(state->current_area, (unsigned char*)(argv[i] + 1), len);
			state->PC += len;
		} else {
			int error;
			uint64_t result;
			tokenized_expression_t *expression = parse_expression(argv[i]);

			if (expression == NULL) {
				error = EXPRESSION_BAD_SYNTAX;
			} else {
				result = evaluate_expression(expression, state->equates, &error);
			}

			if (error == EXPRESSION_BAD_SYMBOL) {
				/* TODO: Throw error if using explicit import */
				late_immediate_t *late_imm = malloc(sizeof(late_immediate_t));
				late_imm->address = state->current_area->data_length;
				late_imm->width = 8;
				late_imm->type = IMM_TYPE_ABSOLUTE;
				late_imm->expression = expression;
				list_add(state->current_area->late_immediates, late_imm);
				*state->instruction_buffer = 0;
			} else if (error == EXPRESSION_BAD_SYNTAX) {
				ERROR(ERROR_INVALID_SYNTAX, state->column);
			} else {
				if ((result & 0xFF) != result && ~result >> 8) {
					ERROR(ERROR_VALUE_TRUNCATED, state->column);
				} else {
					*state->instruction_buffer = (uint8_t)result;
				}
			}
			append_to_area(state->current_area, state->instruction_buffer, 1);
			state->PC++;
		}
	}
	return 1;
}

int handle_dw(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int i;
	for (i = 0; i < argc; ++i) {
		int error;
		uint64_t result;
		tokenized_expression_t *expression = parse_expression(argv[i]);

		if (expression == NULL) {
			error = EXPRESSION_BAD_SYNTAX;
		} else {
			result = evaluate_expression(expression, state->equates, &error);
		}

		if (error == EXPRESSION_BAD_SYMBOL) {
			/* TODO: Throw error if using explicit import */
			late_immediate_t *late_imm = malloc(sizeof(late_immediate_t));
			late_imm->address = state->current_area->data_length;
			late_imm->width = 16;
			late_imm->type = IMM_TYPE_ABSOLUTE;
			late_imm->expression = expression;
			list_add(state->current_area->late_immediates, late_imm);
			*state->instruction_buffer = 0;
		} else if (error == EXPRESSION_BAD_SYNTAX) {
			ERROR(ERROR_INVALID_SYNTAX, state->column);
		} else {
			if ((result & 0xFFFF) != result && ~result >> 16) {
				ERROR(ERROR_VALUE_TRUNCATED, state->column);
			} else {
				state->instruction_buffer[1] = (uint8_t)(result >> 8);
				state->instruction_buffer[0] = (uint8_t)(result & 0xFF);
			}
		}
		append_to_area(state->current_area, state->instruction_buffer, 2);
		state->PC += 2;
	}
	return 1;
}

int handle_echo(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int len = strlen(argv[0]);
	if (argv[0][0] != '"' || argv[0][len - 1] != '"') {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	argv[0][len - 1] = '\0';
	len -= 2;
	/* TODO: Handle format string properly */
	puts(argv[0] + 1);
	return 1;
}

int handle_equ(struct assembler_state *state, char **argv, int argc) {
	/* TODO: Rewrite these forms somewhere higher up:
	 * key = value
	 * key .equ value
	 * Also, concat all the args into one string (except for the key)
	 */
	if (argc != 2) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	tokenized_expression_t *expression = parse_expression(argv[1]);
	int error;
	uint64_t result;
	if (expression == NULL) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
		return 1;
	} else {
		result = evaluate_expression(expression, state->equates, &error);
	}
	if (error == EXPRESSION_BAD_SYMBOL) {
		ERROR(ERROR_UNKNOWN_SYMBOL, state->column);
	} else if (error == EXPRESSION_BAD_SYNTAX) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
	} else {
		symbol_t *sym = malloc(sizeof(symbol_t));
		sym->name = malloc(strlen(argv[0]) + 1);
		strcpy(sym->name, argv[0]);
		sym->type = SYMBOL_EQUATE;
		sym->value = result;
		sym->exported = 0;
		list_add(state->equates, sym);
	}
	return 1;
}

int handle_even(struct assembler_state *state, char **argv, int argc) {
	if (argc != 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	if (state->PC % 2 != 0) {
		uint8_t pad = 0;
		append_to_area(state->current_area, &pad, sizeof(uint8_t));
		++state->PC;
	}
	return 1;
}

int handle_incbin(struct assembler_state *state, char **argv, int argc) {
	if (argc != 1) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	/* TODO: Pass runtime settings down to assembler from main */
	int len = strlen(argv[0]);
	if ((argv[0][0] != '"' || argv[0][len - 1] != '"') && (argv[0][0] != '<' || argv[0][len - 1] != '>')) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	argv[0][len - 1] = '\0';
	len -= 2;
	len = unescape_string(argv[0] + 1);
	char *name = malloc(strlen(argv[0] + 1));
	strcpy(name, argv[0] + 1);
	FILE *file = fopen(name, "r");
	if (!file) {
		ERROR(ERROR_BAD_FILE, state->column);
		return 1;
	}
	uint8_t *buf = malloc(1024);
	int l;
	while ((l = fread(buf, sizeof(uint8_t), 1024, file))) {
		append_to_area(state->current_area, buf, l);
		state->PC += l;
	}
	free(buf);
	fclose(file);
	return 1;
}

int handle_include(struct assembler_state *state, char **argv, int argc) {
	if (argc != 1) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	/* TODO: Pass runtime settings down to assembler from main */
	int len = strlen(argv[0]);
	if ((argv[0][0] != '"' || argv[0][len - 1] != '"') && (argv[0][0] != '<' || argv[0][len - 1] != '>')) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	argv[0][len - 1] = '\0';
	len -= 2;
	len = unescape_string(argv[0] + 1);
	char *name = malloc(strlen(argv[0] + 1));
	strcpy(name, argv[0] + 1);
	FILE *file = fopen(name, "r");
	if (!file) {
		ERROR(ERROR_BAD_FILE, state->column);
		return 1;
	}
	stack_push(state->file_stack, file);
	stack_push(state->file_name_stack, name);
	int *ln = malloc(sizeof(int)); *ln = 0;
	stack_push(state->line_number_stack, ln);
	return 1;
}

int handle_list(struct assembler_state *state, char **argv, int argc) {
	if (argc != 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	state->nolist = 0;
	return 1;
}

int handle_nolist(struct assembler_state *state, char **argv, int argc) {
	if (argc != 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	state->nolist = 1;
	return 1;
}

int handle_odd(struct assembler_state *state, char **argv, int argc) {
	if (argc != 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	if (state->PC % 2 != 1) {
		uint8_t pad = 0;
		append_to_area(state->current_area, &pad, sizeof(uint8_t));
		++state->PC;
	}
	return 1;
}

int handle_org(struct assembler_state *state, char **argv, int argc) {
	if (argc == 0) {
		ERROR(ERROR_INVALID_DIRECTIVE, state->column);
		return 1;
	}
	int error;
	uint64_t result;
	char *args = join_args(argv, argc);
	tokenized_expression_t *expression = parse_expression(args);
	free(args);
	if (!expression) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
		return 1;
	} else {
		result = evaluate_expression(expression, state->equates, &error);
	}
	if (error == EXPRESSION_BAD_SYMBOL) {
		ERROR(ERROR_UNKNOWN_SYMBOL, state->column);
	} else if (error == EXPRESSION_BAD_SYNTAX) {
		ERROR(ERROR_INVALID_SYNTAX, state->column);
	} else {
		state->PC = result;
	}
	return 1;
}

/* Keep this alphabetized */
struct directive directives[] = {
	{ "area", handle_area, 1 },
	{ "ascii", handle_ascii, 1 },
	{ "asciip", handle_asciip, 1 },
	{ "asciiz", handle_asciiz, 1 },
	{ "blkb", handle_block, 0 },
	{ "block", handle_block, 0 },
	{ "bndry", handle_bndry, 1 },
	{ "byte", handle_db, 0 },
	{ "db", handle_db, 0 },
	{ "ds", handle_block, 0 },
	{ "dw", handle_dw, 0 },
	{ "echo", handle_echo, 1 },
	{ "equ", handle_equ, 1 },
	{ "equate", handle_equ, 1 },
	{ "even", handle_even, 1 },
	{ "gblequ", handle_equ, 1 }, /* TODO: Allow users to export equates? */
	{ "incbin", handle_incbin, 1 },
	{ "include", handle_include, 1 },
	{ "lclequ", handle_equ, 1 },
	{ "list", handle_list, 1 },
	{ "local", handle_nop, 1 },
	{ "module", handle_nop, 1 },
	{ "nolist", handle_nolist, 1 },
	{ "odd", handle_odd, 1 },
	{ "optsdcc", handle_nop, 1 },
	{ "org", handle_org, 0 },
	{ "rmb", handle_block, 1 },
	{ "rs", handle_block, 1 },
	{ "section", handle_area, 1 },
	{ "strs", handle_ascii, 1 },
};

int directive_compare(const void *_a, const void *_b) {
	const struct directive *a = _a;
	const struct directive *b = _b;
	return strcasecmp(a->match, b->match);
}

static struct directive nop = { "!", handle_nop };
struct directive *find_directive(char *line) {
	if (line[1] == '!') {
		return &nop;
	}
	++line;
	int whitespace = 0;
	while (line[whitespace] && !isspace(line[whitespace++]));
	if (line[whitespace]) {
		--whitespace;
	}
	char b = line[whitespace];
	line[whitespace] = '\0';
	struct directive d = { .match=line };
	struct directive *res = bsearch(&d, directives, sizeof(directives) / sizeof(struct directive), 
			sizeof(struct directive), directive_compare);
	line[whitespace] = b;
	return res;
}

char **split_directive(char *line, int *argc, int allow_space_delimiter) {
	*argc = 0;
	while (isspace(*line) && *line) ++line;
	/*
	 * Directives can be delimited with whitespace OR with commas.
	 * If you use commas at all, commas will be used exclusively.
	 */
	int capacity = 10;
	char **parts = malloc(sizeof(char *) * capacity);
	char *delimiters;
	if (code_strchr(line, ',') == NULL && allow_space_delimiter) {
		delimiters = "\t ";
	} else {
		delimiters = ",";
	}
	int in_string = 0, in_character = 0;
	int i, j, _;
	for (i = 0, j = 0; line[i]; ++i) {
		if (line[i] == '\\') {
			++i;
		} else if (line[i] == '"' && !in_character) {
			in_string = !in_string;
		} else if (line[i] == '\'' && !in_string) {
			in_character = !in_character;
		} else if (!in_character && !in_string) {
			if (strchr(delimiters, line[i]) != NULL) {
				char *item = malloc(i - j + 1);
				strncpy(item, line + j, i - j);
				item[i - j] = '\0';
				item = strip_whitespace(item, &_);
				if (*argc == capacity) {
					capacity *= 2;
					parts = realloc(parts, sizeof(char *) * capacity);
				}
				parts[*argc] = item;
				j = i + 1;
				++*argc;
			}
		}
	}
	char *item = malloc(i - j);
	strncpy(item, line + j, i - j);
	item[i - j] = '\0';
	item = strip_whitespace(item, &_);
	if (*argc == capacity) {
		capacity++;
		parts = realloc(parts, sizeof(char *) * capacity);
	}
	parts[*argc] = item;
	++*argc;
	return parts;
}

int try_handle_directive(struct assembler_state *state, char **line) {
	if (**line == '.' || **line == '#') {
		struct directive *d = find_directive(*line);
		if (d == NULL) {
			ERROR(ERROR_INVALID_DIRECTIVE, state->column);
			return 1;
		}
		int argc;
		char **argv = split_directive(*line + strlen(d->match) + 1, &argc, d->allow_space_delimiter);
		int ret = d->function(state, argv, argc);
		while (argc--) {
			free(argv[argc]);
		}
		free(argv);
		return ret;
	}
	return 0;
}
