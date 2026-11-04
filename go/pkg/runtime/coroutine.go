package runtime

import (
	"fmt"
	"sync"
)

var CoroutineClass *Class

func init() {
	CoroutineClass = NewClass("coroutine", ObjectClass, nil)
}

type CoroutineVal struct {
	Block      *BlockVal
	VM         *VM
	Status     string // "suspended", "running", "dead"
	ResumeChan chan Value
	YieldChan  chan Value
	DoneChan   chan struct{}
	Started    bool
	LastArg    Value
	mu         sync.Mutex
}

func (c *CoroutineVal) Type() string { return "coroutine" }
func (c *CoroutineVal) String() string {
	return fmt.Sprintf("<coroutine: %s>", c.Status)
}
func (c *CoroutineVal) Class() *Class  { return CoroutineClass }
func (c *CoroutineVal) IsTruthy() bool { return c.Status != "dead" }

func NewCoroutineVal(block *BlockVal, vm *VM) *CoroutineVal {
	return &CoroutineVal{
		Block:      block,
		VM:         vm,
		Status:     "suspended",
		ResumeChan: make(chan Value),
		YieldChan:  make(chan Value),
		DoneChan:   make(chan struct{}),
		Started:    false,
	}
}

func (c *CoroutineVal) Resume(arg Value) (Value, error) {
	c.mu.Lock()
	c.LastArg = arg
	if c.Status == "dead" {
		c.mu.Unlock()
		return NilVal{}, fmt.Errorf("cannot resume dead coroutine")
	}

	if !c.Started {
		c.Started = true
		c.Status = "running"
		c.mu.Unlock()

		go func() {
			// Save current active coroutine in VM context
			prevCoro := c.VM.ActiveCoro
			c.VM.ActiveCoro = c

			// Wait for initial resume trigger
			initialArg := <-c.ResumeChan

			var args []Value
			if initialArg != nil && initialArg.Type() != "nil" {
				args = []Value{initialArg}
			}

			// Run block
			res, err := c.VM.EvalBlock(c.Block, args)
			c.VM.ActiveCoro = prevCoro

			c.mu.Lock()
			c.Status = "dead"
			c.mu.Unlock()

			close(c.DoneChan)
			if err != nil {
				if sig, ok := err.(*ReturnSignal); ok {
					c.YieldChan <- sig.Value
				} else {
					c.YieldChan <- NilVal{}
				}
			} else {
				c.YieldChan <- res
			}
		}()

		c.ResumeChan <- arg
		val := <-c.YieldChan
		return val, nil
	}

	c.Status = "running"
	c.mu.Unlock()

	c.ResumeChan <- arg
	val := <-c.YieldChan
	return val, nil
}

func (c *CoroutineVal) Yield(val Value) (Value, error) {
	c.mu.Lock()
	c.Status = "suspended"
	c.mu.Unlock()

	// Send yielded value to resumer
	c.YieldChan <- val
	// Wait for next resume
	nextArg, ok := <-c.ResumeChan
	if !ok {
		return NilVal{}, nil
	}
	return nextArg, nil
}

func InitCoroutineMethods(vm *VM) {
	// Class constructor: coroutine create: block / coroutine new: block
	createFn := func(recv Value, args []Value, v *VM) (Value, error) {
		if err := CheckArgCount("create:", args, 1); err != nil {
			return nil, err
		}
		blk, ok := args[0].(*BlockVal)
		if !ok {
			return nil, fmt.Errorf("coroutine create requires a block argument")
		}
		return NewCoroutineVal(blk, v), nil
	}
	CoroutineClass.AddNativeMethod("create:", createFn)
	CoroutineClass.AddNativeMethod("new:", createFn)
	MetaclassClass.AddNativeMethod("create:", createFn)

	// Class method: coroutine yield: val
	yieldFn := func(recv Value, args []Value, v *VM) (Value, error) {
		if v.ActiveCoro == nil {
			return nil, fmt.Errorf("cannot yield outside of a running coroutine")
		}
		var val Value = NilVal{}
		if len(args) > 0 {
			val = args[0]
		}
		return v.ActiveCoro.Yield(val)
	}
	CoroutineClass.AddNativeMethod("yield:", yieldFn)
	CoroutineClass.AddNativeMethod("yield", func(recv Value, args []Value, v *VM) (Value, error) {
		return yieldFn(recv, []Value{NilVal{}}, v)
	})
	CoroutineClass.AddNativeMethod("arg", func(recv Value, args []Value, v *VM) (Value, error) {
		if v.ActiveCoro == nil {
			return NilVal{}, nil
		}
		if v.ActiveCoro.LastArg == nil {
			return NilVal{}, nil
		}
		return v.ActiveCoro.LastArg, nil
	})
	MetaclassClass.AddNativeMethod("yield:", yieldFn)
	MetaclassClass.AddNativeMethod("arg", func(recv Value, args []Value, v *VM) (Value, error) {
		if v.ActiveCoro == nil {
			return NilVal{}, nil
		}
		if v.ActiveCoro.LastArg == nil {
			return NilVal{}, nil
		}
		return v.ActiveCoro.LastArg, nil
	})

	// Instance method: co resume / co resume: arg
	CoroutineClass.AddNativeMethod("resume", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		return co.Resume(NilVal{})
	})
	CoroutineClass.AddNativeMethod("resume:", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		var arg Value = NilVal{}
		if len(args) > 0 {
			arg = args[0]
		}
		return co.Resume(arg)
	})
	CoroutineClass.AddNativeMethod("next", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		return co.Resume(NilVal{})
	})
	CoroutineClass.AddNativeMethod("status", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		return StringVal{Val: co.Status}, nil
	})
	CoroutineClass.AddNativeMethod("isDone", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		return BoolVal{Val: co.Status == "dead"}, nil
	})
	CoroutineClass.AddNativeMethod("isDead", func(recv Value, args []Value, v *VM) (Value, error) {
		co, ok := recv.(*CoroutineVal)
		if !ok {
			return nil, fmt.Errorf("receiver must be a coroutine")
		}
		return BoolVal{Val: co.Status == "dead"}, nil
	})
}
