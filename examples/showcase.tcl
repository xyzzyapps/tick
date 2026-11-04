# Supporting classes for the showcase
Object subclass Logger vars { messages }
Logger method init {
    set messages [Array new]
}
Logger method info msg {
    @puts "[INFO] $msg"
}
Logger method recordMetric metric value val {
    @puts "[METRIC] $metric = $val"
}
Logger method flush {
    @puts "[LOGGER] Flushed buffer"
}

Object subclass Order vars { paid cancelled time amt }
Order method paid p cancelled c timestamp t amount a {
    set paid p
    set cancelled c
    set time t
    set amt a
}
Order method isPaid { return paid }
Order method isCancelled { return cancelled }
Order method timestamp { return time }
Order method amount { return amt }

Object subclass WindowBuilder vars { title width height }
WindowBuilder method title t { set title t }
WindowBuilder method width w { set width w }
WindowBuilder method height h { set height h }
WindowBuilder method build {
    return "Window('$title', ${width}x${height})"
}

# ==============================================================================
# Complete Language Showcase from Specification
# ==============================================================================

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

# ==============================================================================
# Run and verify
# ==============================================================================
@puts "Window built via Cascade & Pipeline:"
@puts window

set myLogger [Logger new]
[myLogger init]

set processor [OrderProcessor new]
processor initializeWithTax 0.05 logger myLogger

# Create test orders
set o1 [Order new]
o1 paid true cancelled false timestamp 100 amount 50.0

set o2 [Order new]
o2 paid false cancelled false timestamp 200 amount 80.0

set o3 [Order new]
o3 paid true cancelled true timestamp 150 amount 120.0

set o4 [Order new]
o4 paid true cancelled false timestamp 300 amount 150.0

set ordersList [Array new]
ordersList add o1
ordersList add o2
ordersList add o3
ordersList add o4

@puts "Processing orders batch..."
set total [processor processOrders ordersList onError { err | @puts "ERROR: $err" }]
@puts "Grand Total: $total"
