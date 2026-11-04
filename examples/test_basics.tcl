# Test basic classes and methods
Object subclass Person vars { name age }

Person method init n age a {
    set name n
    set age a
}

Person method describe {
    return "$name is $age years old"
}

set p [Person new]
p init "Alice" age 30
set desc [p describe]
@puts desc
