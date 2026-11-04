package lexer

import (
	"fmt"
	"strings"
	"unicode"
)

type Lexer struct {
	source []rune
	start  int
	curr   int
	line   int
	col    int
	tokens []Token
}

func NewLexer(input string) *Lexer {
	return &Lexer{
		source: []rune(input),
		start:  0,
		curr:   0,
		line:   1,
		col:    1,
	}
}

func (l *Lexer) isAtEnd() bool {
	return l.curr >= len(l.source)
}

func (l *Lexer) peek() rune {
	if l.isAtEnd() {
		return 0
	}
	return l.source[l.curr]
}

func (l *Lexer) peekNext() rune {
	if l.curr+1 >= len(l.source) {
		return 0
	}
	return l.source[l.curr+1]
}

func (l *Lexer) advance() rune {
	ch := l.source[l.curr]
	l.curr++
	if ch == '\n' {
		l.line++
		l.col = 1
	} else {
		l.col++
	}
	return ch
}

func (l *Lexer) Tokenize() ([]Token, error) {
	rawTokens := make([]Token, 0)

	for !l.isAtEnd() {
		l.start = l.curr
		startLine := l.line
		startCol := l.col

		ch := l.advance()

		switch ch {
		case ' ', '\t', '\r':
			// Skip whitespace
			continue

		case '#':
			// Comment to end of line
			for !l.isAtEnd() && l.peek() != '\n' {
				l.advance()
			}
			continue

		case '\n', ';':
			rawTokens = append(rawTokens, Token{
				Type:   TOKEN_NEWLINE,
				Lexeme: "\n",
				Line:   startLine,
				Column: startCol,
			})

		case '[':
			rawTokens = append(rawTokens, Token{Type: TOKEN_LBRACKET, Lexeme: "[", Line: startLine, Column: startCol})
		case ']':
			rawTokens = append(rawTokens, Token{Type: TOKEN_RBRACKET, Lexeme: "]", Line: startLine, Column: startCol})
		case '{':
			rawTokens = append(rawTokens, Token{Type: TOKEN_LBRACE, Lexeme: "{", Line: startLine, Column: startCol})
		case '}':
			rawTokens = append(rawTokens, Token{Type: TOKEN_RBRACE, Lexeme: "}", Line: startLine, Column: startCol})
		case '|':
			rawTokens = append(rawTokens, Token{Type: TOKEN_PIPE, Lexeme: "|", Line: startLine, Column: startCol})
		case ',':
			rawTokens = append(rawTokens, Token{Type: TOKEN_COMMA, Lexeme: ",", Line: startLine, Column: startCol})

		case '"':
			str, err := l.readString()
			if err != nil {
				return nil, err
			}
			rawTokens = append(rawTokens, Token{
				Type:   TOKEN_STRING,
				Lexeme: str,
				Line:   startLine,
				Column: startCol,
			})

		case '@':
			// @ is exclusively the FFI sigil (@func, @VAR, or @lib:func)
			if isIdentStart(l.peek()) {
				ident := l.readIdentifier()
				lexeme := "@" + ident
				rawTokens = append(rawTokens, Token{
					Type:   TOKEN_SIGIL_FFI,
					Lexeme: lexeme,
					Line:   startLine,
					Column: startCol,
				})
			} else {
				return nil, fmt.Errorf("unexpected '@' at line %d, col %d: '@' is reserved for FFI identifiers", startLine, startCol)
			}

		default:
			if unicode.IsDigit(ch) {
				num, isFloat := l.readNumber(ch)
				tokType := TOKEN_INT
				if isFloat {
					tokType = TOKEN_FLOAT
				}
				rawTokens = append(rawTokens, Token{
					Type:   tokType,
					Lexeme: num,
					Line:   startLine,
					Column: startCol,
				})
			} else if isIdentStart(ch) {
				l.curr--
				l.col--
				ident := l.readIdentifier()
				switch ident {
				case "set":
					rawTokens = append(rawTokens, Token{Type: TOKEN_SET, Lexeme: "set", Line: startLine, Column: startCol})
				case "return":
					rawTokens = append(rawTokens, Token{Type: TOKEN_RETURN, Lexeme: "return", Line: startLine, Column: startCol})
				case "method":
					rawTokens = append(rawTokens, Token{Type: TOKEN_METHOD, Lexeme: "method", Line: startLine, Column: startCol})
				default:
					rawTokens = append(rawTokens, Token{Type: TOKEN_IDENTIFIER, Lexeme: ident, Line: startLine, Column: startCol})
				}
			} else if isBinaryOpChar(ch) {
				op := string(ch)
				if isBinaryOpChar(l.peek()) {
					next := l.advance()
					op += string(next)
				}
				rawTokens = append(rawTokens, Token{
					Type:   TOKEN_BINARY_OP,
					Lexeme: op,
					Line:   startLine,
					Column: startCol,
				})
			} else {
				// Fallback single character binary op
				rawTokens = append(rawTokens, Token{
					Type:   TOKEN_BINARY_OP,
					Lexeme: string(ch),
					Line:   startLine,
					Column: startCol,
				})
			}
		}
	}

	// Filter redundant newlines according to Tcl-Talk syntax:
	// - Discard leading newlines
	// - Discard newlines right after '[' or '{'
	// - Discard newlines right before ']' or '}'
	// - Discard newlines right after ',' or '|'
	// - Discard newlines right before ',' or '|'
	// - Collapse consecutive newlines into one
	filtered := l.filterNewlines(rawTokens)
	filtered = append(filtered, Token{Type: TOKEN_EOF, Lexeme: "", Line: l.line, Column: l.col})

	l.tokens = filtered
	return filtered, nil
}

func (l *Lexer) filterNewlines(tokens []Token) []Token {
	res := make([]Token, 0, len(tokens))

	for i := 0; i < len(tokens); i++ {
		t := tokens[i]
		if t.Type != TOKEN_NEWLINE {
			res = append(res, t)
			continue
		}

		// Discard newline at the beginning of the file
		if len(res) == 0 {
			continue
		}

		prev := res[len(res)-1]
		// Discard newline right after '[', '{', ',', '|', or another NEWLINE
		if prev.Type == TOKEN_LBRACKET || prev.Type == TOKEN_LBRACE ||
			prev.Type == TOKEN_COMMA || prev.Type == TOKEN_PIPE ||
			prev.Type == TOKEN_NEWLINE {
			continue
		}

		// Look ahead to next non-whitespace token
		var next Token
		hasNext := false
		for j := i + 1; j < len(tokens); j++ {
			if tokens[j].Type != TOKEN_NEWLINE {
				next = tokens[j]
				hasNext = true
				break
			}
		}

		if hasNext {
			// Discard newline right before ']', '}', ',', '|'
			if next.Type == TOKEN_RBRACKET || next.Type == TOKEN_RBRACE ||
				next.Type == TOKEN_COMMA || next.Type == TOKEN_PIPE {
				continue
			}
		}

		res = append(res, t)
	}

	// Discard trailing newline if any
	if len(res) > 0 && res[len(res)-1].Type == TOKEN_NEWLINE {
		res = res[:len(res)-1]
	}

	return res
}

func isIdentStart(ch rune) bool {
	return unicode.IsLetter(ch) || ch == '_'
}

func isIdentPart(ch rune) bool {
	return unicode.IsLetter(ch) || unicode.IsDigit(ch) || ch == '_' || ch == ':'
}

func isBinaryOpChar(ch rune) bool {
	return strings.ContainsRune("+-*/%<>=!&^", ch)
}

func (l *Lexer) readIdentifier() string {
	start := l.curr
	for !l.isAtEnd() && isIdentPart(l.peek()) {
		l.advance()
	}
	return string(l.source[start:l.curr])
}

func (l *Lexer) readNumber(first rune) (string, bool) {
	num := []rune{first}
	isFloat := false

	for !l.isAtEnd() {
		ch := l.peek()
		if unicode.IsDigit(ch) {
			num = append(num, l.advance())
		} else if ch == '.' && !isFloat && unicode.IsDigit(l.peekNext()) {
			isFloat = true
			num = append(num, l.advance())
		} else {
			break
		}
	}
	return string(num), isFloat
}

func (l *Lexer) readString() (string, error) {
	var sb strings.Builder
	for !l.isAtEnd() {
		ch := l.advance()
		if ch == '"' {
			return sb.String(), nil
		}
		if ch == '\\' {
			if l.isAtEnd() {
				break
			}
			esc := l.advance()
			switch esc {
			case 'n':
				sb.WriteRune('\n')
			case 't':
				sb.WriteRune('\t')
			case 'r':
				sb.WriteRune('\r')
			case '"':
				sb.WriteRune('"')
			case '\\':
				sb.WriteRune('\\')
			case '$':
				sb.WriteRune('$')
			case '[':
				sb.WriteRune('[')
			case ']':
				sb.WriteRune(']')
			default:
				sb.WriteRune('\\')
				sb.WriteRune(esc)
			}
		} else {
			sb.WriteRune(ch)
		}
	}
	return sb.String(), nil
}
