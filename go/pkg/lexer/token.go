package lexer

import "fmt"

type TokenType int

const (
	TOKEN_EOF TokenType = iota
	TOKEN_NEWLINE
	TOKEN_IDENTIFIER
	TOKEN_SIGIL_FFI // @name or @lib:name
	TOKEN_INT
	TOKEN_FLOAT
	TOKEN_STRING
	TOKEN_BINARY_OP // + - * / % < > <= >= == != @ & ^
	TOKEN_LBRACKET  // [
	TOKEN_RBRACKET  // ]
	TOKEN_LBRACE    // {
	TOKEN_RBRACE    // }
	TOKEN_PIPE      // |
	TOKEN_COMMA     // ,
	TOKEN_SET       // set
	TOKEN_RETURN    // return
	TOKEN_METHOD    // method
)

type Token struct {
	Type   TokenType
	Lexeme string
	Line   int
	Column int
}

func (t Token) String() string {
	switch t.Type {
	case TOKEN_EOF:
		return "EOF"
	case TOKEN_NEWLINE:
		return "NEWLINE"
	case TOKEN_IDENTIFIER:
		return fmt.Sprintf("IDENT(%s)", t.Lexeme)
	case TOKEN_SIGIL_FFI:
		return fmt.Sprintf("FFI(%s)", t.Lexeme)
	case TOKEN_INT:
		return fmt.Sprintf("INT(%s)", t.Lexeme)
	case TOKEN_FLOAT:
		return fmt.Sprintf("FLOAT(%s)", t.Lexeme)
	case TOKEN_STRING:
		return fmt.Sprintf("STR(%q)", t.Lexeme)
	case TOKEN_BINARY_OP:
		return fmt.Sprintf("BINOP(%s)", t.Lexeme)
	case TOKEN_LBRACKET:
		return "["
	case TOKEN_RBRACKET:
		return "]"
	case TOKEN_LBRACE:
		return "{"
	case TOKEN_RBRACE:
		return "}"
	case TOKEN_PIPE:
		return "|"
	case TOKEN_COMMA:
		return ","
	case TOKEN_SET:
		return "set"
	case TOKEN_RETURN:
		return "return"
	case TOKEN_METHOD:
		return "method"
	default:
		return fmt.Sprintf("TOKEN(%d, %s)", t.Type, t.Lexeme)
	}
}
