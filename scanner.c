#include "scanner.h"

#include <ctype.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>

typedef struct
{
    const char *start;
    const char *current;
    int line;
} scanner_t;

static scanner_t scanner;

void
init_scanner(const char *source)
{
    /* TODO don't use LC_ALL_MASK, also freelocale and set the old locale */
    locale_t locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    uselocale(locale);

    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool
isalpha_or_under(char c)
{
    return c == '_' || isalpha((unsigned char)c);
}

static bool
is_at_end(void)
{
    return *scanner.current == '\0';
}

static char
advance(void)
{
    return *scanner.current++;
}

static char
peek(void)
{
    return *scanner.current;
}

static char
peek_next(void)
{
    return is_at_end() ? '\0' : scanner.current[1];
}

static bool
match(char expected)
{
    if (is_at_end())
        return false;
    if (*scanner.current != expected)
        return false;
    scanner.current++;
    return true;
}

static token_t
make_token(token_type type)
{
    return (token_t){
        .type = type,
        .start = scanner.start,
        .length = (int)(scanner.current - scanner.start),
        .line = scanner.line,
    };
}

static token_t
error_token(const char *message)
{
    return (token_t){
        .type = TOKEN_ERROR,
        .start = message,
        .length = (int)strlen(message),
        .line = scanner.line,
    };
}

static void
skip_whitespace(void)
{
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peek_next() == '/')
                    while (peek() != '\n' && !is_at_end())
                        advance();
                else
                    return;
                break;
            default:
                return;
        }
    }
}

static token_type
check_keyword(int start, int length, const char *rest, token_type type)
{
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0)
        return type;
    return TOKEN_IDENTIFIER;
}

static token_type
identifier_type(void)
{
    switch (scanner.start[0]) {
        case 'a':
            return check_keyword(1, 2, "nd", TOKEN_AND);
        case 'c':
            return check_keyword(1, 4, "lass", TOKEN_CLASS);
        case 'e':
            return check_keyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1)
                switch (scanner.start[1]) {
                    case 'a':
                        return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return check_keyword(2, 1, "r", TOKEN_FOR);
                    case 'u':
                        return check_keyword(2, 1, "n", TOKEN_FUN);
                }
            break;
        case 'i':
            return check_keyword(1, 1, "f", TOKEN_IF);
        case 'n':
            return check_keyword(1, 2, "il", TOKEN_NIL);
        case 'o':
            return check_keyword(1, 1, "r", TOKEN_OR);
        case 'p':
            return check_keyword(1, 4, "rint", TOKEN_PRINT);
        case 'r':
            return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            return check_keyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1)
                switch (scanner.start[1]) {
                    case 'h':
                        return check_keyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        return check_keyword(2, 2, "ue", TOKEN_TRUE);
                }
            break;
        case 'v':
            return check_keyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static token_t
identifier(void)
{
    while (isalpha_or_under(peek()) || isdigit(peek()))
        advance();
    return make_token(identifier_type());
}

static token_t
number(void)
{
    while (isdigit((unsigned char)peek()))
        advance();

    if (peek() == '.' && isdigit((unsigned char)peek_next())) {
        advance();
        while (isdigit((unsigned char)peek()))
            advance();
    }
    return make_token(TOKEN_NUMBER);
}

static token_t
string(void)
{
    while (peek() != '"' && !is_at_end()) {
        if (peek() == '\n')
            scanner.line++;
        advance();
    }

    if (is_at_end())
        return error_token("Unterminated string.");

    advance();
    return make_token(TOKEN_STRING);
}

token_t
scan_token(void)
{
    skip_whitespace();
    scanner.start = scanner.current;

    if (is_at_end())
        return make_token(TOKEN_EOF);

    char c = advance();
    if (isalpha_or_under(c))
        return identifier();
    if (isdigit((unsigned char)c))
        return number();
    switch (c) {
        case '(':
            return make_token(TOKEN_LEFT_PAREN);
        case ')':
            return make_token(TOKEN_RIGHT_PAREN);
        case '{':
            return make_token(TOKEN_LEFT_BRACE);
        case '}':
            return make_token(TOKEN_RIGHT_BRACE);
        case ';':
            return make_token(TOKEN_SEMICOLON);
        case ',':
            return make_token(TOKEN_COMMA);
        case '.':
            return make_token(TOKEN_DOT);
        case '-':
            return make_token(TOKEN_MINUS);
        case '+':
            return make_token(TOKEN_PLUS);
        case '/':
            return make_token(TOKEN_SLASH);
        case '*':
            return make_token(TOKEN_STAR);
        case '!':
            return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return make_token(match('=') ? TOKEN_GREATER_EQUAL
                                         : TOKEN_GREATER);
        case '"':
            return string();
    }

    return error_token("Unexpected character.");
}
