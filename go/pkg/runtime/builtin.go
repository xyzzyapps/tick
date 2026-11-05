package runtime

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"tcltalk/pkg/ast"
)

func InitBuiltinMethods(vm *VM) {
	// ================= Object =================
	ObjectClass.AddNativeMethod("print", func(recv Value, args []Value, v *VM) (Value, error) {
		v.WriteOut(recv.String())
		return recv, nil
	})
	ObjectClass.AddNativeMethod("println", func(recv Value, args []Value, v *VM) (Value, error) {
		v.WriteOutLn(recv.String())
		return recv, nil
	})
	ObjectClass.AddNativeMethod("asString", func(recv Value, args []Value, v *VM) (Value, error) {
		return StringVal{Val: recv.String()}, nil
	})
	ObjectClass.AddNativeMethod("==", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("==", args, 1); err != nil {
			return nil, err
		}
		eq := ValuesEqual(recv, args[0])
		return BoolVal{Val: eq}, nil
	})
	ObjectClass.AddNativeMethod("!=", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("!=", args, 1); err != nil {
			return nil, err
		}
		eq := ValuesEqual(recv, args[0])
		return BoolVal{Val: !eq}, nil
	})
	ObjectClass.AddNativeMethod("isNil", func(recv Value, args []Value, v *VM) (Value, error) {
		_, ok := recv.(NilVal)
		return BoolVal{Val: ok}, nil
	})
	ObjectClass.AddNativeMethod("notNil", func(recv Value, args []Value, v *VM) (Value, error) {
		_, ok := recv.(NilVal)
		return BoolVal{Val: !ok}, nil
	})
	ObjectClass.AddNativeMethod("class", func(recv Value, args []Value, v *VM) (Value, error) {
		return recv.Class().AsValue(), nil
	})
	ObjectClass.AddNativeMethod("asJson", func(recv Value, args []Value, v *VM) (Value, error) {
		jsonStr, err := JSONStringify(recv)
		if err != nil {
			return nil, err
		}
		return StringVal{Val: jsonStr}, nil
	})

	// ================= JSON =================
	JSONClass.AddNativeMethod("parse:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("parse:", args, 1); err != nil {
			return nil, err
		}
		return JSONParse(args[0].String())
	})
	JSONClass.AddNativeMethod("stringify:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("stringify:", args, 1); err != nil {
			return nil, err
		}
		jsonStr, err := JSONStringify(args[0])
		if err != nil {
			return nil, err
		}
		return StringVal{Val: jsonStr}, nil
	})

	// Also allow sending parse: / stringify: directly to instance if any
	MetaclassClass.AddNativeMethod("parse:", JSONClass.Methods["parse:"].(*NativeMethod).Fn)
	MetaclassClass.AddNativeMethod("stringify:", JSONClass.Methods["stringify:"].(*NativeMethod).Fn)
	MetaclassClass.AddNativeMethod("new", func(recv Value, args []Value, v *VM) (Value, error) {
		classVal, ok := recv.(*ClassVal)
		if !ok {
			return nil, fmt.Errorf("receiver of 'new' must be a Class")
		}
		if classVal.Klass == ArrayClass {
			return &ArrayVal{Elements: make([]Value, 0)}, nil
		}
		if classVal.Klass == DictClass {
			return &DictVal{Entries: make(map[string]Value)}, nil
		}
		return classVal.Klass.NewInstance(), nil
	})
	MetaclassClass.AddNativeMethod("subclass:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("subclass:", args, 1); err != nil {
			return nil, err
		}
		classVal, ok := recv.(*ClassVal)
		if !ok {
			return nil, fmt.Errorf("receiver of 'subclass:' must be a Class")
		}
		name := args[0].String()
		sub := classVal.Klass.Subclass(name, nil)
		v.Globals.Set(name, sub.AsValue())
		return sub.AsValue(), nil
	})
	MetaclassClass.AddNativeMethod("subclass:vars:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("subclass:vars:", args, 2); err != nil {
			return nil, err
		}
		classVal, ok := recv.(*ClassVal)
		if !ok {
			return nil, fmt.Errorf("receiver of 'subclass:vars:' must be a Class")
		}
		name := args[0].String()
		ivars := extractVarNames(args[1])
		sub := classVal.Klass.Subclass(name, ivars)
		v.Globals.Set(name, sub.AsValue())
		return sub.AsValue(), nil
	})

	// Also support sending new / subclass: / subclass:vars: directly to Object as a Class receiver
	ObjectClass.AddNativeMethod("subclass:", MetaclassClass.Methods["subclass:"].(*NativeMethod).Fn)
	ObjectClass.AddNativeMethod("subclass:vars:", MetaclassClass.Methods["subclass:vars:"].(*NativeMethod).Fn)
	ObjectClass.AddNativeMethod("new", MetaclassClass.Methods["new"].(*NativeMethod).Fn)

	// ================= Boolean =================
	BooleanClass.AddNativeMethod("not", func(recv Value, args []Value, v *VM) (Value, error) {
		return BoolVal{Val: !recv.IsTruthy()}, nil
	})
	BooleanClass.AddNativeMethod("&", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("&", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: recv.IsTruthy() && args[0].IsTruthy()}, nil
	})
	BooleanClass.AddNativeMethod("|", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("|", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: recv.IsTruthy() || args[0].IsTruthy()}, nil
	})
	BooleanClass.AddNativeMethod("ifTrue:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("ifTrue:", args, 1); err != nil {
			return nil, err
		}
		if recv.IsTruthy() {
			return v.EvalBlock(args[0], nil)
		}
		return NilVal{}, nil
	})
	BooleanClass.AddNativeMethod("ifFalse:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("ifFalse:", args, 1); err != nil {
			return nil, err
		}
		if !recv.IsTruthy() {
			return v.EvalBlock(args[0], nil)
		}
		return NilVal{}, nil
	})
	BooleanClass.AddNativeMethod("ifTrue:ifFalse:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("ifTrue:ifFalse:", args, 2); err != nil {
			return nil, err
		}
		if recv.IsTruthy() {
			return v.EvalBlock(args[0], nil)
		}
		return v.EvalBlock(args[1], nil)
	})
	BooleanClass.AddNativeMethod("ifFalse:ifTrue:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("ifFalse:ifTrue:", args, 2); err != nil {
			return nil, err
		}
		if !recv.IsTruthy() {
			return v.EvalBlock(args[0], nil)
		}
		return v.EvalBlock(args[1], nil)
	})

	// Inherit Boolean methods to TrueClass and FalseClass
	for k, m := range BooleanClass.Methods {
		TrueClass.Methods[k] = m
		FalseClass.Methods[k] = m
	}

	// ================= Number / Integer / Float =================
	addArithmeticMethods(IntegerClass)
	addArithmeticMethods(FloatClass)
	addArithmeticMethods(NumberClass)

	// ================= String =================
	StringClass.AddNativeMethod("size", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: int64(len(recv.(StringVal).Val))}, nil
	})
	StringClass.AddNativeMethod("length", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: int64(len(recv.(StringVal).Val))}, nil
	})
	StringClass.AddNativeMethod("+", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("+", args, 1); err != nil {
			return nil, err
		}
		return StringVal{Val: recv.(StringVal).Val + args[0].String()}, nil
	})
	StringClass.AddNativeMethod(",", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount(",", args, 1); err != nil {
			return nil, err
		}
		return StringVal{Val: recv.(StringVal).Val + args[0].String()}, nil
	})
	StringClass.AddNativeMethod("asNumber", func(recv Value, args []Value, v *VM) (Value, error) {
		s := recv.(StringVal).Val
		if i, err := strconv.ParseInt(s, 10, 64); err == nil {
			return IntVal{Val: i}, nil
		}
		if f, err := strconv.ParseFloat(s, 64); err == nil {
			return FloatVal{Val: f}, nil
		}
		return NilVal{}, nil
	})
	StringClass.AddNativeMethod("asInteger", func(recv Value, args []Value, v *VM) (Value, error) {
		s := strings.TrimSpace(recv.(StringVal).Val)
		i, _ := strconv.ParseInt(s, 10, 64)
		return IntVal{Val: i}, nil
	})
	StringClass.AddNativeMethod("asInt", func(recv Value, args []Value, v *VM) (Value, error) {
		s := strings.TrimSpace(recv.(StringVal).Val)
		i, _ := strconv.ParseInt(s, 10, 64)
		return IntVal{Val: i}, nil
	})
	StringClass.AddNativeMethod("asFloat", func(recv Value, args []Value, v *VM) (Value, error) {
		s := strings.TrimSpace(recv.(StringVal).Val)
		f, _ := strconv.ParseFloat(s, 64)
		return FloatVal{Val: f}, nil
	})
	StringClass.AddNativeMethod("parseJson", func(recv Value, args []Value, v *VM) (Value, error) {
		return JSONParse(recv.(StringVal).Val)
	})
	StringClass.AddNativeMethod("split:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("split:", args, 1); err != nil {
			return nil, err
		}
		s := recv.(StringVal).Val
		delim := args[0].String()
		parts := strings.Split(s, delim)
		res := make([]Value, len(parts))
		for i, p := range parts {
			res[i] = StringVal{Val: p}
		}
		return &ArrayVal{Elements: res}, nil
	})
	StringClass.AddNativeMethod("contains:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("contains:", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: strings.Contains(recv.(StringVal).Val, args[0].String())}, nil
	})
	StringClass.AddNativeMethod("startsWith:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("startsWith:", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: strings.HasPrefix(recv.(StringVal).Val, args[0].String())}, nil
	})
	StringClass.AddNativeMethod("endsWith:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("endsWith:", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: strings.HasSuffix(recv.(StringVal).Val, args[0].String())}, nil
	})
	StringClass.AddNativeMethod("trim", func(recv Value, args []Value, v *VM) (Value, error) {
		return StringVal{Val: strings.TrimSpace(recv.(StringVal).Val)}, nil
	})
	StringClass.AddNativeMethod("toUpper", func(recv Value, args []Value, v *VM) (Value, error) {
		return StringVal{Val: strings.ToUpper(recv.(StringVal).Val)}, nil
	})
	StringClass.AddNativeMethod("toLower", func(recv Value, args []Value, v *VM) (Value, error) {
		return StringVal{Val: strings.ToLower(recv.(StringVal).Val)}, nil
	})
	StringClass.AddNativeMethod("replace:with:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("replace:with:", args, 2); err != nil {
			return nil, err
		}
		s := recv.(StringVal).Val
		oldStr := args[0].String()
		newStr := args[1].String()
		return StringVal{Val: strings.ReplaceAll(s, oldStr, newStr)}, nil
	})
	StringClass.AddNativeMethod("substring:to:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("substring:to:", args, 2); err != nil {
			return nil, err
		}
		s := recv.(StringVal).Val
		start := int(toInt(args[0]))
		end := int(toInt(args[1]))
		runes := []rune(s)
		if start < 0 {
			start = 0
		}
		if end > len(runes) {
			end = len(runes)
		}
		if start > end {
			return StringVal{Val: ""}, nil
		}
		return StringVal{Val: string(runes[start:end])}, nil
	})

	// ================= Array =================
	ArrayClass.AddNativeMethod("size", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		return IntVal{Val: int64(len(arr.Elements))}, nil
	})
	ArrayClass.AddNativeMethod("count", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		return IntVal{Val: int64(len(arr.Elements))}, nil
	})
	ArrayClass.AddNativeMethod("length", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		return IntVal{Val: int64(len(arr.Elements))}, nil
	})
	ArrayClass.AddNativeMethod("isEmpty", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		return BoolVal{Val: len(arr.Elements) == 0}, nil
	})
	ArrayClass.AddNativeMethod("first", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		if len(arr.Elements) == 0 {
			return NilVal{}, nil
		}
		return arr.Elements[0], nil
	})
	ArrayClass.AddNativeMethod("last", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		if len(arr.Elements) == 0 {
			return NilVal{}, nil
		}
		return arr.Elements[len(arr.Elements)-1], nil
	})
	ArrayClass.AddNativeMethod("at:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("at:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		idxVal, ok := args[0].(IntVal)
		if !ok {
			return nil, fmt.Errorf("array index must be integer")
		}
		idx := int(idxVal.Val)
		// Smalltalk is 1-based; support 1-based and 0-based
		if idx >= 1 && idx <= len(arr.Elements) {
			return arr.Elements[idx-1], nil
		}
		if idx >= 0 && idx < len(arr.Elements) {
			return arr.Elements[idx], nil
		}
		return NilVal{}, fmt.Errorf("index out of bounds: %d (size: %d)", idx, len(arr.Elements))
	})
	ArrayClass.AddNativeMethod("at:put:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("at:put:", args, 2); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		idxVal, ok := args[0].(IntVal)
		if !ok {
			return nil, fmt.Errorf("array index must be integer")
		}
		idx := int(idxVal.Val)
		if idx >= 1 && idx <= len(arr.Elements) {
			arr.Elements[idx-1] = args[1]
			return args[1], nil
		}
		if idx >= 0 && idx < len(arr.Elements) {
			arr.Elements[idx] = args[1]
			return args[1], nil
		}
		return NilVal{}, fmt.Errorf("index out of bounds: %d", idx)
	})
	ArrayClass.AddNativeMethod("add:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("add:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		arr.Elements = append(arr.Elements, args[0])
		return recv, nil
	})
	ArrayClass.AddNativeMethod("addAll:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("addAll:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		if other, ok := args[0].(*ArrayVal); ok {
			arr.Elements = append(arr.Elements, other.Elements...)
		}
		return recv, nil
	})
	ArrayClass.AddNativeMethod("removeAt:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("removeAt:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		idx := int(toInt(args[0]))
		if idx >= 1 && idx <= len(arr.Elements) {
			idx--
		}
		if idx >= 0 && idx < len(arr.Elements) {
			removed := arr.Elements[idx]
			arr.Elements = append(arr.Elements[:idx], arr.Elements[idx+1:]...)
			return removed, nil
		}
		return NilVal{}, fmt.Errorf("index out of bounds: %d", idx)
	})
	ArrayClass.AddNativeMethod("join:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("join:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		sep := args[0].String()
		parts := make([]string, len(arr.Elements))
		for i, el := range arr.Elements {
			parts[i] = el.String()
		}
		return StringVal{Val: strings.Join(parts, sep)}, nil
	})
	ArrayClass.AddNativeMethod("contains:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("contains:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		target := args[0]
		for _, el := range arr.Elements {
			if ValuesEqual(el, target) {
				return BoolVal{Val: true}, nil
			}
		}
		return BoolVal{Val: false}, nil
	})
	ArrayClass.AddNativeMethod("indexOf:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("indexOf:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		target := args[0]
		for i, el := range arr.Elements {
			if ValuesEqual(el, target) {
				return IntVal{Val: int64(i + 1)}, nil // 1-based Smalltalk index
			}
		}
		return IntVal{Val: 0}, nil
	})
	ArrayClass.AddNativeMethod("reversed", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		res := make([]Value, len(arr.Elements))
		for i, el := range arr.Elements {
			res[len(arr.Elements)-1-i] = el
		}
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("reverse", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		res := make([]Value, len(arr.Elements))
		for i, el := range arr.Elements {
			res[len(arr.Elements)-1-i] = el
		}
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("slice:to:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("slice:to:", args, 2); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		from := int(toInt(args[0]))
		to := int(toInt(args[1]))
		if from >= 1 {
			from--
		}
		if to > len(arr.Elements) {
			to = len(arr.Elements)
		}
		if from < 0 {
			from = 0
		}
		if from > to {
			return &ArrayVal{Elements: nil}, nil
		}
		res := make([]Value, to-from)
		copy(res, arr.Elements[from:to])
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("select:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("select:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		block := args[0]
		res := make([]Value, 0)
		for _, el := range arr.Elements {
			cond, err := v.EvalBlock(block, []Value{el})
			if err != nil {
				return nil, err
			}
			if cond.IsTruthy() {
				res = append(res, el)
			}
		}
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("reject:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("reject:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		block := args[0]
		res := make([]Value, 0)
		for _, el := range arr.Elements {
			cond, err := v.EvalBlock(block, []Value{el})
			if err != nil {
				return nil, err
			}
			if !cond.IsTruthy() {
				res = append(res, el)
			}
		}
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("collect:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("collect:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		block := args[0]
		res := make([]Value, len(arr.Elements))
		for i, el := range arr.Elements {
			mapped, err := v.EvalBlock(block, []Value{el})
			if err != nil {
				return nil, err
			}
			res[i] = mapped
		}
		return &ArrayVal{Elements: res}, nil
	})
	ArrayClass.AddNativeMethod("inject:into:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("inject:into:", args, 2); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		acc := args[0]
		block := args[1]
		for _, el := range arr.Elements {
			var err error
			acc, err = v.EvalBlock(block, []Value{acc, el})
			if err != nil {
				return nil, err
			}
		}
		return acc, nil
	})
	ArrayClass.AddNativeMethod("sortBy:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("sortBy:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		block := args[0]
		sorted := make([]Value, len(arr.Elements))
		copy(sorted, arr.Elements)

		var sortErr error
		sort.SliceStable(sorted, func(i, j int) bool {
			if sortErr != nil {
				return false
			}
			res, err := v.EvalBlock(block, []Value{sorted[i], sorted[j]})
			if err != nil {
				sortErr = err
				return false
			}
			return res.IsTruthy()
		})
		if sortErr != nil {
			return nil, sortErr
		}
		return &ArrayVal{Elements: sorted}, nil
	})
	ArrayClass.AddNativeMethod("sorted", func(recv Value, args []Value, v *VM) (Value, error) {
		arr := recv.(*ArrayVal)
		sorted := make([]Value, len(arr.Elements))
		copy(sorted, arr.Elements)
		sort.SliceStable(sorted, func(i, j int) bool {
			cmp, _ := v.SendMessage(sorted[i], "<", []Value{sorted[j]})
			if cmp != nil {
				return cmp.IsTruthy()
			}
			return sorted[i].String() < sorted[j].String()
		})
		return &ArrayVal{Elements: sorted}, nil
	})
	ArrayClass.AddNativeMethod("do:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("do:", args, 1); err != nil {
			return nil, err
		}
		arr := recv.(*ArrayVal)
		block := args[0]
		for _, el := range arr.Elements {
			if _, err := v.EvalBlock(block, []Value{el}); err != nil {
				return nil, err
			}
		}
		return recv, nil
	})

	// ================= Dictionary =================
	DictClass.AddNativeMethod("size", func(recv Value, args []Value, v *VM) (Value, error) {
		dict := recv.(*DictVal)
		return IntVal{Val: int64(len(dict.Entries))}, nil
	})
	DictClass.AddNativeMethod("count", func(recv Value, args []Value, v *VM) (Value, error) {
		dict := recv.(*DictVal)
		return IntVal{Val: int64(len(dict.Entries))}, nil
	})
	DictClass.AddNativeMethod("isEmpty", func(recv Value, args []Value, v *VM) (Value, error) {
		dict := recv.(*DictVal)
		return BoolVal{Val: len(dict.Entries) == 0}, nil
	})
	DictClass.AddNativeMethod("at:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("at:", args, 1); err != nil {
			return nil, err
		}
		dict := recv.(*DictVal)
		key := args[0].String()
		if val, ok := dict.Entries[key]; ok {
			return val, nil
		}
		return NilVal{}, nil
	})
	DictClass.AddNativeMethod("at:put:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("at:put:", args, 2); err != nil {
			return nil, err
		}
		dict := recv.(*DictVal)
		key := args[0].String()
		dict.Entries[key] = args[1]
		return args[1], nil
	})
	DictClass.AddNativeMethod("hasKey:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("hasKey:", args, 1); err != nil {
			return nil, err
		}
		dict := recv.(*DictVal)
		_, ok := dict.Entries[args[0].String()]
		return BoolVal{Val: ok}, nil
	})
	DictClass.AddNativeMethod("keys", func(recv Value, args []Value, v *VM) (Value, error) {
		dict := recv.(*DictVal)
		keys := make([]Value, 0, len(dict.Entries))
		for k := range dict.Entries {
			keys = append(keys, StringVal{Val: k})
		}
		return &ArrayVal{Elements: keys}, nil
	})
	DictClass.AddNativeMethod("values", func(recv Value, args []Value, v *VM) (Value, error) {
		dict := recv.(*DictVal)
		vals := make([]Value, 0, len(dict.Entries))
		for _, val := range dict.Entries {
			vals = append(vals, val)
		}
		return &ArrayVal{Elements: vals}, nil
	})

	// ================= BlockClosure =================
	BlockClosureClass.AddNativeMethod("value", func(recv Value, args []Value, v *VM) (Value, error) {
		return v.EvalBlock(recv, nil)
	})
	BlockClosureClass.AddNativeMethod("value:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("value:", args, 1); err != nil {
			return nil, err
		}
		return v.EvalBlock(recv, args)
	})
	BlockClosureClass.AddNativeMethod("value:value:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("value:value:", args, 2); err != nil {
			return nil, err
		}
		return v.EvalBlock(recv, args)
	})
	BlockClosureClass.AddNativeMethod("whileTrue:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("whileTrue:", args, 1); err != nil {
			return nil, err
		}
		bodyBlock := args[0]
		var lastVal Value = NilVal{}
		for {
			cond, err := v.EvalBlock(recv, nil)
			if err != nil {
				return nil, err
			}
			if !cond.IsTruthy() {
				break
			}
			lastVal, err = v.EvalBlock(bodyBlock, nil)
			if err != nil {
				return nil, err
			}
		}
		return lastVal, nil
	})
}

func addArithmeticMethods(c *Class) {
	c.AddNativeMethod("+", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("+", args, 1); err != nil {
			return nil, err
		}
		if isInt(recv) && isInt(args[0]) {
			return IntVal{Val: toInt(recv) + toInt(args[0])}, nil
		}
		return FloatVal{Val: toFloat(recv) + toFloat(args[0])}, nil
	})
	c.AddNativeMethod("-", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("-", args, 1); err != nil {
			return nil, err
		}
		if isInt(recv) && isInt(args[0]) {
			return IntVal{Val: toInt(recv) - toInt(args[0])}, nil
		}
		return FloatVal{Val: toFloat(recv) - toFloat(args[0])}, nil
	})
	c.AddNativeMethod("*", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("*", args, 1); err != nil {
			return nil, err
		}
		if isInt(recv) && isInt(args[0]) {
			return IntVal{Val: toInt(recv) * toInt(args[0])}, nil
		}
		return FloatVal{Val: toFloat(recv) * toFloat(args[0])}, nil
	})
	c.AddNativeMethod("/", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("/", args, 1); err != nil {
			return nil, err
		}
		denom := toFloat(args[0])
		if denom == 0 {
			return nil, fmt.Errorf("division by zero")
		}
		if isInt(recv) && isInt(args[0]) && toInt(args[0]) != 0 && toInt(recv)%toInt(args[0]) == 0 {
			return IntVal{Val: toInt(recv) / toInt(args[0])}, nil
		}
		return FloatVal{Val: toFloat(recv) / denom}, nil
	})
	c.AddNativeMethod("%", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("%", args, 1); err != nil {
			return nil, err
		}
		return IntVal{Val: toInt(recv) % toInt(args[0])}, nil
	})
	c.AddNativeMethod("<", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("<", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: toFloat(recv) < toFloat(args[0])}, nil
	})
	c.AddNativeMethod(">", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount(">", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: toFloat(recv) > toFloat(args[0])}, nil
	})
	c.AddNativeMethod("<=", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("<=", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: toFloat(recv) <= toFloat(args[0])}, nil
	})
	c.AddNativeMethod(">=", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount(">=", args, 1); err != nil {
			return nil, err
		}
		return BoolVal{Val: toFloat(recv) >= toFloat(args[0])}, nil
	})
	c.AddNativeMethod("abs", func(recv Value, args []Value, v *VM) (Value, error) {
		if isInt(recv) {
			val := toInt(recv)
			if val < 0 {
				val = -val
			}
			return IntVal{Val: val}, nil
		}
		return FloatVal{Val: math.Abs(toFloat(recv))}, nil
	})
	c.AddNativeMethod("negated", func(recv Value, args []Value, v *VM) (Value, error) {
		if isInt(recv) {
			return IntVal{Val: -toInt(recv)}, nil
		}
		return FloatVal{Val: -toFloat(recv)}, nil
	})
	c.AddNativeMethod("sqrt", func(recv Value, args []Value, v *VM) (Value, error) {
		return FloatVal{Val: math.Sqrt(toFloat(recv))}, nil
	})
	c.AddNativeMethod("round", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: int64(math.Round(toFloat(recv)))}, nil
	})
	c.AddNativeMethod("floor", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: int64(math.Floor(toFloat(recv)))}, nil
	})
	c.AddNativeMethod("ceil", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: int64(math.Ceil(toFloat(recv)))}, nil
	})
	c.AddNativeMethod("asFloat", func(recv Value, args []Value, v *VM) (Value, error) {
		return FloatVal{Val: toFloat(recv)}, nil
	})
	c.AddNativeMethod("asInteger", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: toInt(recv)}, nil
	})
	c.AddNativeMethod("asInt", func(recv Value, args []Value, v *VM) (Value, error) {
		return IntVal{Val: toInt(recv)}, nil
	})
	c.AddNativeMethod("min:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("min:", args, 1); err != nil {
			return nil, err
		}
		if toFloat(recv) < toFloat(args[0]) {
			return recv, nil
		}
		return args[0], nil
	})
	c.AddNativeMethod("max:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("max:", args, 1); err != nil {
			return nil, err
		}
		if toFloat(recv) > toFloat(args[0]) {
			return recv, nil
		}
		return args[0], nil
	})
	c.AddNativeMethod("pow:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("pow:", args, 1); err != nil {
			return nil, err
		}
		return FloatVal{Val: math.Pow(toFloat(recv), toFloat(args[0]))}, nil
	})
	c.AddNativeMethod("timesRepeat:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("timesRepeat:", args, 1); err != nil {
			return nil, err
		}
		count := toInt(recv)
		block := args[0]
		for i := int64(0); i < count; i++ {
			if _, err := v.EvalBlock(block, nil); err != nil {
				return nil, err
			}
		}
		return recv, nil
	})
	c.AddNativeMethod("to:do:", func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("to:do:", args, 2); err != nil {
			return nil, err
		}
		start := toInt(recv)
		end := toInt(args[0])
		block := args[1]
		for i := start; i <= end; i++ {
			if _, err := v.EvalBlock(block, []Value{IntVal{Val: i}}); err != nil {
				return nil, err
			}
		}
		return recv, nil
	})
}

func isInt(v Value) bool {
	_, ok := v.(IntVal)
	return ok
}

func toInt(v Value) int64 {
	switch val := v.(type) {
	case IntVal:
		return val.Val
	case FloatVal:
		return int64(val.Val)
	default:
		return 0
	}
}

func toFloat(v Value) float64 {
	switch val := v.(type) {
	case IntVal:
		return float64(val.Val)
	case FloatVal:
		return val.Val
	default:
		return 0.0
	}
}

func ValuesEqual(a, b Value) bool {
	if a == nil && b == nil {
		return true
	}
	if a == nil || b == nil {
		return false
	}
	switch va := a.(type) {
	case NilVal:
		_, ok := b.(NilVal)
		return ok
	case BoolVal:
		vb, ok := b.(BoolVal)
		return ok && va.Val == vb.Val
	case IntVal:
		if vb, ok := b.(IntVal); ok {
			return va.Val == vb.Val
		}
		if vb, ok := b.(FloatVal); ok {
			return float64(va.Val) == vb.Val
		}
		return false
	case FloatVal:
		if vb, ok := b.(FloatVal); ok {
			return va.Val == vb.Val
		}
		if vb, ok := b.(IntVal); ok {
			return va.Val == float64(vb.Val)
		}
		return false
	case StringVal:
		vb, ok := b.(StringVal)
		return ok && va.Val == vb.Val
	default:
		return a == b
	}
}

func extractVarNames(val Value) []string {
	names := make([]string, 0)
	if blk, ok := val.(*BlockVal); ok {
		var collect func(node ast.Node)
		collect = func(node ast.Node) {
			if node == nil {
				return
			}
			switch n := node.(type) {
			case *ast.Program:
				for _, s := range n.Statements {
					collect(s)
				}
			case *ast.ExprStmt:
				collect(n.Expression)
			case *ast.Pipeline:
				collect(n.First)
				for _, step := range n.Steps {
					collect(step)
				}
			case *ast.Cascade:
				collect(n.FirstMsg)
				for _, m := range n.Messages {
					collect(m)
				}
			case *ast.MessageSend:
				if n.Receiver != nil {
					collect(n.Receiver)
				}
				if n.Msg != nil {
					collect(n.Msg)
				}
			case *ast.UnaryMessage:
				names = append(names, n.Selector)
			case *ast.BinaryMessage:
				collect(n.Argument)
			case *ast.KeywordMessage:
				for i, kw := range n.Keywords {
					names = append(names, kw)
					if i < len(n.Args) {
						collect(n.Args[i])
					}
				}
			case *ast.IdentifierExpr:
				names = append(names, n.Name)
			case *ast.LiteralExpr:
				if n.Kind == ast.LIT_STRING {
					names = append(names, n.StrValue)
				}
			}
		}
		collect(blk.Body)
		return names
	}
	if arr, ok := val.(*ArrayVal); ok {
		for _, el := range arr.Elements {
			names = append(names, el.String())
		}
		return names
	}
	return []string{val.String()}
}
