# Coroutine Showcase in Tcl-Talk / Talk
# Demonstrates cooperative multitasking with Protothreads (C) and Channels (Go)

"=== 1. Generator: Fibonacci Stream ===" println

set makeFibo {
    coroutine create {
        set a 0
        set b 1
        coroutine yield a
        coroutine yield b
        set next [a + b]
        coroutine yield next
        set a b
        set b next
        set next [a + b]
        coroutine yield next
        set a b
        set b next
        set next [a + b]
        coroutine yield next
    }
}

set fib [makeFibo value]
"Fib status: [fib status]" println
"Fib term 1: [fib resume]" println
"Fib term 2: [fib resume]" println
"Fib term 3: [fib resume]" println
"Fib term 4: [fib resume]" println
"Fib term 5: [fib resume]" println

"=== 2. Two-Way Communicating Coroutine ===" println

set worker [
    coroutine create { initial |
        set step1 [coroutine yield [initial * 2]]
        set step2 [coroutine yield [step1 + 50]]
        return [step2 * 10]
    }
]

"Worker initial status: [worker status]" println
"Sent 10, yielded: [worker resume 10]" println
"Sent 5, yielded: [worker resume 5]" println
"Sent 3, final return: [worker resume 3]" println
"Worker final status: [worker status]" println
"Worker is done: [worker isDone]" println
