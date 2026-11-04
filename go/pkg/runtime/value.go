package runtime

import (
	"fmt"
	"strings"
	"tcltalk/pkg/ast"
)

type Value interface {
	Type() string
	String() string
	Class() *Class
	IsTruthy() bool
}

type NilVal struct{}

func (n NilVal) Type() string     { return "nil" }
func (n NilVal) String() string   { return "nil" }
func (n NilVal) Class() *Class    { return UndefinedObjectClass }
func (n NilVal) IsTruthy() bool   { return false }

type BoolVal struct {
	Val bool
}

func (b BoolVal) Type() string   { return "bool" }
func (b BoolVal) String() string { return fmt.Sprintf("%t", b.Val) }
func (b BoolVal) Class() *Class {
	if b.Val {
		return TrueClass
	}
	return FalseClass
}
func (b BoolVal) IsTruthy() bool { return b.Val }

type IntVal struct {
	Val int64
}

func (i IntVal) Type() string     { return "int" }
func (i IntVal) String() string   { return fmt.Sprintf("%d", i.Val) }
func (i IntVal) Class() *Class    { return IntegerClass }
func (i IntVal) IsTruthy() bool   { return i.Val != 0 }

type FloatVal struct {
	Val float64
}

func (f FloatVal) Type() string     { return "float" }
func (f FloatVal) String() string   { return fmt.Sprintf("%g", f.Val) }
func (f FloatVal) Class() *Class    { return FloatClass }
func (f FloatVal) IsTruthy() bool   { return f.Val != 0.0 }

type StringVal struct {
	Val string
}

func (s StringVal) Type() string     { return "string" }
func (s StringVal) String() string   { return s.Val }
func (s StringVal) Class() *Class    { return StringClass }
func (s StringVal) IsTruthy() bool   { return len(s.Val) > 0 }

type PointVal struct {
	X Value
	Y Value
}

func (p PointVal) Type() string     { return "point" }
func (p PointVal) String() string   { return fmt.Sprintf("%s@%s", p.X.String(), p.Y.String()) }
func (p PointVal) Class() *Class    { return PointClass }
func (p PointVal) IsTruthy() bool   { return true }

type ArrayVal struct {
	Elements []Value
}

func (a *ArrayVal) Type() string { return "array" }
func (a *ArrayVal) String() string {
	parts := make([]string, len(a.Elements))
	for i, el := range a.Elements {
		parts[i] = el.String()
	}
	return "[" + strings.Join(parts, ", ") + "]"
}
func (a *ArrayVal) Class() *Class  { return ArrayClass }
func (a *ArrayVal) IsTruthy() bool { return len(a.Elements) > 0 }

type DictVal struct {
	Entries map[string]Value
}

func (d *DictVal) Type() string { return "dict" }
func (d *DictVal) String() string {
	parts := make([]string, 0, len(d.Entries))
	for k, v := range d.Entries {
		parts = append(parts, fmt.Sprintf("%s: %s", k, v.String()))
	}
	return "{" + strings.Join(parts, ", ") + "}"
}
func (d *DictVal) Class() *Class  { return DictClass }
func (d *DictVal) IsTruthy() bool { return len(d.Entries) > 0 }

type BlockVal struct {
	Params          []string
	Body            *ast.Program
	CapturedScope   *Scope
	EnclosingFrameID int64
}

func (b *BlockVal) Type() string   { return "block" }
func (b *BlockVal) String() string { return fmt.Sprintf("{ block (%d params) }", len(b.Params)) }
func (b *BlockVal) Class() *Class  { return BlockClosureClass }
func (b *BlockVal) IsTruthy() bool { return true }

type InstanceVal struct {
	Klass  *Class
	Fields map[string]Value
}

func (in *InstanceVal) Type() string { return in.Klass.Name }
func (in *InstanceVal) String() string {
	return fmt.Sprintf("<%s instance>", in.Klass.Name)
}
func (in *InstanceVal) Class() *Class  { return in.Klass }
func (in *InstanceVal) IsTruthy() bool { return true }

type ClassVal struct {
	Klass *Class
}

func (c *ClassVal) Type() string     { return "class" }
func (c *ClassVal) String() string   { return c.Klass.Name }
func (c *ClassVal) Class() *Class    { return MetaclassClass }
func (c *ClassVal) IsTruthy() bool   { return true }

type FFIHandleVal struct {
	Handle interface{}
	Name   string
}

func (f *FFIHandleVal) Type() string     { return "ffihandle" }
func (f *FFIHandleVal) String() string   { return fmt.Sprintf("<ffi %s %v>", f.Name, f.Handle) }
func (f *FFIHandleVal) Class() *Class    { return FFIHandleClass }
func (f *FFIHandleVal) IsTruthy() bool   { return f.Handle != nil }
