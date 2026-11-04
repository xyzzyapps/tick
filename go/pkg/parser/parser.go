package parser

import (
	"fmt"
	"strconv"
	"tcltalk/pkg/ast"
	"tcltalk/pkg/lexer"
)

type Parser struct {
	tokens []lexer.Token
	curr   int
}

func NewParser(tokens []lexer.Token) *Parser {
	return &Parser{tokens: tokens, curr: 0}
}

func (p *Parser) isAtEnd() bool {
	return p.curr >= len(p.tokens) || p.peek().Type == lexer.TOKEN_EOF
}

func (p *Parser) peek() lexer.Token {
	if p.curr >= len(p.tokens) {
		return lexer.Token{Type: lexer.TOKEN_EOF}
	}
	return p.tokens[p.curr]
}

func (p *Parser) peekNext() lexer.Token {
	if p.curr+1 >= len(p.tokens) {
		return lexer.Token{Type: lexer.TOKEN_EOF}
	}
	return p.tokens[p.curr+1]
}

func (p *Parser) advance() lexer.Token {
	tok := p.peek()
	if !p.isAtEnd() {
		p.curr++
	}
	return tok
}

func (p *Parser) match(types ...lexer.TokenType) bool {
	for _, t := range types {
		if p.peek().Type == t {
			p.advance()
			return true
		}
	}
	return false
}

func (p *Parser) skipNewlines() {
	for p.peek().Type == lexer.TOKEN_NEWLINE {
		p.advance()
	}
}

func (p *Parser) ParseProgram() (*ast.Program, error) {
	prog := &ast.Program{Statements: make([]ast.Statement, 0)}

	p.skipNewlines()
	for !p.isAtEnd() {
		stmt, err := p.parseStatement()
		if err != nil {
			return nil, err
		}
		if stmt != nil {
			prog.Statements = append(prog.Statements, stmt)
		}
		p.skipNewlines()
	}

	return prog, nil
}

func (p *Parser) parseStatement() (ast.Statement, error) {
	p.skipNewlines()
	if p.isAtEnd() {
		return nil, nil
	}

	// 1. Assignment: set IDENTIFIER Expression
	if p.peek().Type == lexer.TOKEN_SET {
		p.advance()
		if p.peek().Type != lexer.TOKEN_IDENTIFIER && p.peek().Type != lexer.TOKEN_SIGIL_FFI {
			return nil, fmt.Errorf("line %d: expected identifier after 'set', got %s", p.peek().Line, p.peek().Lexeme)
		}
		varName := p.advance().Lexeme
		expr, err := p.parsePipeline()
		if err != nil {
			return nil, err
		}
		return &ast.SetStmt{VarName: varName, Value: expr}, nil
	}

	// 2. Return: return Expression
	if p.peek().Type == lexer.TOKEN_RETURN {
		p.advance()
		var expr ast.Expr
		if !p.isAtEnd() && p.peek().Type != lexer.TOKEN_NEWLINE && p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_RBRACKET {
			var err error
			expr, err = p.parsePipeline()
			if err != nil {
				return nil, err
			}
		}
		return &ast.ReturnStmt{Value: expr}, nil
	}

	// 3. Method definition: Receiver "method" <signature> Block
	// e.g. OrderProcessor method initializeWithTax rate logger log { ... }
	if p.canStartPrimary(p.peek()) && p.isMethodDefAhead() {
		recv, err := p.parsePrimary()
		if err != nil {
			return nil, err
		}
		p.advance() // consume "method"
		return p.parseMethodDef(recv)
	}

	// 4. Default: Pipeline
	pipe, err := p.parsePipeline()
	if err != nil {
		return nil, err
	}
	return &ast.ExprStmt{Expression: pipe}, nil
}

func (p *Parser) isMethodDefAhead() bool {
	// Look ahead to see if next token after primary is TOKEN_METHOD
	// For simple primary (identifier):
	if p.peekNext().Type == lexer.TOKEN_METHOD {
		return true
	}
	return false
}

func (p *Parser) parseMethodDef(recv ast.Expr) (ast.Statement, error) {
	// After "method", read signature tokens until "{"
	sigTokens := make([]lexer.Token, 0)
	for !p.isAtEnd() && p.peek().Type != lexer.TOKEN_LBRACE {
		if p.peek().Type == lexer.TOKEN_NEWLINE {
			return nil, fmt.Errorf("line %d: unexpected newline in method signature", p.peek().Line)
		}
		sigTokens = append(sigTokens, p.advance())
	}

	if p.peek().Type != lexer.TOKEN_LBRACE {
		return nil, fmt.Errorf("expected '{' for method body")
	}

	blockExpr, err := p.parseBlock()
	if err != nil {
		return nil, err
	}

	if len(sigTokens) == 0 {
		return nil, fmt.Errorf("method signature cannot be empty")
	}

	var selector string
	params := make([]string, 0)

	if sigTokens[0].Type == lexer.TOKEN_BINARY_OP {
		selector = sigTokens[0].Lexeme
		if len(sigTokens) > 1 {
			params = append(params, sigTokens[1].Lexeme)
		}
	} else if len(sigTokens) == 1 {
		// Unary selector
		selector = sigTokens[0].Lexeme
	} else if len(sigTokens) == 2 {
		// Single keyword selector e.g. "title t" -> "title:"
		selector = sigTokens[0].Lexeme + ":"
		params = append(params, sigTokens[1].Lexeme)
	} else if len(sigTokens)%2 == 0 {
		// Multi-keyword selector: kw1 arg1 kw2 arg2 ...
		for i := 0; i < len(sigTokens); i += 2 {
			selector += sigTokens[i].Lexeme + ":"
			params = append(params, sigTokens[i+1].Lexeme)
		}
	} else {
		// Fallback: concatenate all as selector with params
		selector = sigTokens[0].Lexeme
		for i := 1; i < len(sigTokens); i++ {
			params = append(params, sigTokens[i].Lexeme)
		}
	}

	return &ast.MethodDefStmt{
		Receiver: recv,
		Selector: selector,
		Params:   params,
		Body:     blockExpr,
	}, nil
}

func (p *Parser) parsePipeline() (*ast.Pipeline, error) {
	firstCascade, err := p.parseCascade()
	if err != nil {
		return nil, err
	}

	steps := make([]*ast.Cascade, 0)
	for {
		p.skipNewlines()
		if p.peek().Type == lexer.TOKEN_PIPE {
			p.advance() // consume '|'
			p.skipNewlines()
			nextCascade, err := p.parseCascadeAfterPipe()
			if err != nil {
				return nil, err
			}
			steps = append(steps, nextCascade)
		} else {
			break
		}
	}

	return &ast.Pipeline{
		First: firstCascade,
		Steps: steps,
	}, nil
}

func (p *Parser) parseCascade() (*ast.Cascade, error) {
	msgSend, err := p.parseMessageSend()
	if err != nil {
		return nil, err
	}

	messages := make([]ast.Message, 0)
	for {
		// Cascade separator is ','
		if p.peek().Type == lexer.TOKEN_COMMA {
			p.advance() // consume ','
			p.skipNewlines()
			msg, err := p.parseMessage()
			if err != nil {
				return nil, err
			}
			messages = append(messages, msg)
		} else {
			break
		}
	}

	return &ast.Cascade{
		FirstMsg: msgSend,
		Messages: messages,
	}, nil
}

func (p *Parser) parseCascadeAfterPipe() (*ast.Cascade, error) {
	// After '|', the receiver may be implicit (piped from previous step)
	// We parse a MessageSend where Receiver can be nil if next token starts a Message!
	var msgSend *ast.MessageSend

	// Check if next token is a message without explicit receiver
	// In pipeline: `| reject { ... }` or `| sorted` or `| + 2`
	if p.isMessageAhead() {
		msg, err := p.parseMessage()
		if err != nil {
			return nil, err
		}
		msgSend = &ast.MessageSend{Receiver: nil, Msg: msg}
	} else {
		// Could be an FFI call or Block call: `| @puts` or `| { ... }`
		if p.peek().Type == lexer.TOKEN_SIGIL_FFI {
			tok := p.advance()
			args := make([]ast.Expr, 0)
			for p.canStartPrimary(p.peek()) && !p.isStmtTerminator(p.peek()) {
				arg, err := p.parsePrimary()
				if err != nil {
					return nil, err
				}
				args = append(args, arg)
			}
			ffiExpr := &ast.FFIExpr{Name: tok.Lexeme, Args: args}
			msgSend = &ast.MessageSend{Receiver: ffiExpr, Msg: nil}
		} else if p.peek().Type == lexer.TOKEN_LBRACE {
			block, err := p.parseBlock()
			if err != nil {
				return nil, err
			}
			msgSend = &ast.MessageSend{Receiver: block, Msg: nil}
		} else {
			var err error
			msgSend, err = p.parseMessageSend()
			if err != nil {
				return nil, err
			}
		}
	}

	messages := make([]ast.Message, 0)
	for {
		if p.peek().Type == lexer.TOKEN_COMMA {
			p.advance()
			p.skipNewlines()
			msg, err := p.parseMessage()
			if err != nil {
				return nil, err
			}
			messages = append(messages, msg)
		} else {
			break
		}
	}

	return &ast.Cascade{
		FirstMsg: msgSend,
		Messages: messages,
	}, nil
}

func (p *Parser) parseMessageSend() (*ast.MessageSend, error) {
	primary, err := p.parsePrimary()
	if err != nil {
		return nil, err
	}

	// Check if followed by a Message (Unary, Binary, or Keyword)
	if p.isMessageAhead() {
		msg, err := p.parseMessage()
		if err != nil {
			return nil, err
		}
		return &ast.MessageSend{Receiver: primary, Msg: msg}, nil
	}

	return &ast.MessageSend{Receiver: primary, Msg: nil}, nil
}

func (p *Parser) isMessageAhead() bool {
	if p.isAtEnd() {
		return false
	}
	tok := p.peek()
	if p.isStmtTerminator(tok) {
		return false
	}
	if tok.Type == lexer.TOKEN_BINARY_OP {
		return true
	}
	if tok.Type == lexer.TOKEN_IDENTIFIER {
		return true
	}
	return false
}

func (p *Parser) parseMessage() (ast.Message, error) {
	tok := p.peek()

	// 1. Binary message: BINARY_OP Primary
	if tok.Type == lexer.TOKEN_BINARY_OP {
		opTok := p.advance()
		arg, err := p.parsePrimary()
		if err != nil {
			return nil, err
		}
		return &ast.BinaryMessage{Op: opTok.Lexeme, Argument: arg}, nil
	}

	// 2. Unary or Keyword message starting with IDENTIFIER
	if tok.Type == lexer.TOKEN_IDENTIFIER {
		firstIdent := p.advance().Lexeme

		// Check if followed by a Primary argument -> Keyword message
		if p.canStartPrimary(p.peek()) && !p.isStmtTerminator(p.peek()) {
			keywords := []string{firstIdent}
			args := make([]ast.Expr, 0)

			firstArg, err := p.parsePrimary()
			if err != nil {
				return nil, err
			}
			args = append(args, firstArg)

			// Parse subsequent (IDENTIFIER Primary) pairs if present
			for p.peek().Type == lexer.TOKEN_IDENTIFIER {
				// Ensure that the identifier is followed by a Primary argument
				// If not followed by Primary, it could be the end or something else
				nextTok := p.peekNext()
				if p.canStartPrimary(nextTok) && !p.isStmtTerminator(nextTok) {
					kw := p.advance().Lexeme
					arg, err := p.parsePrimary()
					if err != nil {
						return nil, err
					}
					keywords = append(keywords, kw)
					args = append(args, arg)
				} else {
					break
				}
			}

			return &ast.KeywordMessage{Keywords: keywords, Args: args}, nil
		}

		// Otherwise: Unary message
		return &ast.UnaryMessage{Selector: firstIdent}, nil
	}

	return nil, fmt.Errorf("line %d: expected message (identifier or binary operator), got %s", tok.Line, tok.Lexeme)
}

func (p *Parser) canStartPrimary(tok lexer.Token) bool {
	switch tok.Type {
	case lexer.TOKEN_IDENTIFIER,
		lexer.TOKEN_INT,
		lexer.TOKEN_FLOAT,
		lexer.TOKEN_STRING,
		lexer.TOKEN_SIGIL_FFI,
		lexer.TOKEN_LBRACKET,
		lexer.TOKEN_LBRACE:
		return true
	default:
		return false
	}
}

func (p *Parser) isStmtTerminator(tok lexer.Token) bool {
	switch tok.Type {
	case lexer.TOKEN_EOF,
		lexer.TOKEN_NEWLINE,
		lexer.TOKEN_COMMA,
		lexer.TOKEN_PIPE,
		lexer.TOKEN_RBRACKET,
		lexer.TOKEN_RBRACE:
		return true
	default:
		return false
	}
}

func (p *Parser) parsePrimary() (ast.Expr, error) {
	tok := p.peek()

	switch tok.Type {
	case lexer.TOKEN_INT:
		p.advance()
		val, err := strconv.ParseInt(tok.Lexeme, 10, 64)
		if err != nil {
			return nil, err
		}
		return &ast.LiteralExpr{Kind: ast.LIT_INT, IntValue: val}, nil

	case lexer.TOKEN_FLOAT:
		p.advance()
		val, err := strconv.ParseFloat(tok.Lexeme, 64)
		if err != nil {
			return nil, err
		}
		return &ast.LiteralExpr{Kind: ast.LIT_FLOAT, FltValue: val}, nil

	case lexer.TOKEN_STRING:
		p.advance()
		return &ast.LiteralExpr{Kind: ast.LIT_STRING, StrValue: tok.Lexeme}, nil

	case lexer.TOKEN_IDENTIFIER:
		p.advance()
		switch tok.Lexeme {
		case "nil":
			return &ast.LiteralExpr{Kind: ast.LIT_NIL}, nil
		case "true":
			return &ast.LiteralExpr{Kind: ast.LIT_BOOL, BoolVal: true}, nil
		case "false":
			return &ast.LiteralExpr{Kind: ast.LIT_BOOL, BoolVal: false}, nil
		default:
			return &ast.IdentifierExpr{Name: tok.Lexeme}, nil
		}

	case lexer.TOKEN_SIGIL_FFI:
		p.advance()
		// Check if followed by inline arguments
		args := make([]ast.Expr, 0)
		for p.canStartPrimary(p.peek()) && !p.isStmtTerminator(p.peek()) {
			arg, err := p.parsePrimary()
			if err != nil {
				return nil, err
			}
			args = append(args, arg)
		}
		return &ast.FFIExpr{Name: tok.Lexeme, Args: args}, nil

	case lexer.TOKEN_LBRACKET:
		p.advance() // consume '['
		p.skipNewlines()
		pipe, err := p.parsePipeline()
		if err != nil {
			return nil, err
		}
		p.skipNewlines()
		if p.peek().Type != lexer.TOKEN_RBRACKET {
			return nil, fmt.Errorf("line %d: expected ']' after inline subexpression, got %s", p.peek().Line, p.peek().Lexeme)
		}
		p.advance() // consume ']'
		return &ast.SubExpr{Pipeline: pipe}, nil

	case lexer.TOKEN_LBRACE:
		return p.parseBlock()

	default:
		return nil, fmt.Errorf("line %d: unexpected token %s at start of expression", tok.Line, tok.Lexeme)
	}
}

func (p *Parser) parseBlock() (*ast.BlockExpr, error) {
	p.advance() // consume '{'
	p.skipNewlines()

	// Check for BlockParams: IDENTIFIER+ "|"
	params := make([]string, 0)
	hasPipe := false
	saveCurr := p.curr

	for !p.isAtEnd() && p.peek().Type == lexer.TOKEN_IDENTIFIER {
		p.advance()
		if p.peek().Type == lexer.TOKEN_PIPE {
			hasPipe = true
			break
		}
	}

	if hasPipe {
		// Restore and properly collect params
		p.curr = saveCurr
		for p.peek().Type == lexer.TOKEN_IDENTIFIER {
			params = append(params, p.advance().Lexeme)
		}
		p.advance() // consume '|'
	} else {
		// No params, restore
		p.curr = saveCurr
	}

	p.skipNewlines()
	bodyStmts := make([]ast.Statement, 0)
	for !p.isAtEnd() && p.peek().Type != lexer.TOKEN_RBRACE {
		stmt, err := p.parseStatement()
		if err != nil {
			return nil, err
		}
		if stmt != nil {
			bodyStmts = append(bodyStmts, stmt)
		}
		p.skipNewlines()
	}

	if p.peek().Type != lexer.TOKEN_RBRACE {
		return nil, fmt.Errorf("expected '}' to close block")
	}
	p.advance() // consume '}'

	return &ast.BlockExpr{
		Params: params,
		Body:   &ast.Program{Statements: bodyStmts},
	}, nil
}
