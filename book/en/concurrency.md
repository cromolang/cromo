# Threads, tasks and channels

Cromo runs code at the same time in two ways: **threads** of the system, and lightweight **tasks**, many of which share a few threads. Both are in the module `std.thread`, with the same interface, and the compiler checks what they share: two threads never write the same data at the same time.

The module has seven types:

| Type | What it is |
|---|---|
| `thread.Thread` | a thread of the system, made from a lambda |
| `thread.Task` | a lightweight task, with the interface of `Thread` |
| `thread.Group` | tasks started one by one, also in a loop, and waited for together |
| `thread.Mutex<T>` | data that several threads change, one at a time |
| `thread.Shared<T>` | a value with its lock, reached through a handle that is copied freely |
| `thread.Atomic<T>` | an integer or a `bool` that threads change without a lock |
| `thread.Channel<T>` | values passed from one thread or task to another, in order |

The rules of the threads hold for the tasks too: the section on tasks says only what changes.

## Threads

A `thread.Thread` is made from a lambda, started with `start()` and waited for with `join()`:

```cromo
import std.thread

i64 sum(list<i64> values, i64 from, i64 to) {
    i64 total = 0
    for (i64 i = from; i < to; i++) {
        total += values[i]
    }
    ret total
}

i32 main() {
    list<i64> values
    for (i64 i = 1; i <= 1000000; i++) {
        values.add(i)
    }
    i64 low = 0
    thread.Thread worker = thread.Thread([values, ref low]() {
        low = sum(values, 0, 500000)
    })
    worker.start()
    i64 high = sum(values, 500000, 1000000)
    worker.join()
    println("{}", low + high)
    ret 0
}
```

```output
500000500000
```

Making the thread does not run anything: the lambda runs from `start()`, in a new thread of the system, while `main` goes on. `join()` waits for its end.

### What the lambda of a thread captures

The lambda of a thread captures with the words of [Anonymous functions](lambdas.md):

- **A name** reads the variable. A simple value, as a number (see [Ownership](ownership.md)), is copied when the lambda is made, so the block can change its variable afterwards; any other value is read where it is, as `values` above.
- **`ref`** modifies the variable, as `low` above.
- **`move`** takes the value: the thread owns it. A thread runs its lambda once, so, unlike the other lambdas, it can modify what it took.

Here `limit` is copied when the lambda is made, so `main` changes its own variable freely, and `names` is moved into the thread, which adds a name:

```cromo
import std.thread
import std.time

i32 main() {
    i64 limit = 1
    list<string> names = ["Ada", "Grace"]
    thread.Thread t = thread.Thread([limit, move names]() {
        time.msleep(50)
        names.add("Linus")
        println("the thread sees limit {} and {:?}", limit, names)
    })
    limit = 2
    t.start()
    limit = 3
    t.join()
    println("main has limit {}", limit)
    ret 0
}
```

```output
the thread sees limit 1 and ["Ada", "Grace", "Linus"]
main has limit 3
```

A thread cannot capture a parameter `func` that is not `const`, not even through a lambda inside its own: such a function may be a lambda that modifies what it captures, while another thread reads it. The compiler asks to declare the parameter `const func`, a function that modifies nothing and can run in several threads at once:

```cromo error
import std.thread

void inBackground(func() work) {
    thread.Thread t = thread.Thread([work]() { work() })
    t.start()
    t.join()
}

i32 main() {
    inBackground([]() { println("working") })
    ret 0
}
```

```output
error: 'work' is a parameter func that is not const: a thread or a task cannot call it, as it may modify what it captures; declare it 'const func'
```

With `const func` the thread can call it. A parameter `move func()` works too: it is the function itself, given to the callee, so the thread can take it with `move`, and nobody else has it:

```cromo
import std.thread

void inBackground(const func() work) {
    thread.Thread t = thread.Thread([work]() { work() })
    t.start()
    t.join()
}

thread.Thread later(move func() work) {
    ret thread.Thread([move work]() { work() })
}

i32 main() {
    inBackground([]() { println("working") })
    i64 n = 3
    thread.Thread t = later([n]() { println("n is {}", n) })
    t.start()
    t.join()
    ret 0
}
```

```output
working
n is 3
```

### Threads of a block

A thread whose lambda captures by address, with `ref` or by reading a value that is not simple, is a **thread of its block**: it uses variables of the block, so it must end before them. The compiler checks it with these rules:

- It is declared as a variable whose value is `thread.Thread(...)`, with the lambda written there: `thread.Thread t = thread.Thread([ref v]() { ... })`.
- `t.start()` and `t.join()` are written directly in the block that declares `t`, not inside an `if` or a loop: so the compiler knows where the thread runs.
- While it runs, from `start()` to `join()`, the block cannot modify what the thread reads, and cannot use at all what the thread has with `ref`. Several threads can read the same variable; one that has it with `ref` excludes the others, and starting a second one is an error.
- If `join()` is not called, the thread is waited for at the end of its block.
- It cannot be moved, assigned, returned, passed with `ref` or captured.

```cromo error
import std.thread

i32 main(list<string> args) {
    list<i64> values = [1, 2, 3]
    i64 total = 0
    thread.Thread a = thread.Thread([values, ref total]() {
        for (i64 v in values) {
            total += v
        }
    })
    thread.Thread b = thread.Thread([ref total]() { total = 0 })
    a.start()
    values.add(4)
    println("{}", total)
    b.start()
    if (args.len() > 1) {
        a.join()
    }
    ret 0
}
```

```output
error: 'values' cannot be modified while the thread 'a' runs
error: 'total' is used by the thread 'a' until a.join()
error: the thread 'b' cannot start: 'total' is used by the thread 'a' until a.join()
error: join() of a thread of a block is written directly in the block that declares 'a'
```

Before `start()` and after `join()` the variables are the block's again: `values.add(4)` before `a.start()`, or after `a.join()`, is allowed.

A thread of a block stays where it is declared:

```cromo error
import std.thread

thread.Thread counter(thread.Mutex<i64> total) {
    ret thread.Thread([total]() { total.lock([](ref i64 n) { n++ }) })
}

i32 main() {
    i64 x = 0
    thread.Thread t = thread.Thread([ref x]() { x = 1 })
    list<thread.Thread> kept
    kept.add(move t)
    ret 0
}
```

```output
error: a thread whose lambda captures by address belongs to its block: declare it, as thread.Thread t = thread.Thread(...)
error: 't' is a thread of its block, which captures by address: it cannot be moved, assigned, returned, passed with 'ref' or captured
```

Here `count` reaches its end while its thread still runs, and does not call `join()`: the end of the block waits for the thread, so `total` is set when `count` returns:

```cromo
import std.thread
import std.time

void count(ref i64 total) {
    thread.Thread t = thread.Thread([ref total]() {
        time.msleep(50)
        total = 5
        println("the thread ends")
    })
    t.start()
    println("count ends its work")
}

i32 main() {
    i64 total = 0
    count(ref total)
    println("total {}", total)
    ret 0
}
```

```output
count ends its work
the thread ends
total 5
```

### Free threads

A thread that captures only with `move`, or simple values, owns everything it uses: it is **free**. It can be returned, moved, kept in a list, and it can outlive the block that made it. A free thread that must give back a result uses a `thread.Shared`, which is a simple value, as shown below:

```cromo
import std.thread

// A thread that adds the numbers of 'part' to 'total'.
thread.Thread adder(move list<i64> part, thread.Shared<i64> total) {
    ret thread.Thread([move part, total]() {
        i64 sum = 0
        for (i64 v in part) {
            sum += v
        }
        total.lock([sum](ref i64 t) { t += sum })
    })
}

i32 main() {
    thread.Shared<i64> total = thread.Shared<i64>(0)
    list<thread.Thread> workers
    for (i64 w = 0; w < 4; w++) {
        list<i64> part
        for (i64 i = w * 250 + 1; i <= (w + 1) * 250; i++) {
            part.add(i)
        }
        workers.add(adder(move part, total))
    }
    for (ref thread.Thread w in workers) {
        w.start()
    }
    for (ref thread.Thread w in workers) {
        w.join()
    }
    total.lock([](ref i64 t) { println("{}", t) })
    ret 0
}
```

```output
500500
```

The compiler does not follow a free thread: its `start()` and `join()` can be anywhere, also in a loop, as here.

### start() and join()

- `start()` twice is a panic, and so are `join()` before `start()` and `join()` twice. For a thread of a block, a second `start()` before its `join()` is already an error of the compiler.
- A thread that is never started does nothing, and nothing waits for it.
- A thread that was started and not joined is waited for when its variable ends: at the end of its block, or when the list that holds it ends.

The panics say `the thread is already started`, `the thread is not started: call start() first` and `the thread is already joined`.

### Exceptions and panics in a thread

An exception that ends the lambda of a thread is thrown again by `join()`, in the thread that waits:

```cromo
import std.thread

i32 main() {
    thread.Thread t = thread.Thread([]() { throw Error("no data") })
    t.start()
    try {
        t.join()
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    ret 0
}
```

```output
caught: no data
```

An exception that no `join()` takes stops the program when the thread is waited for, as an exception that leaves `main` ([Errors](errors.md)), with exit code 1:

```cromo file=lost.cro
import std.thread

void work() {
    throw Error("no data")
}

i32 main() {
    thread.Thread t = thread.Thread([]() { work() })
    t.start()
    println("main goes on")
    ret 0
}
```

```output
main goes on
uncaught exception in a thread: no data
    at work (lost.cro:4)
    at main.lambda (lost.cro:8)
```

A panic in any thread stops the whole program at once, with exit code 101: `join()` does not catch it, and the other threads stop too.

```cromo file=late.cro
import std.thread

i32 main() {
    list<i64> values = [1, 2]
    thread.Thread t = thread.Thread([values]() { println("{}", values[5]) })
    t.start()
    t.join()
    println("not printed")
    ret 0
}
```

```output
panic: index out of bounds: the index is 5 but the length is 2 at late.cro:5:70
    at main.lambda (late.cro:5)
```

### The stack of a thread

A thread has the stack of the main thread: 1 MB on Windows, 8 MB on Linux, with a guard page at its end. A recursion that goes beyond it is a panic, `stack overflow`, as in `main`. The memory of a stack is taken when it is used.

### Printing from several threads

Each `print` and `println` writes its whole text at once: two threads never mix the pieces of their lines. The order of the lines of different threads changes from run to run. `format` writes into its own string, and does not wait for the others. A panic waits for a print in progress, then stops the program.

```cromo nocheck
import std.thread

void steps(string name) {
    for (i32 step = 1; step <= 3; step++) {
        println("{}: step {} of 3", name, step)
    }
}

i32 main() {
    thread.Thread a = thread.Thread([]() { steps("first") })
    thread.Thread b = thread.Thread([]() { steps("second") })
    a.start()
    b.start()
    steps("main")
    a.join()
    b.join()
    ret 0
}
```

One run printed:

```output
main: step 1 of 3
main: step 2 of 3
first: step 1 of 3
first: step 2 of 3
first: step 3 of 3
second: step 1 of 3
main: step 3 of 3
second: step 2 of 3
second: step 3 of 3
```

## Mutex

When threads must modify the same data, the data go in a `thread.Mutex<T>`. They can be reached only inside `lock`, which gives them with `ref` to one thread at a time:

```cromo
import std.thread

i32 main() {
    thread.Mutex<i64> counter
    thread.Thread a = thread.Thread([counter]() {
        for (i32 i = 0; i < 1000; i++) {
            counter.lock([](ref i64 n) { n++ })
        }
    })
    thread.Thread b = thread.Thread([counter]() {
        for (i32 i = 0; i < 1000; i++) {
            counter.lock([](ref i64 n) { n += 2 })
        }
    })
    a.start()
    b.start()
    a.join()
    b.join()
    counter.lock([](ref i64 n) { println("{}", n) })
    ret 0
}
```

```output
3000
```

There is no way to reach the `i64` without the lock, so there is no way to forget it. Both threads capture `counter` by reading it: `lock` does not modify the mutex, it lends its data.

- A mutex starts unlocked, with the default value of its data: `thread.Mutex<i64> counter` is ready, at 0. It has no constructor: to start from another value, set it with a first `lock`.
- `lock` is released at the end of its lambda, also when an exception goes through: the changes made before the exception stay in the data, and the exception goes on.
- `lock` is not recursive: locking the same mutex again inside its lambda waits for ever.
- Who finds the mutex locked waits: a task leaves its thread to the others in the meantime. A mutex is not fair: who waits longest is not always the next to have it.
- A mutex cannot be copied: the copy would be a second lock, which the other threads do not see. It can be moved while no thread uses it.

When the lambda of `lock` throws, what it changed before stays in the mutex, which is unlocked:

```cromo
import std.thread

i32 main() {
    thread.Mutex<list<string>> names
    names.lock([](ref list<string> v) { v.add("Ada") })
    try {
        names.lock([](ref list<string> v) {
            v.add("Grace")
            throw Error("stop")
        })
    } catch (Exception e) {
        println("caught: {}", e.message())
    }
    names.lock([](ref list<string> v) { println("{:?}", v) })
    ret 0
}
```

```output
caught: stop
["Ada", "Grace"]
```

Copying a mutex is an error, and so is moving it while a thread uses it:

```cromo error
import std.thread

i32 main() {
    thread.Mutex<i64> counter
    thread.Mutex<i64> copy = counter
    thread.Thread a = thread.Thread([counter]() { counter.lock([](ref i64 n) { n++ }) })
    a.start()
    thread.Mutex<i64> moved = move counter
    a.join()
    ret 0
}
```

```output
error: values of type Mutex<i64> cannot be copied
error: 'counter' cannot be modified while the thread 'a' runs
```

## Shared

A `Mutex` stays where it is declared: a lambda that is kept, as the handler of a window, cannot capture it, and the free threads cannot share it. `thread.Shared<T>` is a handle to a value with its lock, kept until the end of the program: a simple value, copied freely, and every copy reaches the same value with `lock`:

```cromo
import std.thread

i32 main() {
    thread.Shared<list<string>> log = thread.Shared<list<string>>(list<string>())
    list<func(string)> handlers
    handlers.add([log](string line) { log.lock([line](ref list<string> v) { v.add(line) }) })
    handlers[0]("opened")
    handlers[0]("saved")
    log.lock([](ref list<string> v) { println("{}", join(v, ", ")) })
    ret 0
}
```

```output
opened, saved
```

The handlers of the windows keep their state so, as [Windows and apps](gui.md) shows; threads and tasks capture a `Shared` as any other simple value, and a thread that captures only `Shared` handles and simple values is free.

- `thread.Shared<T>(value)` moves the value into memory of its own, which stays until the end of the program: the destructor of the value never runs.
- `lock` works as that of `Mutex`: one at a time, released also when an exception goes through; locking it again inside its lambda waits for ever.
- A `Shared` declared without a value has none: `lock` on it is a panic, `this thread.Shared has no value: make it with thread.Shared<T>(value)`.

Here two handles reach the same `Log`, whose destructor never runs:

```cromo
import std.thread

struct Log {
    list<string> lines

    ~Log() {
        println("the log ends")
    }
}

i32 main() {
    thread.Shared<Log> log = thread.Shared<Log>(Log())
    thread.Shared<Log> other = log
    other.lock([](ref Log l) { l.lines.add("opened") })
    log.lock([](ref Log l) { println("{} line", l.lines.len()) })
    ret 0
}
```

```output
1 line
```

Use a `Mutex` for data of a block, as the counters of a computation, and a `Shared` for a state that lives as long as the program, as that of a window: since its value is never freed, a program that made a new `Shared` in a loop would keep all of them.

## Atomics

`thread.Atomic<T>` holds an integer or a `bool` that several threads change without a lock. Each operation happens whole, and all of them in one order that every thread sees: the sequential consistency of C++, the simplest model to reason about.

| Method | What it does | Gives |
|---|---|---|
| `load()` | reads the value | the value |
| `store(v)` | writes `v` | nothing |
| `add(n)` | adds `n`, for the integers only | the value before |
| `exchange(v)` | writes `v` | the value before |
| `compareExchange(expected, desired)` | writes `desired` only if the value is `expected`, in one step | `true` if it wrote |
| `loadRelaxed()`, `storeRelaxed(v)`, `addRelaxed(n)` | as `load`, `store` and `add`, without an order among the threads | as those |

An atomic declared without a value starts from 0 or `false`; `thread.Atomic<i64>(5)` starts from 5. `add` wraps around when the result does not fit, in every build, without a panic. Unlike C++, `compareExchange` does not write the value it found into `expected`: the program calls `load()` again.

```cromo
import std.thread

i32 main() {
    thread.Atomic<i64> count = thread.Atomic<i64>(5)
    thread.Atomic<bool> ready
    println("{} {}", count.load(), ready.load())
    println("add gives {}, now {}", count.add(10), count.load())
    println("exchange gives {}, now {}", count.exchange(100), count.load())
    println("{}", count.compareExchange(5, 7))
    println("{}, now {}", count.compareExchange(100, 7), count.load())
    println("{}", ready.exchange(true))
    thread.Atomic<i32> big = thread.Atomic<i32>(2147483647)
    big.add(1)
    println("{}", big.load())
    ret 0
}
```

```output
5 false
add gives 5, now 15
exchange gives 15, now 100
false
true, now 7
false
-2147483648
```

`compareExchange` in a loop changes a value by a rule of its own, as keeping the largest one seen: if another thread changed the value between `load()` and `compareExchange`, the loop reads it again.

```cromo
import std.thread

// Raises 'best' to 'value', if it is larger, without a lock.
void raise(thread.Atomic<i64> best, i64 value) {
    for {
        i64 seen = best.load()
        if (value <= seen || best.compareExchange(seen, value)) {
            ret
        }
    }
}

i32 main() {
    list<i64> values = [12, 7, 41, 3, 29, 41, 18, 5]
    thread.Atomic<i64> best
    thread.Thread a = thread.Thread([values, best]() {
        for (i64 i = 0; i < 4; i++) {
            raise(best, values[i])
        }
    })
    thread.Thread b = thread.Thread([values, best]() {
        for (i64 i = 4; i < 8; i++) {
            raise(best, values[i])
        }
    })
    a.start()
    b.start()
    a.join()
    b.join()
    println("{}", best.load())
    ret 0
}
```

```output
41
```

`loadRelaxed`, `storeRelaxed` and `addRelaxed` are faster where only the value counts, as a counter that many threads increase: the value is never half written, but they give no order to what the threads do around them, so another thread may not see yet what this one wrote before.

An atomic cannot be copied, as a mutex; the threads and tasks that use it capture it by reading it, and a function takes it as a parameter that reads it, as `raise` above. A type that is not an integer or a `bool` is an error where it is written, and `add` of an `Atomic<bool>` is an error at the call:

```cromo error
import std.thread

i32 main() {
    thread.Atomic<f64> speed
    thread.Atomic<bool> flag
    flag.add(true)
    thread.Atomic<i64> a
    thread.Atomic<i64> b = a
    ret 0
}
```

```output
error: thread.Atomic holds an integer or a bool, not f64
error: add() is for an atomic integer, not for Atomic<bool>
error: values of type Atomic<i64> cannot be copied
```

## Tasks

A `thread.Task` has the interface and the rules of a `thread.Thread`, but it is not a thread of the system: the runtime runs many tasks on a few threads, its **workers**, one for each processor. When a task waits, for the network, for `time.sleep`, for a mutex, for a channel or for `join()`, its worker runs other tasks in the meantime. The code is the same as with threads: there is no `async`, no `await`, and no function of a different color.

```cromo file=task.cro
import std.thread

i32 main() {
    list<i64> values = [1, 2, 3]
    i64 total = 0
    thread.Task t = thread.Task([values, ref total]() {
        for (i64 v in values) {
            total += v
        }
    })
    t.start()
    t.join()
    println("{}", total)
    thread.Task failing = thread.Task([]() { throw Error("bad input") })
    failing.start()
    ret 0
}
```

```output
6
uncaught exception in a task: bad input
    at main.lambda (task.cro:14)
```

A task captures, starts and joins as a thread, with the same errors and panics; the messages say "task": `the task 'b' cannot start: 'x' is used by the task 'a' until a.join()`.

### What a task costs

- A task has a stack of 1 MB, reserved with a guard page, and the memory is taken page by page when it is used: a task that waits uses a few KB. A recursion that needs more than 1 MB of stack overflows in a task, also on Linux, where a thread has 8 MB.
- The stack of a task that ends is kept, with its memory, for the next task, up to 1024 stacks; the others give their memory back to the system.
- On Windows a program can have more than a hundred thousand tasks at once: 100 000 tasks that wait use about half a gigabyte.
- On Linux each stack is two areas of memory for the kernel, the guard page and the stack, and the kernel limits the areas of a process with `vm.max_map_count`, usually 65530: so about 32 000 tasks can live at once, as many as the threads of a C program. Beyond the limit, starting a task is a panic: `cannot start a task: the system has no resources for it`. Some systems set a higher limit, and the administrator can raise it.

A thread of the system is more expensive: a stack reserved for each, 1 MB on Windows and 8 MB on Linux, and the work of the system to start it and to switch between threads.

### The workers

- There is a worker for each logical processor: all the groups of processors on Windows, the processors that the process may use on Linux. The number cannot be changed.
- A task that calls a function that blocks its thread, as reading a file, a C function or looking up the name of a host on Windows, keeps its worker. Every millisecond the runtime checks the workers: while tasks wait to run and no worker is idle, a worker that has run the same task for the whole millisecond keeps it, and a new worker takes its place for the others. So any C library works inside a task, and a long computation does not stop the other tasks. The workers added are at most 256 beyond the processors.
- `main` is not a task: it is the first thread of the system. When it waits, for `join()`, a channel or a mutex, it waits as a thread, and the tasks go on in the workers. It starts tasks as any thread does.
- A task that finds a mutex locked spins briefly, then parks and leaves its worker to the others; a thread sleeps.

## Groups

`thread.Group` starts many tasks and waits for all of them. `g.start(lambda)` starts a task at once, also in a loop; `g.wait()` waits for the tasks started so far:

```cromo
import std.thread
import std.time

i32 main() {
    time.Instant start = time.now()
    thread.Atomic<i64> done
    thread.Group jobs
    for (i64 id = 0; id < 10000; id++) {
        jobs.start([done]() {
            time.msleep(100)
            done.add(1)
        })
    }
    jobs.wait()
    println("{} tasks waited 100 ms each", done.load())
    println("in less than a second: {}", start.elapsed().milliseconds() < 1000)
    ret 0
}
```

```output
10000 tasks waited 100 ms each
in less than a second: true
```

Ten thousand threads of the system would reserve 10 GB of stacks on Windows and 80 GB on Linux; ten thousand tasks that wait use a few tens of megabytes.

The tasks of a group run at the same time, also with each other, so they share only what nobody changes:

- They can read the variables declared before the group. The variables they read stay constant from the declaration of the group, not from its first `start()`, to the end of its block, also after `wait()`.
- They take other values with `move`; simple values, as `id` in a loop, are copied, wherever they are declared.
- They cannot capture with `ref`: data that the tasks change go in a `Mutex`, a `Shared` or an `Atomic` declared before the group, as `done` above.
- A group cannot be moved, assigned, returned, passed with `ref` or captured.

```cromo error
import std.thread

void take(ref thread.Group g) {
}

i32 main() {
    list<i64> before = [1, 2]
    i64 count = 0
    thread.Group jobs
    before.add(3)
    list<i64> after = [3]
    jobs.start([before]() { println("{}", before.len()) })
    jobs.start([after]() { println("{}", after.len()) })
    jobs.start([ref count]() { count++ })
    jobs.wait()
    before.add(4)
    take(ref jobs)
    ret 0
}
```

```output
error: 'before' cannot be modified after the group 'jobs', whose tasks read it
error: the tasks of the group 'jobs' read 'after', declared after the group: declare it before, or take it with 'move'
error: the tasks of a group run at the same time: they cannot capture 'count' with 'ref'; read it, or take it with 'move'
error: 'before' cannot be modified while the tasks of the group 'jobs' read it
error: 'jobs' is a group of tasks, which read the variables of its block: it cannot be moved, assigned, returned, passed with 'ref' or captured
```

The exceptions of the tasks:

- `wait()` waits for all the tasks started so far, then throws again the first exception of one of them; the exceptions of the others are lost.
- After `wait()` the group can start other tasks, and wait for them again.
- At the end of its block a group waits for its tasks by itself. An exception that no `wait()` takes stops the program, as that of a thread never joined.

Here the same group waits three times; the second exception is lost, and the last one, which no `wait()` takes, stops the program:

```cromo file=group.cro
import std.thread
import std.time

i32 main() {
    thread.Atomic<i64> done
    thread.Group jobs
    jobs.start([done]() { done.add(1) })
    jobs.wait()
    jobs.start([done]() { done.add(10) })
    jobs.wait()
    println("done: {}", done.load())
    jobs.start([]() { throw Error("first") })
    jobs.start([]() {
        time.msleep(100)
        throw Error("second")
    })
    try {
        jobs.wait()
    } catch (Exception e) {
        println("caught {}", e.message())
    }
    jobs.wait()
    println("the second is lost")
    jobs.start([]() { throw Error("never taken") })
    ret 0
}
```

```output
done: 11
caught first
the second is lost
uncaught exception in a task: never taken
    at main.lambda (group.cro:24)
```

## Channels

A `thread.Channel<T>` passes values from one task or thread to another, in the order they were sent:

```cromo
import std.thread

i32 main() {
    thread.Channel<string> lines = thread.Channel<string>(16)
    thread.Atomic<i64> letters
    thread.Group readers
    for (i32 r = 0; r < 4; r++) {
        readers.start([lines, letters]() {
            for (string line in lines) {
                letters.add(line.len())
            }
        })
    }
    list<string> words = ["channels", "pass", "values", "between", "tasks"]
    for (string word in words) {
        lines.send(word.copy())
    }
    lines.close()
    readers.wait()
    println("{} letters", letters.load())
    ret 0
}
```

```output
30 letters
```

Several tasks and threads can send to the same channel and receive from it: each value goes to one receiver. The tasks and threads capture a channel by reading it, and a function takes it as a parameter that reads it: `send`, `receive` and `close` do not need `ref`. A channel cannot be copied.

### The capacity

`thread.Channel<T>(n)` holds up to `n` values: `send` waits while it is full, `receive` while it is empty, and a task that waits leaves its worker to the others. A channel declared without a value has capacity 0: the sender waits until a receiver has taken the value. A negative capacity is a panic.

```cromo
import std.thread
import std.time

// Sends 1, 2, 3 and 4, and receives them 100 ms later.
void run(thread.Channel<i64> channel) {
    thread.Atomic<i64> sent
    thread.Task producer = thread.Task([channel, sent]() {
        for (i64 i = 1; i <= 4; i++) {
            channel.send(i)
            sent.add(1)
        }
        channel.close()
    })
    producer.start()
    time.msleep(100)
    println("sent before the first receive: {}", sent.load())
    i64 sum = 0
    for (i64 v in channel) {
        sum += v
    }
    producer.join()
    println("received: {}", sum)
}

i32 main() {
    run(thread.Channel<i64>(2))
    thread.Channel<i64> handoff
    run(handoff)
    ret 0
}
```

```output
sent before the first receive: 2
received: 10
sent before the first receive: 0
received: 10
```

### Sending, receiving and closing

- `send(move v)` moves the value into the channel. It throws an exception if the channel is closed, and the value is destroyed.
- `receive()` gives the next value. Once the channel is closed and empty, it throws an exception.
- `next(ref v)` receives without the exception: it puts the next value in `v` and returns `true`, or returns `false`, with `v` unchanged, once the channel is closed and empty.
- `close()` ends the channel: the values already sent can still be received. A second `close()` does nothing, so two tasks that end their work can both close the same channel.
- The values still in a channel when it ends are destroyed with it.

```cromo
import std.thread

i32 main() {
    thread.Channel<i64> numbers = thread.Channel<i64>(4)
    numbers.send(1)
    numbers.send(2)
    numbers.close()
    numbers.close()
    try {
        numbers.send(3)
    } catch (Exception e) {
        println("send after close() throws")
    }
    println("{}", numbers.receive())
    i64 v = 0
    println("{} {}", numbers.next(ref v), v)
    println("{} {}", numbers.next(ref v), v)
    try {
        numbers.receive()
    } catch (Exception e) {
        println("receive from a closed, empty channel throws")
    }
    ret 0
}
```

```output
send after close() throws
1
true 2
false 2
receive from a closed, empty channel throws
```

A sender may be waiting when the channel is closed. With capacity 0 it waits until a receiver has its value: if the channel is closed first, `send()` returns normally, and the value can still be received. A `send()` that waits on a full channel throws when the channel is closed, as a `send()` after `close()`:

```cromo
import std.thread
import std.time

i32 main() {
    thread.Channel<string> handoff
    thread.Channel<string> full = thread.Channel<string>(1)
    thread.Task a = thread.Task([handoff]() {
        handoff.send("kept")
        println("send() to handoff returned")
    })
    thread.Task b = thread.Task([full]() {
        full.send("first")
        try {
            full.send("second")
        } catch (Exception e) {
            println("send() to full threw")
        }
    })
    a.start()
    b.start()
    time.msleep(100)
    handoff.close()
    a.join()
    full.close()
    b.join()
    for (string s in handoff) {
        println("handoff still gives {}", s)
    }
    for (string s in full) {
        println("full still gives {}", s)
    }
    ret 0
}
```

```output
send() to handoff returned
send() to full threw
handoff still gives kept
full still gives first
```

### for over a channel

`for (T v in channel)` receives until the channel is closed and empty, then ends: it is the usual way to read a channel. Unlike the elements of a list, `v` is a variable of the body, which owns the value received: the body can modify it, or move it, as to another channel.

```cromo
import std.thread

i32 main() {
    thread.Channel<string> words = thread.Channel<string>(8)
    thread.Channel<string> loud = thread.Channel<string>(8)
    words.send("tasks")
    words.send("threads")
    words.close()
    for (string w in words) {
        w = w.upper()
        loud.send(move w)
    }
    loud.close()
    for (string w in loud) {
        println("{}", w)
    }
    ret 0
}
```

```output
TASKS
THREADS
```

The channel of a `for` is a variable or a field, read at each turn; `ref` is an error, as there is nothing to modify in place:

```cromo error
import std.thread

thread.Channel<i64> numbers() {
    thread.Channel<i64> c = thread.Channel<i64>(1)
    c.close()
    ret c
}

i32 main() {
    thread.Channel<string> words = thread.Channel<string>(8)
    words.close()
    for (ref string w in words) {
        w = "x"
    }
    for (i64 n in numbers()) {
        println("{}", n)
    }
    thread.Channel<string> copy = words
    ret 0
}
```

```output
error: the values of a channel belong to the loop that receives them: write for (string w in ...)
error: a 'for' receives from a channel in a variable or in a field
error: values of type Channel<string> cannot be copied
```

## Sleeping and yielding

`time.sleep(seconds)`, `time.msleep(milliseconds)` and `time.usleep(microseconds)`, of the module `std.time` ([Files, processes and time](system.md)), pause: a thread sleeps, and a task waits while its worker runs the other tasks. A time of 0 or less returns at once, also in a task: it does not give the worker to the others. There is no `thread.yield()`: a task leaves its worker when it waits, and the runtime takes care of the tasks that never wait, as the section on the workers above explains.

## Threads or tasks

Threads and tasks have the same interface, so changing one into the other is changing `Thread` into `Task`. The choice:

- **Tasks** for work that waits, as the servers and the clients of the network, the stages of a pipeline joined by channels, and the long work that a window starts; and whenever the units of work are many: thousands of tasks cost little, and only tasks have a `thread.Group`.
- **Threads** for a few long computations, for C code that blocks its thread for a long time, since each such call keeps a worker and the workers added are at most 256, and for a recursion that needs more than the 1 MB of stack of a task.

## Summary

- `thread.Thread` is a thread of the system; `thread.Task` a lightweight task with the same interface. Both are made from a lambda, started with `start()` and waited for with `join()`.
- The lambda reads, modifies with `ref`, or takes with `move`; it cannot capture a parameter `func` that is not `const`.
- A thread that captures by address belongs to its block: `start()` and `join()` are written in that block, until `join()` the block cannot change what the thread reads nor use what it modifies, and the end of the block waits for it. A thread that owns what it uses is free.
- `start()` or `join()` twice, and `join()` before `start()`, are panics. `join()` throws again the exception of the thread; one that nobody takes stops the program, and a panic in any thread stops it at once.
- The data that threads change go in a `thread.Mutex`; `thread.Shared` is a handle to a value with its lock, which the handlers of the windows and the free threads can keep too.
- `thread.Atomic` holds an integer or a `bool`; `add` and `exchange` give the value before, `compareExchange` whether it wrote.
- A task costs a few KB; on Linux about 32 000 can live at once. `main` is a thread of the system.
- `thread.Group` starts many tasks and waits for them; its tasks read the variables declared before it, which stay constant until the end of its block.
- `thread.Channel` passes values in order, with a capacity, 0 by default; `close()` ends it, and `for (T v in channel)` receives until then.
- Each `print` and `println` writes its whole line, also among threads. A sleep of 0 returns at once, and there is no `thread.yield()`.
