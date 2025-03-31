#include "compiler.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

typedef struct
{
    token_t current;
    token_t previous;
    bool had_error;
    bool panic_mode;
} parser_t;

typedef enum
{
    PREC_NONE,
    PREC_ASSIGNMENT, /* = */
    PREC_OR,         /* or */
    PREC_AND,        /* and */
    PREC_EQUALITY,   /* == != */
    PREC_COMPARISON, /* < > <= >= */
    PREC_TERM,       /* + - */
    PREC_FACTOR,     /* * / */
    PREC_UNARY,      /* ! - */
    PREC_CALL,       /* . () */
    PREC_PRIMARY
} precedence_t;

typedef void (*parse_fn)(bool can_assign);

typedef struct
{
    parse_fn prefix;
    parse_fn infix;
    precedence_t precedence;
} parse_rule;

typedef struct
{
    token_t name;
    int depth;
    bool is_captured;
} local_t;

typedef struct
{
    uint8_t index;
    bool is_local;
} upvalue_t;

typedef enum
{
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT,
} function_type;

typedef struct compiler_t
{
    struct compiler_t *enclosing;
    obj_function *function;
    function_type type;
    local_t locals[UINT8_COUNT];
    int local_count;
    upvalue_t upvalues[UINT8_COUNT];
    int scope_depth;
} compiler_t;

typedef struct class_compiler_t
{
    struct class_compiler_t *enclosing;
    bool has_superclass;
} class_compiler_t;

static parser_t parser;
static compiler_t *current = NULL;
class_compiler_t *current_class = NULL;

static chunk_t *
current_chunk(void)
{
    return &current->function->chunk;
}

static void
error_at(token_t *token, const char *message)
{
    if (parser.panic_mode)
        return;
    parser.panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (token->type == TOKEN_ERROR)
        ;
    else
        fprintf(stderr, " at '%.*s'", token->length, token->start);

    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void
error(const char *message)
{
    error_at(&parser.previous, message);
}

static void
error_at_current(const char *message)
{
    error_at(&parser.current, message);
}

static void
advance(void)
{
    parser.previous = parser.current;

    for (;;) {
        parser.current = scan_token();
        if (parser.current.type != TOKEN_ERROR)
            break;

        error_at_current(parser.current.start);
    }
}

static void
consume(token_type type, const char *message)
{
    if (parser.current.type == type) {
        advance();
        return;
    }
    error_at_current(message);
}

static bool
check(token_type type)
{
    return parser.current.type == type;
}

static bool
match(token_type type)
{
    if (!check(type))
        return false;
    advance();
    return true;
}

static void
emit_byte(op_code byte)
{
    write_chunk(current_chunk(), byte, parser.previous.line);
}

static void
emit_bytes(op_code byte1, uint8_t byte2)
{
    emit_byte(byte1);
    emit_byte(byte2);
}

static void
emit_loop(int loop_start)
{
    emit_byte(OP_LOOP);

    int offset = current_chunk()->count - loop_start + 2;
    if (offset > UINT16_MAX)
        error("Loop body too large.");

    emit_byte((offset >> 8) & 0xff);
    emit_byte(offset & 0xff);
}

static int
emit_jump(uint8_t instruction)
{
    emit_byte(instruction);
    emit_byte(0xff);
    emit_byte(0xff);
    return current_chunk()->count - 2;
}

static void
emit_return(void)
{
    if (current->type == TYPE_INITIALIZER)
        emit_bytes(OP_GET_LOCAL, 0);
    else
        emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}

static uint8_t
make_constant(value_t value)
{
    int constant = add_constant(current_chunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void
emit_constant(value_t value)
{
    emit_bytes(OP_CONSTANT, make_constant(value));
}

static void
patch_jump(int offset)
{
    int jump = current_chunk()->count - offset - 2;

    if (jump > UINT16_MAX)
        error("Too much code to jump over.");

    current_chunk()->code[offset] = (jump >> 8) & 0xff;
    current_chunk()->code[offset + 1] = jump & 0xff;
}

static void
compiler_init(compiler_t *compiler, function_type type)
{
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->function = newfunction();
    current = compiler;
    if (type != TYPE_SCRIPT)
        current->function->name =
          copy_string(parser.previous.start, parser.previous.length);

    /* what does this do again? */
    local_t *local = &current->locals[current->local_count++];
    local->depth = 0;
    local->is_captured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start = "this";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static obj_function *
compiler_end(void)
{
    emit_return();
    obj_function *function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.had_error)
        disassemble_chunk(current_chunk(),
                          function->name != NULL ? function->name->chars
                                                 : "<script>");
#endif
    current = current->enclosing;
    return function;
}

static void
scope_begin(void)
{
    current->scope_depth++;
}

static void
scope_end(void)
{
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth >
             current->scope_depth) {
        if (current->locals[current->local_count - 1].is_captured)
            emit_byte(OP_CLOSE_UPVALUE);
        else
            emit_byte(OP_POP);
        current->local_count--;
    }
}

static void
expression(void);
static void
statement(void);
static void
declaration(void);
static parse_rule *
get_rule(token_type type);
static void
parse_precedence(precedence_t precedence);

static uint8_t
identifier_constant(token_t *name)
{
    return make_constant(
      obj_val((obj_t *)copy_string(name->start, name->length)));
}

static bool
identifiers_equal(token_t *a, token_t *b)
{
    if (a->length != b->length)
        return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static void
add_local(token_t name)
{
    if (current->local_count == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    local_t *local = &current->locals[current->local_count++];
    local->name = name;
    local->depth = -1;
    local->is_captured = false;
}

static void
variable_declare(void)
{
    if (current->scope_depth == 0)
        return;
    token_t *name = &parser.previous;

    for (int i = current->local_count - 1; i >= 0; i++) {
        local_t *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scope_depth)
            break;
        if (identifiers_equal(name, &local->name))
            error("Already a variable with this name in this scope.");
    }

    add_local(*name);
}

static uint8_t
parse_variable(const char *error_message)
{
    consume(TOKEN_IDENTIFIER, error_message);
    variable_declare();
    if (current->scope_depth > 0)
        return 0;
    return identifier_constant(&parser.previous);
}

static void
mark_initialized(void)
{
    if (current->scope_depth == 0)
        return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void
variable_define(uint8_t global)
{
    if (current->scope_depth > 0) {
        mark_initialized();
        return;
    }
    emit_bytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t
argument_list(void)
{
    uint8_t arg_count = 0;
    if (!check(TOKEN_RIGHT_PAREN))
        do {
            expression();
            if (arg_count == 255)
                error("Can't have more than 255 arguments.");
            arg_count++;
        } while (match(TOKEN_COMMA));
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return arg_count;
}

static void
and_(bool can_assign)
{
    int end_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);
    patch_jump(end_jump);
}

static int
resolve_local(compiler_t *compiler, token_t *name)
{
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        local_t *local = &compiler->locals[i];
        if (identifiers_equal(name, &local->name)) {
            if (local->depth == -1)
                error("Can't read local variable in its own initializer.");
            return i;
        }
    }

    return -1;
}

static int
add_upvalue(compiler_t *compiler, uint8_t index, bool is_local)
{
    int upvalue_count = compiler->function->upvalue_count;

    for (int i = 0; i < upvalue_count; i++) {
        upvalue_t *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->is_local == is_local)
            return i;
    }
    if (upvalue_count == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }
    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalues[upvalue_count].index = index;
    return compiler->function->upvalue_count++;
}

static int
resolve_upvalue(compiler_t *compiler, token_t *name)
{
    if (compiler->enclosing == NULL)
        return -1;
    int local = resolve_local(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue != -1)
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    return -1;
}

static void
binary(bool can_assign)
{
    token_type operator_type = parser.previous.type;
    parse_rule *rule = get_rule(operator_type);
    parse_precedence((precedence_t)(rule->precedence + 1));

    switch (operator_type) {
        case TOKEN_BANG_EQUAL:
            emit_bytes(OP_EQUAL, OP_NOT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emit_byte(OP_EQUAL);
            break;
        case TOKEN_GREATER:
            emit_byte(OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emit_bytes(OP_LESS, OP_NOT);
            break;
        case TOKEN_LESS:
            emit_byte(OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emit_bytes(OP_GREATER, OP_NOT);
            break;
        case TOKEN_PLUS:
            emit_byte(OP_ADD);
            break;
        case TOKEN_MINUS:
            emit_byte(OP_SUBTRACT);
            break;
        case TOKEN_STAR:
            emit_byte(OP_MULTIPLY);
            break;
        case TOKEN_SLASH:
            emit_byte(OP_DIVIDE);
            break;
        default:
            /* unreachable */
            return;
    }
}

static void
call(bool can_assign)
{
    uint8_t arg_count = argument_list();
    emit_bytes(OP_CALL, arg_count);
}

static void
dot(bool can_assign)
{
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(&parser.previous);

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t arg_count = argument_list();
        emit_bytes(OP_INVOKE, name);
        emit_byte(arg_count);
    } else {
        emit_bytes(OP_GET_PROPERTY, name);
    }
}

static void
literal(bool can_assign)
{
    switch (parser.previous.type) {
        case TOKEN_FALSE:
            emit_byte(OP_FALSE);
            break;
        case TOKEN_NIL:
            emit_byte(OP_NIL);
            break;
        case TOKEN_TRUE:
            emit_byte(OP_TRUE);
            break;
        default:
            /* Unreachable. */
            return;
    }
}

static void
grouping(bool can_assign)
{
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void
number(bool can_assign)
{
    double value = strtod(parser.previous.start, NULL);
    emit_constant(number_val(value));
}

static void
or_(bool can_assign)
{
    int else_jump = emit_jump(OP_JUMP_IF_FALSE);
    int end_jump = emit_jump(OP_JUMP);

    patch_jump(else_jump);
    emit_byte(OP_POP);

    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static void
string(bool can_assign)
{
    emit_constant(obj_val((obj_t *)copy_string(parser.previous.start + 1,
                                               parser.previous.length - 2)));
}

static void
named_variable(token_t name, bool can_assign)
{
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    } else {
        arg = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match(TOKEN_EQUAL)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        emit_bytes(get_op, (uint8_t)arg);
    }
}

static void
variable(bool can_assign)
{
    named_variable(parser.previous, can_assign);
}

static token_t
synthetic_token(const char *text)
{
    token_t token;
    token.start = text;
    token.length = (int)strlen(text);
    return token;
}

static void
super_(bool can_assign)
{
    if (current_class == NULL)
        error("Can't use 'super' outside of a class.");
    else if (!current_class->has_superclass)
        error("Can't use 'super' in a class with no superclass.");

    consume(TOKEN_DOT, "Expect '.' after 'super',");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifier_constant(&parser.previous);

    named_variable(synthetic_token("this"), false);
    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t arg_count = argument_list();
        named_variable(synthetic_token("super"), false);
        emit_bytes(OP_SUPER_INVOKE, name);
        emit_byte(arg_count);
    } else {

        named_variable(synthetic_token("super"), false);
        emit_bytes(OP_GET_SUPER, name);
    }
}

static void
this_(bool can_assign)
{
    if (current_class == NULL) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

static void
unary(bool can_assign)
{
    token_type operator_type = parser.previous.type;

    parse_precedence(PREC_UNARY);

    switch (operator_type) {
        case TOKEN_BANG:
            emit_byte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emit_byte(OP_NEGATE);
            break;
        default:
            /* unreachable */
            return;
    }
}

parse_rule rules[] = {
    [TOKEN_LEFT_PAREN] = { grouping, call, PREC_CALL },
    [TOKEN_RIGHT_PAREN] = { NULL, NULL, PREC_NONE },
    [TOKEN_LEFT_BRACE] = { NULL, NULL, PREC_NONE },
    [TOKEN_RIGHT_BRACE] = { NULL, NULL, PREC_NONE },
    [TOKEN_COMMA] = { NULL, NULL, PREC_NONE },
    [TOKEN_DOT] = { NULL, dot, PREC_CALL },
    [TOKEN_MINUS] = { unary, binary, PREC_TERM },
    [TOKEN_PLUS] = { NULL, binary, PREC_TERM },
    [TOKEN_SEMICOLON] = { NULL, NULL, PREC_NONE },
    [TOKEN_SLASH] = { NULL, binary, PREC_FACTOR },
    [TOKEN_STAR] = { NULL, binary, PREC_FACTOR },
    [TOKEN_BANG] = { unary, NULL, PREC_NONE },
    [TOKEN_BANG_EQUAL] = { NULL, binary, PREC_EQUALITY },
    [TOKEN_EQUAL] = { NULL, NULL, PREC_NONE },
    [TOKEN_EQUAL_EQUAL] = { NULL, binary, PREC_EQUALITY },
    [TOKEN_GREATER] = { NULL, binary, PREC_COMPARISON },
    [TOKEN_GREATER_EQUAL] = { NULL, binary, PREC_COMPARISON },
    [TOKEN_LESS] = { NULL, binary, PREC_COMPARISON },
    [TOKEN_LESS_EQUAL] = { NULL, binary, PREC_COMPARISON },
    [TOKEN_IDENTIFIER] = { variable, NULL, PREC_NONE },
    [TOKEN_STRING] = { string, NULL, PREC_NONE },
    [TOKEN_NUMBER] = { number, NULL, PREC_NONE },
    [TOKEN_AND] = { NULL, and_, PREC_NONE },
    [TOKEN_CLASS] = { NULL, NULL, PREC_NONE },
    [TOKEN_ELSE] = { NULL, NULL, PREC_NONE },
    [TOKEN_FALSE] = { literal, NULL, PREC_NONE },
    [TOKEN_FOR] = { NULL, NULL, PREC_NONE },
    [TOKEN_FUN] = { NULL, NULL, PREC_NONE },
    [TOKEN_IF] = { NULL, NULL, PREC_NONE },
    [TOKEN_NIL] = { literal, NULL, PREC_NONE },
    [TOKEN_OR] = { NULL, or_, PREC_NONE },
    [TOKEN_PRINT] = { NULL, NULL, PREC_NONE },
    [TOKEN_RETURN] = { NULL, NULL, PREC_NONE },
    [TOKEN_SUPER] = { super_, NULL, PREC_NONE },
    [TOKEN_THIS] = { this_, NULL, PREC_NONE },
    [TOKEN_TRUE] = { literal, NULL, PREC_NONE },
    [TOKEN_VAR] = { NULL, NULL, PREC_NONE },
    [TOKEN_WHILE] = { NULL, NULL, PREC_NONE },
    [TOKEN_ERROR] = { NULL, NULL, PREC_NONE },
    [TOKEN_EOF] = { NULL, NULL, PREC_NONE },
};

static void
parse_precedence(precedence_t precedence)
{
    advance();
    parse_fn prefix_rule = get_rule(parser.previous.type)->prefix;
    if (prefix_rule == NULL) {
        error("Expect expression.");
        return;
    }

    bool can_assign = precedence <= PREC_ASSIGNMENT;
    prefix_rule(can_assign);

    while (precedence <= get_rule(parser.current.type)->precedence) {
        advance();
        parse_fn infix_rule = get_rule(parser.previous.type)->infix;
        infix_rule(can_assign);
    }

    if (can_assign && match(TOKEN_EQUAL))
        error("Invalid assignment target.");
}

static parse_rule *
get_rule(token_type type)
{
    return &rules[type];
}

static void
expression(void)
{
    parse_precedence(PREC_ASSIGNMENT);
}

static void
block(void)
{
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        declaration();

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void
function(function_type type)
{
    compiler_t compiler;
    compiler_init(&compiler, type);
    scope_begin();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255)
                error_at_current("Can't have more than 255 parameters.");
            uint8_t constant = parse_variable("Expect parameter name.");
            variable_define(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    obj_function *function = compiler_end();
    emit_bytes(OP_CLOSURE, make_constant(obj_val((obj_t *)function)));

    for (int i = 0; i < function->upvalue_count; i++) {
        emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
        emit_byte(compiler.upvalues[i].index);
    }
}

static void
method(void)
{
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifier_constant(&parser.previous);
    function_type type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0)
        type = TYPE_INITIALIZER;
    function(type);
    emit_bytes(OP_METHOD, constant);
}

static void
class_declaration(void)
{
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    token_t classname = parser.previous;
    uint8_t name_constant = identifier_constant(&parser.previous);
    variable_declare();

    emit_bytes(OP_CLASS, name_constant);
    variable_define(name_constant);

    class_compiler_t class_compiler;
    class_compiler.has_superclass = false;
    class_compiler.enclosing = current_class;
    current_class = &class_compiler;

    if (match(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false);
        if (identifiers_equal(&classname, &parser.previous))
            error("A class can't inherit from itself.");
        scope_begin();
        add_local(synthetic_token("super"));
        variable_define(0);
        named_variable(classname, false);
        emit_byte(OP_INHERIT);
        class_compiler.has_superclass = true;
    }

    named_variable(classname, false);
    consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
        method();
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emit_byte(OP_POP);

    if (class_compiler.has_superclass)
        scope_end();

    current_class = current_class->enclosing;
}

static void
fun_declaration(void)
{
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized();
    function(TYPE_FUNCTION);
    variable_define(global);
}

static void
var_declaration(void)
{
    uint8_t global = parse_variable("Expect variable name.");
    if (match(TOKEN_EQUAL))
        expression();
    else
        emit_byte(OP_NIL);
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    variable_define(global);
}

static void
expression_statement(void)
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(OP_POP);
}

static void
for_statement(void)
{
    scope_begin();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TOKEN_VAR))
        var_declaration();
    else if (!match(TOKEN_SEMICOLON))
        expression_statement();

    int loop_start = current_chunk()->count;
    int exit_jump = -1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP);
    }

    if (!match(TOKEN_RIGHT_PAREN)) {
        int body_jump = emit_jump(OP_JUMP);
        int increment_start = current_chunk()->count;
        expression();
        emit_byte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }

    scope_end();
}

static void
if_statement(void)
{
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    int else_jump = emit_jump(OP_JUMP);

    patch_jump(then_jump);
    emit_byte(OP_POP);

    if (match(TOKEN_ELSE))
        statement();
    patch_jump(else_jump);
}

static void
print_statement(void)
{
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(OP_PRINT);
}

static void
return_statement(void)
{
    if (current->type == TYPE_SCRIPT)
        error("Can't return from top-level code.");
    if (match(TOKEN_SEMICOLON)) {
        emit_return();
        return;
    } else if (current->type == TYPE_INITIALIZER)
        error("Can't return a value fro man initializer.");
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emit_byte(OP_RETURN);
}

static void
while_statement(void)
{
    int loop_start = current_chunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void
synchronize(void)
{
    parser.panic_mode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON)
            return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:;
        }
        advance();
    }
}

static void
declaration(void)
{
    if (match(TOKEN_CLASS))
        class_declaration();
    else if (match(TOKEN_FUN))
        fun_declaration();
    else if (match(TOKEN_VAR))
        var_declaration();
    else
        statement();

    if (parser.panic_mode)
        synchronize();
}

static void
statement(void)
{
    if (match(TOKEN_PRINT)) {
        print_statement();
    } else if (match(TOKEN_FOR)) {
        for_statement();
    } else if (match(TOKEN_IF)) {
        if_statement();
    } else if (match(TOKEN_RETURN)) {
        return_statement();
    } else if (match(TOKEN_WHILE)) {
        while_statement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        scope_begin();
        block();
        scope_end();
    } else {
        expression_statement();
    }
}

obj_function *
compile(const char *source)
{
    init_scanner(source);
    compiler_t compiler;
    compiler_init(&compiler, TYPE_SCRIPT);
    parser.had_error = false;
    parser.panic_mode = false;

    advance();
    while (!match(TOKEN_EOF))
        declaration();
    obj_function *function = compiler_end();
    return parser.had_error ? NULL : function;
}

void
mark_compiler_roots(void)
{
    compiler_t *compiler = current;
    while (compiler != NULL) {
        mark_object((obj_t *)compiler->function);
        compiler = compiler->enclosing;
    }
}
