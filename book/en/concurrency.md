# Threads, tasks and channels

Cromo runs code at the same time in two ways: **threads** of the system, and lightweight **tasks**, many of which share a few threads. Both are in the module `std.thread`, with the same interface, and the compiler checks what they share: two threads never write the same data at the same time.

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

The lambda reads `values` and modifies `low`, both variables of `main`. While the thread runs, the compiler does not let `main` modify what the thread reads, nor touch what it modifies: here `main` only reads `values`, which is allowed. A thread that captures by address is a thread of its block: if `join()` is not called, the block waits for it at its end, so the variables it uses always outlive it.

A thread that captures only with `move` owns everything it uses, and is free: it can be kept in a list and outlive the block that started it.

An exception that ends a thread is thrown again by `join()`, in the thread that waits.

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

There is no way to reach the `i64` without the lock, so there is no way to forget it. `lock` is released at the end of the lambda, even when an exception goes through.

## Shared

A `Mutex` stays where it is declared, and a lambda that is kept, as the handler of a window, cannot capture it. `thread.Shared<T>` is a handle to a value with its lock, kept until the end of the program: a small value, copied freely, and every copy reaches the same value with `lock`:

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

The handlers of the windows keep their state so, as [Windows and apps](gui.md) shows; threads and tasks capture a `Shared` as any other value.

## Tasks

A `thread.Task` has the interface and the rules of a `thread.Thread`, but it is not a thread of the system: the runtime runs many tasks on a few threads, one for each processor. When a task waits, for the network, for `time.sleep`, for a mutex or for `join()`, its thread runs other tasks in the meantime. The code is the same as with threads: there is no `async`, no `await`, and no function of a different color.

A task costs a few kilobytes: a program can have hundreds of thousands. `thread.Group` starts many of them and waits for all:

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

Ten thousand threads of the system would need gigabytes of stacks; ten thousand tasks need a few tens of megabytes.

- The tasks of a group can read the variables declared before the group, which stay constant until the end of its block, and take others with `move`; they cannot capture with `ref`.
- `wait()` waits for all the tasks, and throws again the first exception of a task. At the end of its block, a group waits for its tasks by itself.
- A task that calls a function that blocks its thread, as reading a file or a C function, keeps that thread; after about a millisecond the runtime gives the other tasks another thread.

Use tasks for work that waits, as servers and clients of the network; use threads for long computations and for C code that blocks.

## Channels

A `thread.Channel<T>` passes values from one task or thread to another, in order:

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

- `thread.Channel<T>(n)` holds up to `n` values: `send` waits while it is full, `receive` while it is empty, and a task that waits leaves its thread to the others. With capacity 0, the sender waits until a receiver has taken the value.
- `close()` ends the channel: the values already sent can still be received, then `receive()` throws an exception, and `for (T v in channel)` ends.
- `next(ref v)` receives without the exception: it returns `false` once the channel is closed and empty.

## Atomics

`thread.Atomic<T>` holds an integer or a `bool` that several threads change without a lock: `load`, `store`, `add`, `exchange` and `compareExchange`. They are sequentially consistent, the simplest model to reason about. `loadRelaxed`, `storeRelaxed` and `addRelaxed` are faster where only the value counts, as a counter that many threads increase: they give no order to what the threads do around them. An atomic cannot be copied, and the tasks and threads that use it capture it by reading it, as `done` and `letters` above.

## Summary

- `thread.Thread` is a thread of the system; `thread.Task` a lightweight task with the same interface.
- The compiler checks what threads and tasks capture: shared data that change go in a `thread.Mutex`.
- `thread.Shared` is a handle to a value with its lock, which the handlers of the windows can keep too.
- `thread.Group` starts many tasks and waits for them; a task that waits leaves its thread to others.
- `thread.Channel` passes values between tasks; `thread.Atomic` counts without locks.
