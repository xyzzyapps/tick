package runtime

import (
	"fmt"
	"io"
	"strings"
	"tcltalk/pkg/ast"
	"tcltalk/pkg/lexer"
	"tcltalk/pkg/parser"
)

type VM struct {
	Globals    *Scope
	FFIHandler func(name string, args []Value, vm *VM) (Value, error)
	ActiveCoro *CoroutineVal
	Output     io.Writer
}

func (v *VM) WriteOut(s string) {
	if v.Output != nil {
		fmt.Fprint(v.Output, s)
	} else {
		fmt.Print(s)
	}
}

func (v *VM) WriteOutLn(s string) {
	if v.Output != nil {
		fmt.Fprintln(v.Output, s)
	} else {
		fmt.Println(s)
	}
}

func NewVM() *VM {
	globals := NewScope(nil, nil, 0)
	vm := &VM{Globals: globals}

	// Register core classes in Globals
	globals.Set("Object", ObjectClass.AsValue())
	globals.Set("object", ObjectClass.AsValue())
	globals.Set("Class", MetaclassClass.AsValue())
	globals.Set("Boolean", BooleanClass.AsValue())
	globals.Set("boolean", BooleanClass.AsValue())
	globals.Set("True", TrueClass.AsValue())
	globals.Set("False", FalseClass.AsValue())
	globals.Set("Number", NumberClass.AsValue())
	globals.Set("number", NumberClass.AsValue())
	globals.Set("Integer", IntegerClass.AsValue())
	globals.Set("integer", IntegerClass.AsValue())
	globals.Set("Float", FloatClass.AsValue())
	globals.Set("float", FloatClass.AsValue())
	globals.Set("String", StringClass.AsValue())
	globals.Set("string", StringClass.AsValue())
	globals.Set("Array", ArrayClass.AsValue())
	globals.Set("array", ArrayClass.AsValue())
	globals.Set("List", ArrayClass.AsValue())
	globals.Set("list", ArrayClass.AsValue())
	globals.Set("Dictionary", DictClass.AsValue())
	globals.Set("dict", DictClass.AsValue())
	globals.Set("JSON", JSONClass.AsValue())
	globals.Set("json", JSONClass.AsValue())
	globals.Set("Coroutine", CoroutineClass.AsValue())
	globals.Set("coroutine", CoroutineClass.AsValue())

	InitBuiltinMethods(vm)
	InitCoroutineMethods(vm)

	return vm
}

func (v *VM) EvalProgram(prog *ast.Program) (Value, error) {
	var lastVal Value = NilVal{}
	frameID := NextFrameID()
	topScope := NewScope(v.Globals, nil, frameID)

	for _, stmt := range prog.Statements {
		val, sig, err := v.EvalStatement(stmt, topScope)
		if err != nil {
			if s, ok := err.(*ReturnSignal); ok {
				return s.Value, nil
			}
			return nil, err
		}
		if sig != nil {
			return sig.Value, nil
		}
		lastVal = val
	}
	return lastVal, nil
}

func (v *VM) EvalStatement(stmt ast.Statement, scope *Scope) (Value, *ReturnSignal, error) {
	switch s := stmt.(type) {
	case *ast.SetStmt:
		val, sig, err := v.EvalExpr(s.Value, scope)
		if err != nil || sig != nil {
			return val, sig, err
		}
		if strings.HasPrefix(s.VarName, "@") {
			// Setting FFI variable
			if v.FFIHandler != nil {
				_, err := v.FFIHandler("set:"+s.VarName, []Value{val}, v)
				if err != nil {
					return nil, nil, err
				}
			}
		} else {
			scope.Set(s.VarName, val)
		}
		return val, nil, nil

	case *ast.ReturnStmt:
		var val Value = NilVal{}
		if s.Value != nil {
			vVal, sig, err := v.EvalExpr(s.Value, scope)
			if err != nil || sig != nil {
				return vVal, sig, err
			}
			val = vVal
		}
		return val, &ReturnSignal{
			TargetFrameID: scope.CallFrameID,
			Value:         val,
		}, nil

	case *ast.MethodDefStmt:
		recvVal, sig, err := v.EvalExpr(s.Receiver, scope)
		if err != nil || sig != nil {
			return recvVal, sig, err
		}
		classVal, ok := recvVal.(*ClassVal)
		if !ok {
			return nil, nil, fmt.Errorf("method receiver must evaluate to a Class, got %s", recvVal.Type())
		}
		method := &UserMethod{
			Selector: s.Selector,
			Params:   s.Params,
			Body:     s.Body.Body,
		}
		classVal.Klass.AddMethod(s.Selector, method)
		return classVal, nil, nil

	case *ast.ExprStmt:
		return v.EvalExpr(s.Expression, scope)

	default:
		return nil, nil, fmt.Errorf("unknown statement type: %T", stmt)
	}
}

func (v *VM) EvalExpr(expr ast.Expr, scope *Scope) (Value, *ReturnSignal, error) {
	switch e := expr.(type) {
	case *ast.Pipeline:
		return v.EvalPipeline(e, scope)
	case *ast.Cascade:
		return v.EvalCascade(e, nil, scope)
	case *ast.MessageSend:
		return v.EvalMessageSend(e, nil, scope)
	case *ast.LiteralExpr:
		return v.evalLiteral(e, scope)
	case *ast.IdentifierExpr:
		val, ok := scope.Get(e.Name)
		if !ok {
			// In Tcl-Talk, unquoted words act as symbols / strings (e.g. class names in subclass:)
			return StringVal{Val: e.Name}, nil, nil
		}
		return val, nil, nil
	case *ast.FFIExpr:
		return v.evalFFI(e, nil, scope)
	case *ast.SubExpr:
		return v.EvalPipeline(e.Pipeline, scope)
	case *ast.BlockExpr:
		return &BlockVal{
			Params:          e.Params,
			Body:            e.Body,
			CapturedScope:   scope,
			EnclosingFrameID: scope.CallFrameID,
		}, nil, nil
	default:
		return nil, nil, fmt.Errorf("unknown expr type: %T", expr)
	}
}

func (v *VM) EvalPipeline(pipe *ast.Pipeline, scope *Scope) (Value, *ReturnSignal, error) {
	curr, sig, err := v.EvalCascade(pipe.First, nil, scope)
	if err != nil || sig != nil {
		return curr, sig, err
	}

	for _, step := range pipe.Steps {
		curr, sig, err = v.EvalCascade(step, curr, scope)
		if err != nil || sig != nil {
			return curr, sig, err
		}
	}
	return curr, nil, nil
}

func (v *VM) EvalCascade(cascade *ast.Cascade, pipedRecv Value, scope *Scope) (Value, *ReturnSignal, error) {
	// First message send
	firstVal, sig, err := v.EvalMessageSend(cascade.FirstMsg, pipedRecv, scope)
	if err != nil || sig != nil {
		return firstVal, sig, err
	}

	if len(cascade.Messages) == 0 {
		return firstVal, nil, nil
	}

	// For cascade (with ','):
	// All subsequent messages are sent to the target receiver (the object produced/targeted by first message).
	target := firstVal
	for _, msg := range cascade.Messages {
		_, err := v.sendMsgNode(target, msg, scope)
		if err != nil {
			return nil, nil, err
		}
	}

	return target, nil, nil
}

func (v *VM) EvalMessageSend(msgSend *ast.MessageSend, pipedRecv Value, scope *Scope) (Value, *ReturnSignal, error) {
	if ffi, ok := msgSend.Receiver.(*ast.FFIExpr); ok && pipedRecv != nil {
		return v.evalFFI(ffi, pipedRecv, scope)
	}
	if blk, ok := msgSend.Receiver.(*ast.BlockExpr); ok && pipedRecv != nil {
		bVal, _, err := v.EvalExpr(blk, scope)
		if err != nil {
			return nil, nil, err
		}
		res, err := v.EvalBlock(bVal, []Value{pipedRecv})
		return res, nil, err
	}

	var recv Value
	if msgSend.Receiver != nil {
		r, sig, err := v.EvalExpr(msgSend.Receiver, scope)
		if err != nil || sig != nil {
			return r, sig, err
		}
		recv = r
	} else if pipedRecv != nil {
		recv = pipedRecv
	} else {
		return nil, nil, fmt.Errorf("message send has no receiver")
	}

	if msgSend.Msg == nil {
		return recv, nil, nil
	}

	val, err := v.sendMsgNode(recv, msgSend.Msg, scope)
	if err != nil {
		return nil, nil, err
	}
	return val, nil, nil
}

func (v *VM) sendMsgNode(recv Value, msg ast.Message, scope *Scope) (Value, error) {
	switch m := msg.(type) {
	case *ast.UnaryMessage:
		return v.SendMessage(recv, m.Selector, nil)

	case *ast.BinaryMessage:
		argVal, sig, err := v.EvalExpr(m.Argument, scope)
		if err != nil {
			return nil, err
		}
		if sig != nil {
			return sig.Value, nil
		}
		return v.SendMessage(recv, m.Op, []Value{argVal})

	case *ast.KeywordMessage:
		args := make([]Value, len(m.Args))
		for i, argExpr := range m.Args {
			aVal, sig, err := v.EvalExpr(argExpr, scope)
			if err != nil {
				return nil, err
			}
			if sig != nil {
				return sig.Value, nil
			}
			args[i] = aVal
		}
		selector := m.SelectorName()
		return v.SendMessage(recv, selector, args)

	default:
		return nil, fmt.Errorf("unknown message type: %T", msg)
	}
}

func (v *VM) SendMessage(receiver Value, selector string, args []Value) (Value, error) {
	if receiver == nil {
		receiver = NilVal{}
	}

	klass := receiver.Class()
	method := klass.LookupMethod(selector)
	if method == nil {
		// Try fallback: if selector has no colons and args count == 1, try with colon
		if !strings.Contains(selector, ":") && len(args) == 1 {
			method = klass.LookupMethod(selector + ":")
		}
	}

	if method == nil {
		return nil, fmt.Errorf("MessageNotUnderstood: %s does not understand '%s'", receiver.Class().Name, selector)
	}

	switch m := method.(type) {
	case *NativeMethod:
		return m.Fn(receiver, args, v)

	case *UserMethod:
		return v.invokeUserMethod(receiver, m, args)

	default:
		return nil, fmt.Errorf("unknown method implementation for selector '%s'", selector)
	}
}

func (v *VM) invokeUserMethod(receiver Value, m *UserMethod, args []Value) (Value, error) {
	frameID := NextFrameID()
	methodScope := NewScope(v.Globals, receiver, frameID)

	// Bind arguments to parameters
	for i, param := range m.Params {
		if i < len(args) {
			methodScope.Define(param, args[i])
		} else {
			methodScope.Define(param, NilVal{})
		}
	}

	// Execute body statements
	for _, stmt := range m.Body.Statements {
		val, sig, err := v.EvalStatement(stmt, methodScope)
		if err != nil {
			if s, ok := err.(*ReturnSignal); ok {
				if s.TargetFrameID == frameID {
					return s.Value, nil
				}
				return nil, s
			}
			return nil, err
		}
		if sig != nil {
			if sig.TargetFrameID == frameID {
				return sig.Value, nil
			}
			// Propagate non-local return up the call stack
			return nil, sig
		}
		_ = val
	}

	// Default Smalltalk return is self!
	return receiver, nil
}

func (v *VM) EvalBlock(blockVal Value, args []Value) (Value, error) {
	blk, ok := blockVal.(*BlockVal)
	if !ok {
		return nil, fmt.Errorf("cannot evaluate non-block value: %s", blockVal.Type())
	}

	blockScope := NewScope(blk.CapturedScope, blk.CapturedScope.Self, blk.EnclosingFrameID)

	for i, param := range blk.Params {
		if i < len(args) {
			blockScope.Define(param, args[i])
		} else {
			blockScope.Define(param, NilVal{})
		}
	}

	var lastVal Value = NilVal{}
	for _, stmt := range blk.Body.Statements {
		val, sig, err := v.EvalStatement(stmt, blockScope)
		if err != nil {
			return nil, err
		}
		if sig != nil {
			// Propagate non-local return!
			return nil, sig
		}
		lastVal = val
	}
	return lastVal, nil
}

func (v *VM) evalLiteral(lit *ast.LiteralExpr, scope *Scope) (Value, *ReturnSignal, error) {
	switch lit.Kind {
	case ast.LIT_NIL:
		return NilVal{}, nil, nil
	case ast.LIT_BOOL:
		return BoolVal{Val: lit.BoolVal}, nil, nil
	case ast.LIT_INT:
		return IntVal{Val: lit.IntValue}, nil, nil
	case ast.LIT_FLOAT:
		return FloatVal{Val: lit.FltValue}, nil, nil
	case ast.LIT_STRING:
		interpolated, err := v.interpolateString(lit.StrValue, scope)
		if err != nil {
			return nil, nil, err
		}
		return StringVal{Val: interpolated}, nil, nil
	default:
		return NilVal{}, nil, nil
	}
}

func (v *VM) evalFFI(ffi *ast.FFIExpr, pipedVal Value, scope *Scope) (Value, *ReturnSignal, error) {
	args := make([]Value, 0, len(ffi.Args)+1)
	if pipedVal != nil {
		args = append(args, pipedVal)
	}
	for _, argExpr := range ffi.Args {
		aVal, sig, err := v.EvalExpr(argExpr, scope)
		if err != nil || sig != nil {
			return aVal, sig, err
		}
		args = append(args, aVal)
	}

	if v.FFIHandler == nil {
		return nil, nil, fmt.Errorf("FFI handler not initialized")
	}

	val, err := v.FFIHandler(ffi.Name, args, v)
	if err != nil {
		return nil, nil, err
	}
	return val, nil, nil
}

func (v *VM) interpolateString(s string, scope *Scope) (string, error) {
	var sb strings.Builder
	runes := []rune(s)
	n := len(runes)

	for i := 0; i < n; i++ {
		ch := runes[i]
		if ch == '$' && i+1 < n && runes[i+1] == '{' {
			// ${varName} interpolation
			j := i + 2
			for j < n && runes[j] != '}' {
				j++
			}
			if j < n && runes[j] == '}' {
				varName := string(runes[i+2 : j])
				if val, ok := scope.Get(varName); ok {
					sb.WriteString(val.String())
				} else {
					sb.WriteString("${" + varName + "}")
				}
				i = j
			} else {
				sb.WriteRune(ch)
			}
		} else if ch == '$' && i+1 < n && (isLetter(runes[i+1]) || runes[i+1] == '_') {
			// $varName interpolation
			j := i + 1
			for j < n && (isLetter(runes[j]) || isDigit(runes[j]) || runes[j] == '_') {
				j++
			}
			varName := string(runes[i+1 : j])
			if val, ok := scope.Get(varName); ok {
				sb.WriteString(val.String())
			} else {
				sb.WriteString("$" + varName)
			}
			i = j - 1
		} else if ch == '@' && i+1 < n && (runes[i+1] >= 'A' && runes[i+1] <= 'Z') {
			// @VAR_NAME uppercase FFI variable interpolation
			j := i + 1
			for j < n && (isLetter(runes[j]) || isDigit(runes[j]) || runes[j] == '_') {
				j++
			}
			varName := string(runes[i:j])
			if v.FFIHandler != nil {
				if ffiVal, err := v.FFIHandler(varName, nil, v); err == nil {
					sb.WriteString(ffiVal.String())
				} else {
					sb.WriteString(varName)
				}
			} else {
				sb.WriteString(varName)
			}
			i = j - 1
		} else if ch == '[' {
			// Subexpression interpolation [ ... ]
			j := i + 1
			depth := 1
			for j < n && depth > 0 {
				if runes[j] == '[' {
					depth++
				} else if runes[j] == ']' {
					depth--
				}
				j++
			}
			if depth == 0 {
				subCode := string(runes[i+1 : j-1])
				lex := lexer.NewLexer(subCode)
				tokens, err := lex.Tokenize()
				if err != nil {
					sb.WriteRune('[')
					sb.WriteString(subCode)
					sb.WriteRune(']')
					i = j - 1
					continue
				}
				p := parser.NewParser(tokens)
				pipe, err := p.ParseProgram()
				if err != nil || len(pipe.Statements) == 0 {
					sb.WriteRune('[')
					sb.WriteString(subCode)
					sb.WriteRune(']')
					i = j - 1
					continue
				}
				// Evaluate program statements in current scope
				var subVal Value = NilVal{}
				evalFailed := false
				for _, st := range pipe.Statements {
					val, sig, err := v.EvalStatement(st, scope)
					if err != nil {
						evalFailed = true
						break
					}
					if sig != nil {
						return "", sig
					}
					subVal = val
				}
				if evalFailed {
					sb.WriteRune('[')
					sb.WriteString(subCode)
					sb.WriteRune(']')
				} else {
					sb.WriteString(subVal.String())
				}
				i = j - 1
			} else {
				sb.WriteRune(ch)
			}
		} else {
			sb.WriteRune(ch)
		}
	}
	return sb.String(), nil
}

func isLetter(ch rune) bool {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
}

func isDigit(ch rune) bool {
	return ch >= '0' && ch <= '9'
}
