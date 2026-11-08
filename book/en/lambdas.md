# Anonymous functions

An anonymous function, or *lambda*, is a function written where it is used, as an argument or a value. It can use the variables around it, and the way it takes them follows the rules of [Ownership](ownership.md).

## Writing a lambda

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 limit = 6
    i64 big = count(values, [limit](i64 x) bool { ret x > limit })
    println("{} values are above {}", big, limit)
    ret 0
}
```

```output
3 values are above 6
```

The lambda is `[limit](i64 x) bool { ret x > limit }`:

- between square brackets, the variables it **captures**, here `limit`; a lambda that captures nothing starts with `[]`;
- its parameters, as a function's;
- the type it returns, after the parameters; without it, the lambda returns nothing;
- its body, where `ret` returns from the lambda, not from the function around it.

A parameter of a function type is called as a function: `keep(v)`.

A function declared with a name is not a value: `count(values, isEven)` is the error `'isEven' is a function: call it with '()'`. A lambda that calls it takes its place: `count(values, [](i64 x) bool { ret isEven(x) })`.

## The type of a function

`func(i64) bool` is the type of a function that takes an `i64` and returns a `bool`: the keyword `func`, the types of the parameters, and the type returned after them. A function type without a type returned is a function that returns nothing: `func()`, `func(string)`.

The parameters keep their modes, as in a function: `func(ref i64)` is a function that can modify its argument, `func(move string)` one that takes its argument, and the call writes `ref` and `move` as for any function:

```cromo
void update(ref list<i64> values, func(ref i64) change) {
    for (ref i64 v in values) {
        change(ref v)
    }
}

void give(func(move string) take) {
    string text = "a new string"
    take(move text)
}

i32 main() {
    list<i64> values = [1, 2, 3]
    update(ref values, [](ref i64 v) { v *= 10 })
    println("{:?}", values)

    give([](move string s) { println("took {}", s) })
    ret 0
}
```

```output
[10, 20, 30]
took a new string
```

A lambda is accepted only where its type is the same, with the same types and modes of the parameters and the same type returned:

```cromo error
void update(ref list<i64> values, func(ref i64) change) {
    for (ref i64 v in values) {
        change(ref v)
    }
}

i32 main() {
    list<i64> values = [1, 2, 3]
    update(ref values, [](i64 v) {
        println("{}", v)
    })
    ret 0
}
```

```output
error: a value of type func(i64) cannot be used as func(ref i64)
```

A function type can use the type parameters of a generic, as `func(T) U` in `U apply<T, U>(T value, func(T) U f)`: [Interfaces and generics](interfaces-generics.md) shows it.

## Captures

A lambda says how it uses each variable it captures, with the same words as a parameter:

| Capture | The lambda |
|---|---|
| `[limit]` | reads the variable |
| `[ref total]` | modifies the variable |
| `[move name]` | takes the value, which the variable loses |

### Reading

A name alone reads the variable. A value of a simple type, a number, a `bool` or a struct made only of them, is copied when the lambda is made, so later changes to the variable do not reach the lambda:

```cromo
i32 main() {
    i64 limit = 1
    func(i64) bool above = [limit](i64 x) bool { ret x > limit }
    limit = 100
    println("{} {}", above(5), limit)
    ret 0
}
```

```output
true 100
```

Any other value, as a `string` or a list, is read where it is, by address, without copying it. Such a lambda depends on the variable, and can only be passed as an argument, as explained in the section on lambdas as values below.

### Modifying with ref

With `ref`, the lambda modifies the variable itself:

```cromo
i64 count(list<i64> values, func(i64) bool keep) {
    i64 n = 0
    for (i64 v in values) {
        if (keep(v)) {
            n++
        }
    }
    ret n
}

i32 main() {
    list<i64> values = [3, 8, 12, 5, 20]
    i64 total = 0
    count(values, [ref total](i64 x) bool {
        total += x
        ret true
    })
    println("total {}", total)
    ret 0
}
```

```output
total 48
```

A parameter is captured with `ref` only if it is a `ref` parameter: a read parameter cannot be modified, by the function or by its lambdas, and `[ref total]` of one is the error `cannot capture 'total' with 'ref': parameter 'total' cannot be modified`.

### Taking with move

With `move`, the lambda takes the value, and owns it: the variable can no longer be used after, as after any `move`. Only a local variable or a `move` parameter can give its value; a read parameter belongs to the caller, and `[move text]` of one is the error `cannot capture 'text' with 'move': only a local variable or a 'move' parameter can give its value`. For a simple type, `move` is a copy, and the variable stays usable.

```cromo
func() greeter(move string name) {
    ret [move name]() { println("hello {}", name) }
}

i32 main() {
    func() hello = greeter("Anna")
    hello()
    hello()
    ret 0
}
```

```output
hello Anna
hello Anna
```

### The rules of the captures

- A lambda uses only the variables of outside that it captures: any other is an error, which says to write it between the `[]`.
- A name is captured once: `[limit, ref limit]` is the error `'limit' is already captured`.
- A field cannot be captured: inside a method, `[count]` of a field is an error; the method copies it into a variable first, `i64 c = count`, and captures `c`. A local struct is captured whole, `[p]`, and the lambda reads `p.x`.
- What is captured without `ref` is read-only, also what the lambda took with `move`: the lambda can run again, and must find its captures as they were. Only the lambda of a thread or a task can modify what it took, as explained at the end of the chapter.
- For the same reason, a captured value cannot be moved out of the lambda: `ret name` gives a copy, `ret move name` is an error.
- `break` and `continue` do not leave the lambda: inside it they work only in a `for` of the lambda itself.

The compiler says which rule a lambda breaks:

```cromo error
void run(func() f) {
    f()
}

struct Counter {
    i64 count

    void show() {
        run([count]() { println("{}", count) })
    }
}

i32 main() {
    i64 total = 0
    run([]() {
        total++
    })
    i64 limit = 3
    run([limit]() {
        limit = 4
    })
    string name = "Anna"
    func() string give = [move name]() string { ret move name }
    ret 0
}
```

```output
error: 'count' is a field: copy it into a variable to capture it
error: 'total' is not captured: write it between the [] of the lambda
error: 'limit' is captured to be read: capture it with 'ref limit' to modify it
error: cannot move 'name': it is captured, and the lambda can run again
```

### Captures and the other arguments

What a lambda captures by address, with `ref` or by reading a value that is not simple, counts as used by the call that receives the lambda. So the same call cannot also pass that variable with `ref`, as two `ref` arguments cannot be the same variable ([Ownership](ownership.md)):

```cromo error
void add(ref i64 total, func(i64) f) {
    f(1)
}

i32 main() {
    i64 total = 0
    add(ref total, [ref total](i64 x) {
        total += x
    })
    ret 0
}
```

```output
error: 'total' overlaps 'total' in the same call, which a lambda of the call captures
```

## Lambdas as values

Where a lambda can go depends on its captures:

- A lambda that captures by address, with `ref` or by reading a value that is not simple, can only be passed as an argument, to a parameter that reads it, as `keep` of `count`, not to a `move` parameter: the variables it refers to live in the function that calls, and the lambda ends with the call.
- A lambda that captures nothing, only simple values, or takes its values with `move` owns everything it uses. It is a value: it can be kept in a variable, a field or a list, returned, and passed to a `move` parameter. Keeping a lambda that has captures puts them on the heap, as a `string` keeps its text there.

```cromo
func(i64) i64 adder(i64 amount) {
    ret [amount](i64 x) i64 { ret x + amount }
}

struct Button {
    string label
    func() onClick

    void click() {
        onClick()
    }
}

i32 main() {
    func(i64) i64 plus3 = adder(3)
    println("{}", plus3(4))

    list<func(i64) i64> steps = [adder(1), adder(10), adder(100)]
    i64 x = 0
    for (func(i64) i64 step in steps) {
        x = step(x)
    }
    println("{}", x)

    string file = "notes.txt"
    Button save
    save.label = "Save"
    save.onClick = [move file]() { println("saving {}", file) }
    save.click()
    ret 0
}
```

```output
7
111
saving notes.txt
```

Keeping a lambda that captures by address does not compile, and the message says how to keep it: take the value with `move`.

```cromo error
i32 main() {
    list<i64> values = [1, 2, 3]
    func() show = [values]() {
        println("{:?}", values)
    }
    ret 0
}
```

```output
error: this lambda captures by address (with 'ref', or reading a value that is not simple): it can only be passed as an argument; capture with 'move' to keep it
```

Returning one is the same error: the variable would end with the function, and the lambda would refer to nothing.

```cromo error
func() makeCounter() {
    i64 n = 0
    ret [ref n]() { n++ }
}

i32 main() {
    ret 0
}
```

```output
error: this lambda captures by address
```

## Function values

A variable, a field, an element of a list and a value returned can be of a function type. Such a value:

- is called as a function: a variable as `plus3(4)`, a field as `button.onClick(1)`, and the value of any expression too, as `steps[1](5)` or `multiplier(4)(5)`;
- is moved with `move`, never copied: `func() again = hello` is the error `values of type func() cannot be copied: write 'move hello'`, and it has no `copy()`;
- cannot be compared or printed: `==` does not work on it, and neither `{}` nor `{:?}` prints it;
- starts empty, as a field never assigned: calling an empty function is a panic, `the function is empty: no lambda was given to it`.

A lambda can throw an exception, as any function: the exception leaves the call of the function value, and then the function that called it, until a `catch`.

```cromo
func(i64) i64 multiplier(i64 k) {
    ret [k](i64 x) i64 { ret x * k }
}

struct Button {
    string label
    func(i32) onClick
}

void each(list<i32> values, func(i32) f) {
    for (i32 v in values) {
        f(v)
    }
}

i32 main() {
    Button ok
    ok.onClick = [](i32 n) { println("clicked {} times", n) }
    ok.onClick(2)

    list<func(i64) i64> steps = [multiplier(2), multiplier(3)]
    println("{} {}", steps[1](5), multiplier(4)(5))

    i32 seen = 0
    try {
        each([1, 2, 30], [ref seen](i32 v) {
            if (v > 10) {
                throw Error(format("{} is too big", v))
            }
            seen++
        })
    } catch (Exception e) {
        println("{}, after {} values", e.message(), seen)
    }
    ret 0
}
```

```output
clicked 2 times
15 20
30 is too big, after 2 values
```

## const func

A parameter `const func(...)` accepts only a function that does not modify what it captures, as a lambda of C++ without `mutable`:

- a lambda that does not capture with `ref`;
- a function value, as a variable or a field of a function type: its captures are read-only;
- a parameter that is a `const func` too.

```cromo
struct Button {
    func() onClick
}

void twice(const func() f) {
    f()
    f()
}

i32 main() {
    string word = "hello"
    twice([word]() { println("{}", word) })

    Button b
    b.onClick = []() { println("click") }
    twice(b.onClick)
    ret 0
}
```

```output
hello
hello
click
click
```

A lambda that captures with `ref` is refused, and so is a parameter `func` that is not `const`, which may be such a lambda; a `const func` cannot call it either, from a lambda of its own:

```cromo error
void twice(const func() f) {
    f()
    f()
}

void pass(func() g) {
    twice(g)
    twice([g]() { g() })
}

i32 main() {
    i64 calls = 0
    twice([ref calls]() { calls++ })
    ret 0
}
```

```output
error: 'g' is a parameter func that is not const: only a 'const func' can be given to a 'const func'
error: 'g' is a parameter func that is not const: a const func cannot call it
error: a const func does not modify what it captures: it cannot capture 'calls' with 'ref'
```

Since a `const func` modifies nothing, several threads can call it at the same time, safely: this is how the HTTP server calls the same handler for many requests at once, as [Networking and HTTP](networking.md) shows.

## The lambdas of threads

A thread or a task runs a lambda: `thread.Thread([move lines]() { ... })`, `thread.Task(...)`. That lambda runs once, so what it takes with `move` is its own, and, unlike the other lambdas, it can modify it. A thread or a task cannot capture a parameter `func` that is not `const`, not even through a lambda inside its own: that function may modify what it captures while another thread reads it, and the compiler asks to declare the parameter `const func`. [Threads, tasks and channels](concurrency.md) explains the threads and what their lambdas can capture.

```cromo
import std.thread

i32 main() {
    list<string> lines = ["first"]
    thread.Thread worker = thread.Thread([move lines]() {
        lines.add("second")
        println("{:?}", lines)
    })
    worker.start()
    worker.join()
    ret 0
}
```

```output
["first", "second"]
```

## Summary

- A lambda is `[captures](parameters) type { body }`; its type is `func(parameters) type`, where the parameters keep their modes, as `func(ref i64)`.
- A named function is not a value: a lambda that calls it takes its place.
- Captures use the words of parameters: a name reads, `ref` modifies, `move` takes. A simple value read is copied when the lambda is made; the others are read where they are.
- A lambda uses only what it captures; a name is captured once, a field is copied into a variable first, and what is captured without `ref` is read-only.
- A lambda that captures by address lives only during the call that receives it; one that owns what it captures is a value, kept in variables, fields and lists, and returned.
- Function values are moved, not copied, compared or printed; an empty one is a panic when called.
- `const func` is a function that does not modify what it captures, and can be called from several threads at once.
- The lambda of a thread or a task runs once, and can modify what it takes with `move`; it cannot capture a parameter `func` that is not `const`.
