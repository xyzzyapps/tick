package runtime

import (
	"fmt"
	"tcltalk/pkg/ast"
)

type Method interface {
	isMethod()
}

type NativeMethodFunc func(receiver Value, args []Value, vm *VM) (Value, error)

type NativeMethod struct {
	Fn NativeMethodFunc
}

func (n *NativeMethod) isMethod() {}

type UserMethod struct {
	Selector string
	Params   []string
	Body     *ast.Program
}

func (u *UserMethod) isMethod() {}

type Class struct {
	Name             string
	Superclass       *Class
	InstanceVarNames []string
	Methods          map[string]Method
}

func NewClass(name string, superclass *Class, ivars []string) *Class {
	return &Class{
		Name:             name,
		Superclass:       superclass,
		InstanceVarNames: ivars,
		Methods:          make(map[string]Method),
	}
}

func (c *Class) AddMethod(selector string, method Method) {
	c.Methods[selector] = method
}

func (c *Class) AddNativeMethod(selector string, fn NativeMethodFunc) {
	c.Methods[selector] = &NativeMethod{Fn: fn}
}

func (c *Class) LookupMethod(selector string) Method {
	curr := c
	for curr != nil {
		if m, ok := curr.Methods[selector]; ok {
			return m
		}
		curr = curr.Superclass
	}
	return nil
}

func (c *Class) NewInstance() *InstanceVal {
	fields := make(map[string]Value)
	// Collect instance variables from hierarchy
	curr := c
	for curr != nil {
		for _, iv := range curr.InstanceVarNames {
			if _, exists := fields[iv]; !exists {
				fields[iv] = NilVal{}
			}
		}
		curr = curr.Superclass
	}
	return &InstanceVal{
		Klass:  c,
		Fields: fields,
	}
}

func (c *Class) Subclass(name string, ivars []string) *Class {
	return NewClass(name, c, ivars)
}

func (c *Class) HasInstanceVar(name string) bool {
	curr := c
	for curr != nil {
		for _, iv := range curr.InstanceVarNames {
			if iv == name {
				return true
			}
		}
		curr = curr.Superclass
	}
	return false
}

// Global Classes
var (
	ObjectClass          *Class
	MetaclassClass       *Class
	UndefinedObjectClass *Class
	BooleanClass         *Class
	TrueClass            *Class
	FalseClass           *Class
	NumberClass          *Class
	IntegerClass         *Class
	FloatClass           *Class
	StringClass          *Class
	ArrayClass           *Class
	DictClass            *Class
	BlockClosureClass    *Class
	FFIHandleClass       *Class
)

func init() {
	ObjectClass = NewClass("Object", nil, nil)
	MetaclassClass = NewClass("Class", ObjectClass, nil)
	UndefinedObjectClass = NewClass("UndefinedObject", ObjectClass, nil)
	BooleanClass = NewClass("Boolean", ObjectClass, nil)
	TrueClass = NewClass("True", BooleanClass, nil)
	FalseClass = NewClass("False", BooleanClass, nil)
	NumberClass = NewClass("Number", ObjectClass, nil)
	IntegerClass = NewClass("integer", NumberClass, nil)
	FloatClass = NewClass("Float", NumberClass, nil)
	StringClass = NewClass("String", ObjectClass, nil)
	ArrayClass = NewClass("Array", ObjectClass, nil)
	DictClass = NewClass("Dictionary", ObjectClass, nil)
	BlockClosureClass = NewClass("BlockClosure", ObjectClass, nil)
	FFIHandleClass = NewClass("FFIHandle", ObjectClass, nil)
}

func (c *Class) AsValue() Value {
	return &ClassVal{Klass: c}
}

func (c *Class) String() string {
	return c.Name
}

func FormatSelector(kwParts []string) string {
	if len(kwParts) == 0 {
		return ""
	}
	res := ""
	for _, p := range kwParts {
		res += p + ":"
	}
	return res
}

func CheckArgCount(selector string, args []Value, expected int) error {
	if len(args) != expected {
		return fmt.Errorf("message %s expected %d argument(s), got %d", selector, expected, len(args))
	}
	return nil
}
