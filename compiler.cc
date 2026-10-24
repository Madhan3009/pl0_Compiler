/*
 * pl0c: a compiler for the PL/0 language.
 *
 * Usage: pl0c file.pl0
 *
 * Currently contains the lexer, which breaks the source file into tokens,
 * and a recursive-descent parser that checks the program's syntax.
 * Each parser function handles one grammar rule, shown in its comment.
 */

#include <complex>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <string>
#include <iostream>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>
#include <array>
#include <charconv>


using namespace std;


/*
 * Token types returned by lex().
 * Each token is a single character so it can be printed directly while
 * debugging. Symbol tokens use the symbol itself (e.g. ';' for semicolon).
 */
#define TOK_IDENT	'I'
#define TOK_NUMBER	'N'
#define TOK_CONST	'C'
#define TOK_VAR		'V'
#define TOK_PROCEDURE	'P'
#define TOK_CALL	'c'
#define TOK_BEGIN	'B'
#define TOK_END		'E'
#define TOK_IF		'i'
#define TOK_THEN	'T'
#define TOK_WHILE	'W'
#define TOK_DO		'D'
#define TOK_ODD		'O'
#define TOK_DOT		'.'
#define TOK_EQUAL	'='
#define TOK_COMMA	','
#define TOK_SEMICOLON	';'
#define TOK_ASSIGN	':'     // the two-character ":=" operator
#define TOK_HASH	'#'     // "not equal" in PL/0
#define TOK_LESSTHAN	'<'
#define TOK_GREATERTHAN	'>'
#define TOK_PLUS	'+'
#define TOK_MINUS	'-'
#define TOK_MULTIPLY	'*'
#define TOK_DIVIDE	'/'
#define TOK_LPAREN	'('
#define TOK_RPAREN	')'
#define TOK_LTEQUAL	'{'     // "<=" ('{' starts a comment, so it's never a real token)
#define TOK_GTEQUAL	'}'     // ">="

static string raw;               // entire source file; must not be modified after readin()
static string_view token_text;   // text of the current identifier/keyword/number (points into raw)
static size_t pos = 0;           // index of the next unread character in raw
static long number_value = 0;    // numeric value of the current TOK_NUMBER
static size_t line = 1;          // current line number, for error messages
static int type;                 // type of the current token (the parser's lookahead)
static int depth = 0;            // how deeply blocks (procedures) are nested

// Return the next character without consuming it ('\0' at end of file).
static char peek(){
    return pos < raw.size() ? raw[pos] : '\0';
}

// Consume and return the next character ('\0' at end of file).
static char next(){
    return pos < raw.size() ? raw[pos++] : '\0';
}

// Print an error with the current line number and exit.
// Accepts any number of printable arguments: error("bad token: ", c);
template <typename... Args>
[[noreturn]] static void error(const Args&... args) {
    std::cerr << "pl0c: error: " << line << ": ";
    (std::cerr << ... << args);   // C++17 fold expression
    std::cerr << '\n';
    std::exit(1);
}

template <typename... Args>
static void aout(const Args&... args){
    (std::cout << ... << args);
}

// Skip a { ... } comment. The opening '{' has already been consumed.
static void comment(){
    char ch;
    while((ch = next()) != '}'){
        if(ch == '\0'){
            error("underterminated comment");
        }
        if(ch == '\n'){
            line++;     // comments can span lines, so keep counting
        }
    }
}

// Lex an identifier or keyword starting at pos.
// Returns the keyword's token type, or TOK_IDENT if it isn't a keyword.
static int ident(){
    size_t start = pos;

    while(isalnum(static_cast<unsigned char>(peek()))){
        next();
    }

    // View of the word inside raw (no copy). Saved before the keyword
    // check because keywords return early from the loop below.
    string_view token(raw.data() + start,pos - start);
    token_text = token;


    // Keyword table, built at compile time.
    static constexpr array<pair<string_view, int>, 11> keywords{{
        {"const",     TOK_CONST},
        {"var",       TOK_VAR},
        {"procedure", TOK_PROCEDURE},
        {"call",      TOK_CALL},
        {"begin",     TOK_BEGIN},
        {"end",       TOK_END},
        {"if",        TOK_IF},
        {"then",      TOK_THEN},
        {"while",     TOK_WHILE},
        {"do",        TOK_DO},
        {"odd",       TOK_ODD},
    }};



    for (const auto& [word, tok] : keywords)
        if (token == word)
            return tok;

    return TOK_IDENT;

}

// Lex a number starting at pos. Stores its value in number_value
// and its text in token_text, then returns TOK_NUMBER.
static int number(){
    size_t start = pos;
    while (isdigit(static_cast<unsigned char>(peek())))
        next();

    string_view token(raw.data() + start, pos - start);
    token_text = token;

    // from_chars converts without throwing; failures come back in ec.
    auto [ptr, ec] = from_chars(token.data(), token.data() + token.size(), number_value);
    if (ec == errc::invalid_argument)
        error("invalid number");
    if (ec == errc::result_out_of_range)
        error("number out of range");

    return TOK_NUMBER;}

// Return the type of the next token in the source, or 0 at end of file.
static int lex(){
again:
    // skip whitespace, counting newlines
    while (isspace(static_cast<unsigned char>(peek()))) {
        if (next() == '\n')
            line++;
    }

    // Identifiers/keywords and numbers have their own lexing functions.
    char c = peek();
    if (isalpha(static_cast<unsigned char>(c)) || c == '_')
        return ident();
    if (isdigit(static_cast<unsigned char>(c)))
        return number();

    switch(next()){
	case '{':
		comment();
		goto again;     // a comment isn't a token, so look for the next one
	// Single-character symbols: the token type is the symbol itself.
	case '.':
	case '=':
	case ',':
	case ';':
	case '#':
	case '+':
	case '-':
	case '*':
	case '/':
	case '(':
	case ')':
        return c;
	// '<' and '>' may be followed by '=' to make "<=" / ">="
	case '<':
		if (peek() == '=') {
			next();
			return TOK_LTEQUAL;
		}
		return TOK_LESSTHAN;
	case '>':
		if (peek() == '=') {
			next();
			return TOK_GTEQUAL;
		}
		return TOK_GREATERTHAN;
	// ':' is only valid as part of ":="
	case ':':
		if (next() != '=')
			error("unknown token: ",c );

		return TOK_ASSIGN;
	case '\0':
		return 0;       // end of file
	default:
		error("unknown token: ", c);
	}

    return 0;
}

// Advance to the next token, storing its type in type.
static void next_token(){
    type = lex();
}

// Require the current token to be of type match, then move past it.
static void expect(int match){
    if(type != match){
        error("syntax error");
    }
    next_token();
}

static void term();   // expression() and factor() call each other, so declare ahead

// expression = [ "+" | "-" ] term { ( "+" | "-" ) term } ;
static void expression(){

    if(type == TOK_PLUS || type == TOK_MINUS){   // optional leading sign
        next_token();
    }
    term();

    while(type == TOK_PLUS || type == TOK_MINUS){
        next_token();
        term();
    }
}

// factor = [ "+" | "-" ] ( ident | number | "(" expression ")" ) ;
// Every expression bottoms out here, so a missing expression is caught here.
static void factor(){
    if(type == TOK_PLUS || type == TOK_MINUS){   // sign inside a term, e.g. a * -1
        next_token();
    }

    switch(type){
        case TOK_IDENT:
        case TOK_NUMBER:
            next_token();
            break;
        case TOK_LPAREN:
            next_token();
            expression();
            expect(TOK_RPAREN);
            break;
        default:
            error("syntax error: expected an expression");
    }

}

// term = factor { ( "*" | "/" ) factor } ;
static void term(){
    factor();

    while(type == TOK_MULTIPLY || type == TOK_DIVIDE){
        next_token();
        factor();
    }
}

// condition = "odd" expression
//           | expression ( "=" | "#" | "<" | "<=" | ">" | ">=" ) expression ;
static void condition(){
    if(type == TOK_ODD){
        expect(TOK_ODD);
        expression();
        return;
    }
    expression();
    switch(type){
         case TOK_EQUAL:
         case TOK_HASH:
         case TOK_LESSTHAN:
         case TOK_LTEQUAL:
         case TOK_GREATERTHAN:
         case TOK_GTEQUAL:
            next_token();          // any comparison operator is fine; consume it
            break;
         default:
            error("invalid conditional");
     }

    expression();
}


// statement = [ ident ":=" expression
//             | "call" ident
//             | "begin" statement { ";" statement } "end"
//             | "if" condition "then" statement
//             | "while" condition "do" statement ] ;
// Any other token means an empty statement, so nothing is consumed.
static void statement(){
    switch(type){
        case TOK_IDENT:
            expect(TOK_IDENT);
            expect(TOK_ASSIGN);
            expression();
            break;
        case TOK_CALL:
            expect(TOK_CALL);
            expect(TOK_IDENT);
            break;
        case TOK_BEGIN:
            expect(TOK_BEGIN);
            statement();
            while(type == TOK_SEMICOLON){
                expect(TOK_SEMICOLON);
                statement();
            }
            expect(TOK_END);
            break;
        case TOK_IF:
            expect(TOK_IF);
            condition();
            expect(TOK_THEN);
            statement();
            break;
        case TOK_WHILE:
            expect(TOK_WHILE);
            condition();
            expect(TOK_DO);
            statement();
            break;
    }
}

static void cg_end(){
    aout("The code is compiled");
}

static void cg_const(){
    aout("const long ", token_text);
}

static void cg_semicolon(){
    aout(";\n");
}

static void cg_symbol(){
    switch(type){
        	case TOK_IDENT:
	case TOK_NUMBER:
		aout(token_text);
		break;
	case TOK_BEGIN:
		aout("{\n");
		break;
	case TOK_END:
		aout(";\n}\n");
		break;
	case TOK_IF:
		aout("if(");
		break;
	case TOK_THEN:
	case TOK_DO:
		aout(")");
		break;
	case TOK_ODD:
		aout("(");
		break;
	case TOK_WHILE:
		aout("while(");
		break;
	case TOK_EQUAL:
		aout("==");
		break;
	case TOK_COMMA:
		aout(",");
		break;
	case TOK_ASSIGN:
		aout("=");
		break;
	case TOK_HASH:
		aout("!=");
		break;
	case TOK_LESSTHAN:
		aout("<");
		break;
	case TOK_GREATERTHAN:
		aout(">");
		break;
	case TOK_PLUS:
		aout("+");
		break;
	case TOK_MINUS:
		aout("-");
		break;
	case TOK_MULTIPLY:
		aout("*");
		break;
	case TOK_DIVIDE:
		aout("/");
		break;
	case TOK_LPAREN:
		aout("(");
		break;
	case TOK_RPAREN:
		aout(")");
	}
}

// block = [ "const" ident "=" number { "," ident "=" number } ";" ]
//         [ "var" ident { "," ident } ";" ]
//         { "procedure" ident ";" block ";" } statement ;
// Declarations must appear in this order.
static void block(){

    if(depth++ > 1){   // the main block plus one level of procedures
        error("nesting depth exceeded");
    }

    if(type == TOK_CONST){
        expect(TOK_CONST);
        if(type == TOK_IDENT){
            cg_const();
        }
        expect(TOK_IDENT);
        expect(TOK_EQUAL);
        expect(TOK_NUMBER);

        while(type == TOK_COMMA){
            expect(TOK_COMMA);
            if(type == TOK_IDENT){
                cg_const();
            }
            expect(TOK_IDENT);
            expect(TOK_EQUAL); 
            
            if(type == TOK_NUMBER){
                cg_symbol();
                cg_semicolon();
            }

            expect(TOK_NUMBER);
        }
        expect(TOK_SEMICOLON);
    }
    if (type == TOK_VAR) {
		expect(TOK_VAR);
		expect(TOK_IDENT);
		while (type == TOK_COMMA) {
			expect(TOK_COMMA);
			expect(TOK_IDENT);
		}
		expect(TOK_SEMICOLON);
	}


    while(type == TOK_PROCEDURE){
        expect(TOK_PROCEDURE);
        expect(TOK_IDENT);
        expect(TOK_SEMICOLON);

        block();       // a procedure's body is itself a block

        expect(TOK_SEMICOLON);
    }

    statement();
    
    if(--depth < 0){
        error("nesting depth underflow");
    }
}

// program = block "." ;
static void parse(){
    next_token();
    block();
    expect(TOK_DOT);

    if(type != 0){     // nothing may follow the final "."
        error("extra tokens after end of program");
    }

    cg_end();
}
    
// Read the whole source file at path into raw.
static void readin(const char *path) {
    if(filesystem::path(path).extension() != ".pl0"){
        error("file must end in .pl0");
    }
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        std::cerr << "couldn't open " << path << "\n";
        std::exit(1);
    }

    auto size = std::filesystem::file_size(path);  // like fstat() in C
    raw.resize(size);                              // like malloc(size)
    file.read(raw.data(), size);                   // like read(fd, raw, size)
}



int main(int argc, char *argv[]){
    if(argc != 2){
        cerr << "Usage :" << argv[0] << "file.pl0" << endl;
        exit(1);
    }

    readin(argv[1]);

    parse();
}

