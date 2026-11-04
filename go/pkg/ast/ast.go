package ast

import "strings"

type Node interface {
	node()
}

type Expr interface {
	Node
	expr()
}

type Statement interface {
	Node
	stmt()
}

type Program struct {
	Statements []Statement
}

func (p *Program) node() {}

type SetStmt struct {
	VarName string
	Value   Expr
}

func (s *SetStmt) node() {}
func (s *SetStmt) stmt() {}

type ReturnStmt struct {
	Value Expr
}

func (r *ReturnStmt) node() {}
func (r *ReturnStmt) stmt() {}

type MethodDefStmt struct {
	Receiver Expr
	Selector string
	Params   []string
	Body     *BlockExpr
}

func (m *MethodDefStmt) node() {}
func (m *MethodDefStmt) stmt() {}

type ExprStmt struct {
	Expression Expr
}

func (e *ExprStmt) node() {}
func (e *ExprStmt) stmt() {}

type Pipeline struct {
	First *Cascade
	Steps []*Cascade
}

func (p *Pipeline) node() {}
func (p *Pipeline) expr() {}

type Cascade struct {
	FirstMsg *MessageSend
	Messages []Message
}

func (c *Cascade) node() {}
func (c *Cascade) expr() {}

type MessageSend struct {
	Receiver Expr // Can be nil if receiver is piped from left
	Msg      Message
}

func (m *MessageSend) node() {}
func (m *MessageSend) expr() {}

type Message interface {
	Node
	SelectorName() string
}

type UnaryMessage struct {
	Selector string
}

func (u *UnaryMessage) node()                {}
func (u *UnaryMessage) SelectorName() string { return u.Selector }

type BinaryMessage struct {
	Op       string
	Argument Expr
}

func (b *BinaryMessage) node()                {}
func (b *BinaryMessage) SelectorName() string { return b.Op }

type KeywordMessage struct {
	Keywords []string
	Args     []Expr
}

func (k *KeywordMessage) node() {}
func (k *KeywordMessage) SelectorName() string {
	var sb strings.Builder
	for _, kw := range k.Keywords {
		sb.WriteString(kw)
		sb.WriteString(":")
	}
	return sb.String()
}

type LiteralExpr struct {
	Kind     LiteralKind
	IntValue int64
	FltValue float64
	StrValue string
	BoolVal  bool
}

type LiteralKind int

const (
	LIT_NIL LiteralKind = iota
	LIT_BOOL
	LIT_INT
	LIT_FLOAT
	LIT_STRING
)

func (l *LiteralExpr) node() {}
func (l *LiteralExpr) expr() {}

type IdentifierExpr struct {
	Name string
}

func (i *IdentifierExpr) node() {}
func (i *IdentifierExpr) expr() {}

type FFIExpr struct {
	Name string
	Args []Expr
}

func (f *FFIExpr) node() {}
func (f *FFIExpr) expr() {}

type SubExpr struct {
	Pipeline *Pipeline
}

func (s *SubExpr) node() {}
func (s *SubExpr) expr() {}

type BlockExpr struct {
	Params []string
	Body   *Program
	Raw    string
}

func (b *BlockExpr) node() {}
func (b *BlockExpr) expr() {}
