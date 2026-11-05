# Tick

🌐 **[Interactive WebAssembly Playground](https://xyzzyapps.github.io/tick/)** &bull; 📦 **[Download Precompiled Binaries (v1.0)](https://github.com/xyzzyapps/tick/releases/tag/v1.0)**

**Tick** is a strict, zero-filler synthesis of Smalltalk’s pure object-oriented message passing and Tcl’s command-token structure, designed as an embeddable, featherweight scripting language for rapid prototyping (akin to Lua, Tcl, or Lisp). Every word in a statement is a receiver, a selector keyword, or an argument—with zero decorative prepositions or redundant punctuation.

This repository provides **two complete, independent reference implementations**:
1. **Golang Implementation** (`go/`)
2. **C99 Implementation** (`c/`)

Both implementations support the full language grammar, pure Smalltalk message passing, closures with non-local returns, comma cascades, fluent pipelines, string interpolation, **stackless Protothreads coroutines**, and **native Foreign Function Interface (FFI) via `@` sigilled variables and dynamic DLL calls**.

---

## Core Syntax Rules

1. **Command-Line Statements**: A newline terminates a statement (replacing Smalltalk’s `.`). The first token is always the **Receiver**, followed by:
   - A **Unary Selector**: `window close`
   - A **Binary Selector**: `a + b`, `x * y`
   - Alternating **Keyword-Argument pairs**: `dict at "a" put 1` (compiles directly to `#at:put:`)

2. **Inline Evaluation `[ ... ]`**: Borrowed from Tcl command substitution, brackets evaluate nested expressions inline and return their result (replacing Smalltalk’s parentheses `(...)`).

3. **Code Blocks `{ params | body }`**: Borrowed from Tcl braces, `{ ... }` defines an unevaluated Smalltalk block closure. Optional parameters are declared at the start of the block followed by `|`.

4. **Fluent Pipelines (`|`) & Comma Cascades (`,`)**:
   - **Pipe (`|`)**: Chains the return value of the left message as the receiver of the right message (Ruby-style fluent chaining without bracket creep).
   - **Comma (`,`)**: Sends the next message to the original receiver, reading like a natural English list of actions on a single subject (`logger info "Started", record "count" value 1, flush`).

5. **Special Forms & Interpolation**:
   - `set var value`: Handles variable and instance variable assignment (`:=`).
   - `return value`: Handles early and non-local returns (`^`).
   - Double-quoted strings support Tcl variable, FFI, and command interpolation:
     `"Hello $name, you have [ inbox count ] messages"`

6. **FFI Engine (`@` Sigil)**:
   - Access host & FFI variables: `@PI`, `@E`, `@OS`, `@ARCH`, `@VERSION`
   - Host function calls: `@puts`, `@printf`, `@sqrt`, `@sin`, `@cos`, `@time`, `@clock`, `@getenv`, `@setenv`, `@system`, `@json_parse`, `@json_stringify`
   - Dynamic library calling: `@call "msvcrt.dll" "puts" "text"` or `@libc:puts "text"`
   - Piping to FFI: `"Hello" | @puts`

7. **JSON & Rich Data Types**:
   - Built-in JSON: `JSON parse string`, `JSON stringify obj`, `obj asJson`, `str parseJson`
   - Lowercase Type Names: Use `integer`, `float`, `string`, `list`, `dict`, `boolean`, `number` naturally (e.g. `integer method fact { ... }`) alongside capital aliases.
   - Lists (`list` / `Array`): `add:`, `addAll:`, `removeAt:`, `join:`, `contains:`, `indexOf:`, `reverse`, `reversed`, `slice:to:`, `select:`, `reject:`, `collect:`, `inject:into:`, `sortBy:`
   - Numbers (`integer` & `float`): Full math, `round`, `floor`, `ceil`, `min:`, `max:`, `pow:`, `asFloat`, `asInt`, `asInteger`
   - Strings: `split:`, `contains:`, `startsWith:`, `endsWith:`, `substring:to:`, `trim`, `toUpper`, `toLower`, `replace:with:`
   - Full Recursion: Recursive methods, mutual recursion, and recursive block closures with call-frame unwinding.

8. **First-Class Coroutines**:
   - **Protothreads in C**: Implemented via Adam Dunkels' stackless local continuations (`pt.h`).
   - **Channels in Go**: Implemented via Goroutine channel pairs with non-blocking coordination.
   - Two-way communication: `co resume arg` delivers arguments directly into `coroutine yield:` or `coroutine arg`.
   - Inspection: `co status` (`"suspended"`, `"running"`, `"dead"`), `co isDone`.

---

## Object-Oriented Principles in Tick

Tick is a **pure object-oriented programming language**, faithfully implementing the core principles established by Alan Kay and Smalltalk-80, expressed through Tcl’s lightweight command-token syntax:

### 1. Everything is an Object
There are no primitive unboxed types. Numbers (`integer`, `float`), strings, booleans (`true`, `false`), blocks (`{ ... }`), arrays/lists, dictionaries, coroutines, and classes themselves are all first-class objects inheriting from `Object`:
- Even control flow is object messaging: `[count > 0] ifTrue { ... } ifFalse { ... }` sends `#ifTrue:ifFalse:` to a `Boolean` object (`True` or `False`).
- Arithmetic is object messaging: `3 + 4` sends the binary message `+` with argument `4` to the `Integer` receiver `3`.

### 2. Classes & Dynamic Subclassing
Classes are first-class runtime objects (instances of `Metaclass`). Any class can be subclassed dynamically:
```tcl
# Define a base class with instance variables
Object subclass Animal vars { name sound }

# Subclassing inherits all state and behavior
Animal subclass Dog vars { breed }
Animal subclass Cat vars { lives }
```

### 3. Encapsulation & Instance Variables
Instance variables are private to each instance. They can be read and mutated within the methods of the class hierarchy, but cannot be accessed directly from the outside:
```tcl
# Method definition with keyword parameters
Animal method init nameVal sound soundVal {
    set name nameVal
    set sound soundVal
}

Animal method speak {
    "$name says: $sound" println
}
```
> **Assignment Note**: Tick uses the clean keyword `set` for all variable and instance variable assignment (`set name nameVal`).

### 4. Polymorphism & Dynamic Dispatch
Method resolution is late-bound and determined at runtime based on the actual class of the receiver:
```tcl
Dog method initDog nameVal breed breedVal {
    set name nameVal
    set sound "Woof!"
    set breed breedVal
}

Dog method speak {
    "$name the $breed barks: $sound" println
}

Cat method initCat nameVal {
    set name nameVal
    set sound "Meow!"
    set lives 9
}

Cat method speak {
    "$name purrs: $sound (lives: $lives)" println
}

# Polymorphic invocation:
set animals [Array new , add [Dog new , initDog "Rover" breed "Collie"] , add [Cat new , initCat "Luna"]]
animals do { a | a speak }
```

### 5. `self` and Comma Cascades
Inside any method, `self` refers to the active receiver. If a method does not explicitly return a value with `return`, it implicitly returns `self`. This enables natural Smalltalk-style message cascading with commas:
```tcl
set dog [
    Dog new
    , initDog "Rover" breed "Collie"
    , speak
]
```

### 6. Open Classes (Extensibility)
Classes in Tick are open—new methods can be added to existing user-defined classes or core built-in classes at any time:
```tcl
# Extend built-in integers with a custom method
integer method square {
    return [self * self]
}

"5 squared = [5 square]" println   # => 25
```

---

## Side-by-Side Reference

| Feature | Standard Smalltalk | Tick |
| :--- | :--- | :--- |
| **Assignment** | `total := 0.` | `set total 0` |
| **Unary Message** | `server start.` | `server start` |
| **Keyword Message** | `canvas draw: shape at: origin.` | `canvas draw shape at origin` |
| **Sub-expression** | `x := a + (b factorial).` | `set x [a + [b factorial]]` |
| **Zero-Arg Block** | `[ self alert ]` | `{ self alert }` |
| **Parameterized Block** | `[ :x :y \| x + y ]` | `{ x y \| x + y }` |
| **Fluent Pipeline** | `((items select: [:x \| x valid]) collect: [:x \| x name]) sorted.` | `items select { x \| x valid } \| collect { x \| x name } \| sorted` |
| **Message Cascade** | `pen up; color: "blue"; down.` | `pen up, color "blue", down` |

### Collections & Data Structures (Smalltalk vs. Tick)

Tick preserves Smalltalk's pure object-oriented message passing for collections while modernizing ergonomics:
- **No clumsy semicolons or `yourself`**: Comma cascades (`,`) naturally chain mutations on receivers without boilerplate.
- **No nested parentheses for functional transforms**: Fluent pipelines (`|`) channel data effortlessly through `select`, `reject`, `collect`, `inject:into:`, and `sortBy:`.
- **First-class JSON**: Native `asJson`, `JSON parse`, and `JSON stringify` eliminate reliance on external libraries like NeoJSON.

#### 1. Lists & Ordered Collections (`Array` / `List`)

| Collection Operation | Standard Smalltalk (Smalltalk-80 / Pharo) | Tick |
| :--- | :--- | :--- |
| **Instantiation** | `list := OrderedCollection new.` | `set list [List new]` *(or `Array new`)* |
| **Append Item** | `list add: 42.` | `list add 42` |
| **Cascade Appends** | `list add: 1; add: 2; add: 3; yourself.` | `list add 1, add 2, add 3` |
| **Access by Index** | `elem := list at: 1.` *(1-based)* | `set elem [list at 1]` *(1-based; 0 also accepted as alias for 1st item)* |
| **Mutate by Index** | `list at: 1 put: 99.` | `list at 1 put 99` |
| **First / Last** | `list first. list last.` | `list first` / `list last` |
| **Size / Length** | `list size.` | `list size` *(or `count`, `length`)* |
| **Check Empty** | `list isEmpty.` | `list isEmpty` |
| **Contains Item** | `list includes: 42.` | `list contains 42` |
| **Index of Item** | `list indexOf: 42.` | `list indexOf 42` |
| **Iteration (`do:`)** | `list do: [:x \| Transcript show: x printString].` | `list do { x \| x println }` |
| **Map / Transform** | `doubles := list collect: [:x \| x * 2].` | `set doubles [list collect { x \| x * 2 }]` |
| **Filter (Select)** | `evens := list select: [:x \| x even].` | `set evens [list select { x \| [x % 2] == 0 }]` |
| **Filter (Reject)** | `odds := list reject: [:x \| x even].` | `set odds [list reject { x \| [x % 2] == 0 }]` |
| **Fold / Reduce** | `sum := list inject: 0 into: [:acc :x \| acc + x].` | `set sum [list inject 0 into { acc x \| acc + x }]` |
| **Custom Sort** | `sorted := list asSortedCollection: [:a :b \| a < b].` | `set sorted [list sortBy { a b \| a < b }]` |
| **Natural Sort** | `sorted := list asSortedCollection.` | `set sorted [list sorted]` |
| **Reverse** | `list reversed.` | `list reversed` *(or `reverse`)* |
| **Slice / Sub-list** | `list copyFrom: 1 to: 3.` | `list slice 0 to 3` |
| **Join into String** | `(list collect: #asString) joinSeparatedBy: ', '.` | `list join ", "` |
| **Fluent Pipeline** | `((list select: [:x \| x > 10]) collect: [:x \| x * 2]) sorted.` | `list select { x \| x > 10 } \| collect { x \| x * 2 } \| sorted` |

#### 2. Dictionaries & Key-Value Maps (`Dictionary` / `dict`)

| Dictionary Operation | Standard Smalltalk (Smalltalk-80 / Pharo) | Tick |
| :--- | :--- | :--- |
| **Instantiation** | `dict := Dictionary new.` | `set dict [Dictionary new]` *(or `dict new`)* |
| **Store Key-Value** | `dict at: 'name' put: 'Alice'.` | `dict at "name" put "Alice"` |
| **Lookup Key** | `val := dict at: 'name'.` | `set val [dict at "name"]` |
| **Cascade Inserts** | `dict at: 'a' put: 1; at: 'b' put: 2; yourself.` | `dict at "a" put 1, at "b" put 2` |
| **Check Key Exists**| `dict includesKey: 'name'.` | `dict hasKey "name"` |
| **All Keys** | `dict keys.` | `dict keys` *(returns Array)* |
| **All Values** | `dict values.` | `dict values` *(returns Array)* |
| **Size / Count** | `dict size.` | `dict size` *(or `count`)* |
| **Check Empty** | `dict isEmpty.` | `dict isEmpty` |
| **JSON Export** | *(Requires external NeoJSON library)* | `dict asJson` *(or `JSON stringify dict`)* |
| **JSON Import** | *(Requires external NeoJSON library)* | `set dict [JSON parse '{"name": "Alice"}']` |

#### 3. Real-World Data Pipeline Example

```tcl
# Create a list of student records using cascading dictionaries
set students [List new
    , add [Dictionary new , at "name" put "Alice" , at "score" put 92 , at "active" put true]
    , add [Dictionary new , at "name" put "Bob"   , at "score" put 65 , at "active" put true]
    , add [Dictionary new , at "name" put "Carol" , at "score" put 88 , at "active" put false]
    , add [Dictionary new , at "name" put "Dave"  , at "score" put 95 , at "active" put true]
]

# Pipeline: filter active students scoring >= 80, map to names, and sort alphabetically
set honorRoll [
    students select { s | [s at "active"] & [[s at "score"] >= 80] }
    | collect { s | s at "name" }
    | sorted
]

set names [honorRoll join ", "]
"Honor Roll: $names" println
# => Honor Roll: Alice, Dave

# Native JSON Serialization
set jsonOutput [students asJson]
"Exported JSON: $jsonOutput" println
```

---

## Complete Language Showcase

```tcl
# 1. Class & Method Definition
Object subclass OrderProcessor vars { taxRate logger }

OrderProcessor method initializeWithTax rate logger log {
    set taxRate rate
    set logger log
}

OrderProcessor method processOrders orders onError errorBlock {
    # 2. Multi-line fluent pipeline with { param | body } blocks
    set validOrders [
        orders select { o | o isPaid }
        | reject { o | o isCancelled }
        | sortBy { a b | [a timestamp] < [b timestamp] }
    ]

    # 3. Conditional with multi-keyword selector (#ifTrue:ifFalse:)
    [validOrders isEmpty] ifTrue {
        errorBlock value "No valid orders found"
        return 0
    } ifFalse {
        logger info "Processing [validOrders size] orders"
    }

    # 4. Fold/Inject Pipeline
    set grandTotal [
        validOrders collect { o | [o amount] * [1 + taxRate] }
        | inject 0 into { sum val | sum + val }
    ]

    # 5. Multi-line Comma Cascade (sends all three messages to logger)
    logger info "Batch complete",
           recordMetric "revenue" value grandTotal,
           flush

    return grandTotal
}

# 6. Mixing Cascades (,) and Pipelines (|)
set window [
    WindowBuilder new
    , title "Order Dashboard"
    , width 1024
    , height 768
    | build
]
```

---

## Formal EBNF Grammar

```ebnf
Program       ::= (Statement? NEWLINE)*
Statement     ::= Assignment | Return | MethodDef | Pipeline

Assignment    ::= "set" (IDENTIFIER | SIGIL_FFI) Expression
Return        ::= "return" Expression?
MethodDef     ::= Primary "method" MethodSignature Block

Pipeline      ::= Cascade ( ("|" | NEWLINE "|") Cascade )*
Cascade       ::= MessageSend ( ("," | "," NEWLINE) Message )*

MessageSend   ::= Primary Message?
Message       ::= IDENTIFIER                         /* Unary:   window close */
                | BINARY_OP Primary                  /* Binary:  a + b        */
                | (IDENTIFIER Primary)+              /* Keyword: at "a" put 1 */

Primary       ::= IDENTIFIER | LITERAL | SIGIL_FFI | SubExpr | Block

SubExpr       ::= "[" Pipeline "]"
Block         ::= "{" BlockParams? Program "}"
BlockParams   ::= IDENTIFIER+ "|"
```

---

## Foreign Function Interface (FFI)

Variables and function calls with the `@` sigil directly interact with the host runtime or dynamic C libraries:

```tcl
# 1. FFI Variables
set pi @PI
@puts "Current OS: @OS, Arch: @ARCH, Pi: $pi"

# 2. Mathematical Calls
set root [@sqrt 144]
set sine [@sin [pi / 2]]
set pow2 [@pow 2 10]

# 3. System & Time
set now [@time]
set user [@getenv "USERNAME"]
@printf "Current user: %s (timestamp: %d)\n" user now

# 4. Dynamic DLL / Shared Library Calls
@call "msvcrt.dll" "puts" "Direct dynamic C invocation!"

# 5. Piping into FFI
"Piped straight into stdout" | @puts
```

---

## Building and Running

### 1. Golang Implementation
```bash
# Build
cd go
go build -o tick.exe ./cmd/tick

# Run the complete showcase
./tick.exe ../examples/showcase.tick

# Run FFI demo
./tick.exe ../examples/ffi_demo.tick

# Run JSON, lists, floats, ints, strings, recursion demo
./tick.exe ../examples/test_json_types.tick

# Run coroutine demo (cooperative multitasking)
./tick.exe ../examples/coroutine_demo.tick

# Run interactive REPL
./tick.exe
```

### 2. C Implementation
```bash
# Build with GCC or Make
cd c
gcc -Wall -O2 -Iinclude -Isrc -o tick.exe src/lexer.c src/ast.c src/parser.c src/object.c src/json.c src/coroutine.c src/ffi.c src/vm.c src/main.c -lm

# Run the complete showcase
./tick.exe ../examples/showcase.tick

# Run FFI demo
./tick.exe ../examples/ffi_demo.tick

# Run JSON, lists, floats, ints, strings, recursion demo
./tick.exe ../examples/test_json_types.tick

# Run coroutine demo (cooperative multitasking via Protothreads)
./tick.exe ../examples/coroutine_demo.tick

# Run interactive REPL
./tick.exe
```

---

## WebAssembly (WASM) Interactive Playground

Tick compiles directly to WebAssembly, enabling in-browser execution with zero installation.

🎮 **Live Playground:** **[https://xyzzyapps.github.io/tick/](https://xyzzyapps.github.io/tick/)**

### Running the Web Playground Locally
```bash
# Start the built-in HTTP server
go run ./web/server.go

# Open in browser:
# http://localhost:8080/
```

### Rebuilding the WASM Binary
```bash
cd go
$env:GOOS='js'; $env:GOARCH='wasm'; go build -o ../web/tick.wasm ./cmd/wasm
```

---

## C Embedding API (`include/tick.h`)

Embedding Tick into a C or C++ application requires just a few lines:

```c
#include "tick.h"

int main(void) {
    TickVM* vm = tick_new();

    // Evaluate inline Tick statements
    tick_eval(vm, "42 println");
    tick_eval(vm, "set window [ WindowBuilder new , title 'Editor' | build ]");

    // Evaluate an external script file
    tick_eval_file(vm, "script.tick");

    tick_free(vm);
    return 0;
}
```

---

## Acknowledgments

✨ **Built with Gemini** — Tick's specification, dual-reference runtime architecture (Go & C99), Protothreads coroutine engine, interactive WebAssembly playground, and test suites were designed and crafted in collaboration with Google Gemini.

---

## License

This software is licensed under the **[PolyForm Noncommercial License 1.0.0](LICENSE)**.

```
Required Notice: Copyright 2026 Xyzzy Apps <xyzzyapps@gmail.com>
```

You are free to use, modify, and distribute this software for personal study, research, hobby projects, educational institutions, noncommercial organizations, and other noncommercial purposes. Commercial use requires a separate license from the licensor.
