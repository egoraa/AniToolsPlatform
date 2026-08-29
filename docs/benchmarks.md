# Measurements of the io and runtime layers

The baseline. Before it the project had 563 tests and **not one number**: three pacing modes,
exponential backoff, notifiers, the copy-on-delivery and "`unsafe` is cheaper" were all claims about
cost with not a single measurement behind them.

## How to build and run

```bash
cmake -S . -B <build-dir> -DCMAKE_BUILD_TYPE=Release -DATP_BUILD_BENCHMARKS=ON
cmake --build <build-dir> --target atp_benchmarks
<build-dir>/tests/benchmarks/atp_benchmarks --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

`ATP_BUILD_BENCHMARKS` is **OFF** by default: an ordinary build and every CI job are unaffected, and
nobody pays for downloading google/benchmark for a build that will not run it. **Release is
mandatory** — Debug measures the debug runtime rather than the platform.

## How much to trust these numbers

The measurements were taken on a developer's working machine (Windows, MSVC, 32 logical cores) rather
than on a quiet bench. The spread confirms it: `deliver_safe`'s median differed threefold between two
runs (47 ns against 137 ns), and the stddev on some rows reaches a third of the median. Therefore:

- **the absolute values below are an order of magnitude, not a constant of the platform;**
- what deserves trust is **the ratios measured within one run** — they are stable in sign, even while
  they float in magnitude;
- an authoritative baseline has to be taken on a dedicated machine; when CI comes back, that is the
  place for it.

The numbers below are medians of five repetitions of one run, with `--benchmark_min_time=0.3s`.

## The io layer

| Benchmark | Median | stddev |
|---|---|---|
| `deliver_to_subscribers/1` | 49.0 ns | 0.17 |
| `deliver_to_subscribers/4` | 231 ns | 74 |
| `deliver_to_subscribers/16` | 628 ns | 30 |
| `deliver_safe` | 137 ns | 36 |
| `deliver_unsafe` | 94.0 ns | 6.5 |
| `deliver_payload/1KiB` | 184 ns | 16 |
| `deliver_payload/64KiB` | 9501 ns | 510 |
| `deliver_payload/256KiB` | 35695 ns | 3476 |
| `read_state_input` (`get()`) | 31.8 ns | 1.2 |
| `read_queued_pop` (one value) | 178 ns | 12 |
| `read_queued_drain/256` | 2072 ns | 91 |
| `read_queued_pop_batch/256` | 4019 ns | 1760 |
| `property_get` | 11.0 ns | 0.12 |
| `property_set` | 12.1 ns | 0.09 |

What follows from this:

- **`unsafe` really is cheaper, and now that is a number.** 94 ns against 137; in a less loaded run,
  26 against 47. The ratio stays around 0.55–0.7 — a deferred lock saves roughly a third of the cost
  of delivery. This is the first confirmation of what the `unsafe` mode exists for at all.
- **`drain()` is about twice as cheap per element as a `try_pop()` at a time** (8.1 ns against
  15.7 ns at a batch of 256). The phrase "the cheapest way to process a batch" in `queued_input.hpp`
  is confirmed.
- **The cost per subscriber grows linearly**, but the base is large: one receiver 49 ns, sixteen 628.
  That is on the order of 35–40 ns per receiver, and it is exactly the copying of the receiver list on
  every write.
- **A large payload hits memory** (5–7.8 GiB/s), that is, delivery becomes a memcpy and the only thing
  worth discussing is the number of copies rather than the protocol's overhead.
- **Properties are cheap** (11–12 ns) — reading them in `iterate` is nothing to regret.

## The runtime layer

| Benchmark | Value |
|---|---|
| `pipeline_one_thread` | 4.19 M items/s |
| `pipeline_two_threads` | 2.74 M items/s |
| `on_demand_wakeup` p50 | **16.4 µs** |
| `on_demand_wakeup` p99 | 9.1 ms |
| `on_demand_wakeup` max | 14.8 ms |

- **Two threads are slower than one** (2.74 against 4.19 M/s) on this pair of modules: the price of
  cross-thread delivery and synchronization exceeds the gain from parallelism when there is almost no
  work per module. That is not a verdict on the mode but a boundary: splitting across threads pays off
  when a module computes for longer than the handover costs.
- **`on_demand` wake-up latency: a median of 16.4 µs.** The notifier works — without it the minimum
  would equal the backoff step. This is the number that bounds the platform's applicability for
  reacting to an external event.
- **The same number through `module_host::wake()`: a median of ~44 µs, a minimum of 16, and one
  outlier of 559 µs over ten runs.** Measured by the test
  `Wake.WakesTheOnDemandThreadWellBeforeTheBackoffCap` rather than by a benchmark, so it is an order
  of magnitude rather than a line. The order is the same as the input notifier's, which is expected:
  the path is the same `thread_signal`. The comparison to make is not against 16.4 µs but against what
  an external event faced **before** that channel existed: half of `idle_sleep_cap`, that is, about
  5 ms — over a hundred times more.
- **The tail is a closed question: it is the OS.** In the first run p99 = 9.1 ms and max = 14.8 ms —
  suspiciously close to `idle_sleep_cap` (10 ms) and to the granularity of the Windows system timer
  (15.6 ms). A repeat on a less loaded machine gave **p50 = 7.9 µs, p99 = 0.64 ms, max = 2.6 ms**: the
  tail shrank by an order of magnitude along with the load, while the runner's constants did not
  change. That pointed at the OS scheduler but did not prove it — **proved on 2026-08-07**, see the
  section "The `on_demand` wake-up tail" at the end of this file: not one tail measurement was served
  by a timeout, and a bare condition variable produces the same tail with not a line of the platform
  involved. There is no loss of a value here in any case: `wait_for` is called with a predicate, and
  after the wait the loop calls `iterate()` again regardless.

## What has already been fixed on the strength of these numbers

**The receiver list is no longer copied on every write.** It became a
`shared_ptr<const vector<input_base*>>` with copy-on-write: `connect`/`disconnect` build a new vector
and swap the pointer, while the write path takes a copy of the `shared_ptr` under the lock — a counter
increment instead of an allocation. The guarantee is the same: the list must outlive the delivery loop,
which runs **outside** the lock, or the output's and the input's mutexes would nest.

Measured by alternating A/B — two binaries, three rounds interleaved, or the machine's drift covers
the effect:

| benchmark | before | after | gain |
|---|---|---|---|
| `deliver_to_subscribers/1` | 47.5 ns | 31.7 ns | −33% |
| `deliver_to_subscribers/4` | 84.7 ns | 71.4 ns | −16% |
| `deliver_to_subscribers/16` | 236.6 ns | 226 ns | −4% |

The saving is constant — about **16 ns per write**, exactly the allocation removed. With one receiver
that is a third of the cost of delivery; with sixteen it dissolves into the cost of the deliveries
themselves. That also says where to look next: at a large N the payload copies dominate rather than the
write's overhead.

**The price of per-module metrics, by the same method.** Timing every `iterate` with a pair of
`steady_clock::now()` calls:

| | items/s |
|---|---|
| no metrics | 4.49 M |
| metrics enabled | 3.35 M (**−25%**) |
| metrics disabled by the switch | 4.45 M (within the noise) |

Hence the decision: off by default, switched on for the duration of a diagnosis. Always-on it cannot
be — a cheap `iterate` is cheaper than two clock reads, so the measurement would distort the measured
exactly where it is being looked at.

**A note on method.** The first attempts to compare "before" and "after" with ordinary sequential runs
produced contradictions: `deliver_safe` and `deliver_to_subscribers/1` — the same operation — diverged
threefold in opposite directions. The machine's drift here exceeds the effect being measured, and the
only thing that works is to build both variants as separate binaries and run them interleaved.

## The write path (2026-08-04)

**There were `2 + 2N` copies per write, not the `2 + N` assumed when the work was scoped.** Two fixed
ones (a temporary on the writer's stack and a copy into the output's cache) and **two per subscriber**:
`input<T>::operator()` built its own stack copy and only then moved it into storage, and a move of a
trivially copyable type is a second memcpy. The measurement showed it directly: at 256 KiB the increment
per subscriber (19–21 µs) was twice a single copy (10.5 µs).

The `fanout/*` (0/1/2/4 subscribers) and `movable/*` (lvalue against rvalue on a `vector<uint8_t>`)
benchmarks were added to `io_bench.cpp` for this work. The method is alternating A/B: two binaries
interleaved, three rounds, medians of five repetitions averaged over the rounds.

| benchmark | before | after | gain |
|---|---|---|---|
| `fanout/1KiB/0` | 37.0 ns | 17.7 ns | −52% |
| `fanout/1KiB/1` | 108.5 ns | 67.6 ns | −38% |
| `fanout/1KiB/4` | 264.9 ns | 178.1 ns | −33% |
| `fanout/64KiB/0` | 4313 ns | 32.7 ns | −99% |
| `fanout/64KiB/1` | 7726 ns | 2238 ns | −71% |
| `fanout/64KiB/4` | 23910 ns | 8273 ns | −65% |
| `fanout/256KiB/0` | 20490 ns | 31.9 ns | −100% |
| `fanout/256KiB/1` | 38356 ns | 9137 ns | **−76%** |
| `fanout/256KiB/2` | 55497 ns | 17032 ns | −69% |
| `fanout/256KiB/4` | 73457 ns | 46618 ns | −37% |
| `movable/64KiB` lvalue | 8443 ns | 5665 ns | −33% |
| `movable/64KiB` rvalue | 5787 ns | 2266 ns | −61% |
| `movable/256KiB` lvalue | 29135 ns | 17214 ns | −41% |
| `movable/256KiB` rvalue | 21036 ns | 7993 ns | −62% |

What is worth reading carefully here:

- **`fanout/*/1` is exactly "4 copies → 1"**, and −76% at 256 KiB matches the arithmetic (−75%) better
  than this machine usually lets one hope;
- **a write with no subscribers now costs nanoseconds** (−99…100%): there is nobody left to pay for a
  cache that does not exist, and only a counter increment remains. That is the price headless runs with
  not one reader used to carry;
- **an rvalue of a movable payload is two to three times cheaper than an lvalue** — the value is handed
  to the last subscriber by move, and a chain with a single receiver copies nothing;
- the gain **falls with the number of subscribers** (−76% at one, −37% at four): the copies removed
  were the fixed ones, while the delivery copies remain. That is expected, and it also says where to
  look next if the need arises — at a shared buffer, not at the write path.

**There is no ceiling on value size any more.** Before this work it existed, was undocumented, and
stood at **448 KiB**: two stack copies (in the output and in the input) lived at once, and at 512 KiB
the process died with `STATUS_STACK_OVERFLOW` (0xC00000FD) on a 1 MB thread stack — identically on the
main thread and on a `std::thread`. That is why the io table above stops at 256 KiB rather than a
megabyte. Now no write path puts more than `io::heap_copy_threshold` (4 KiB) on the stack, and 4 MiB
travel the chain (the test `InputStorePath.FourMegabytesTravelWithoutOverflowingTheStack`).

The remaining boundary is not in delivery but in reading, and it is visible: `get()` and `take()`
return a copy into the caller's frame, so `auto v = in.get()` on a megabyte value puts a megabyte on
the reader's stack. That is the reader's own variable rather than a hidden copy made by the platform.

## The price of the input counters (2026-08-05)

A bounded queue and the received/discarded counters add an increment, a predictable branch and — for a
queue — a comparison of size against capacity to the write path. All of it is inside a critical section
the input holds anyway, so a cost within a couple of per cent was expected. Measured against `master`
(`1f58851`), Release, 9 repetitions, medians:

| Benchmark | master, ns | branch, ns (two runs) | Δ |
|---|---|---|---|
| `read_state_input` | 11.9 | 11.9 / 12.0 | 0…+0.8% |
| `read_queued_pop` | 47.5 | 48.9 / 49.2 | +2.9…+3.6% |
| `read_queued_drain/16` | 190.9 | 195.5 / 196.7 | +2.4…+3.0% |
| `read_queued_drain/256` | 801.0 | 801.4 / 809.3 | +0.1…+1.0% |
| `read_queued_pop_batch/16` | 355.5 | 359.8 / 356.9 | +0.4…+1.2% |
| `read_queued_pop_batch/256` | 3990.9 | 4058.6 / 4018.0 | +0.7…+1.7% |

The conclusion: **up to 3.6% on taking one at a time, about a per cent on the batch paths, zero on
"last value wins"**. Cheapest where there is more work per value — which is what one expects from an
addition of constant size.

### How these numbers were obtained, and why the first three attempts had to be thrown away

This is recorded not for history's sake but because each mistake looked like a result:

1. **The baseline was a binary that happened to be lying in `bench-release`.** By content it held none
   of the new code, but it predated an earlier subproject, and the comparison showed −60% on `fanout` —
   the effect of that work rather than this one. The moral: "contains none of the new code" is not the
   same as "is master".
2. **The baseline was rebuilt from `master` (through a stash), but the run went in parallel with builds
   and tests.** The numbers came apart: neighbouring sizes disagreed in sign (`fanout/256KiB/2` +66%,
   `/4` −5%) and the absolute times doubled. An inconsistency of sign within one family is the sign
   that the machine was being measured rather than the code.
3. **The order within a round was constant** (master first, then the branch) — and gave −48% on
   `read_state_input`. That is **logically impossible**: the change on that path only adds an
   increment. The impossible sign turned out to be the most useful signal of the whole measurement.

What worked was alternating the order: in odd rounds master goes first, in even ones the branch. Two
runs of the branch taken in different positions agreed with each other to within fractions of a per
cent — and it is that reproducibility, rather than closeness to expectation, that makes the numbers
above usable.

The usual caveat is especially apt here: the machine is a working one. Magnitudes below five per cent
on it are at the edge of its resolving power, and are worth rechecking on a dedicated bench once one
exists.

## The `on_demand` wake-up tail (2026-08-07)

The question was posed as a choice between two: a median of 16 µs with a p99 of **9.1 ms** — is that
the runner (the signal failed to reach a thread that was about to fall asleep, and it slept out the
backoff step) or the OS (the scheduler and the timer granularity)? The benchmark itself could not
answer: it measures end-to-end latency only.

**The answer is the OS.** The runner is vindicated by measurement rather than by argument.

### What is measured on the current `master` (`bb2065a`), five clean runs

| | p50 | p99 | max |
|---|---|---|---|
| spread over five runs | 4.6–7.7 µs | 0.14–2.1 ms | 1.8–9.4 ms |

The median is three times better than the 16.4 µs recorded above, and the tail **reproduces in every
run** — at least one excursion into milliseconds per 200 measurements. The outliers are scattered over
the whole interval [0, 10 ms] rather than pressed against its end.

### The distinguishing experiment

Counters were temporarily added to `thread_signal` (`notify()`, returns of `wait_for` split into "by
signal" and "by timeout", and timestamps of both), and to the benchmark a classification of every
measurement by which counter moved during its window. Three runs of 200 measurements:

| run | `notifies` | by signal | by timeout | tail by signal | **tail by timeout** | max | of which inside the wake-up |
|---|---|---|---|---|---|---|---|
| 1 | 200 | 200 | 0 | 3 | **0** | 2.693 ms | 2.692 ms |
| 2 | 200 | 198 | 2 | 2 | **0** | 6.818 ms | 6.816 ms |
| 3 | 200 | 200 | 0 | 2 | **0** | 3.622 ms | 3.621 ms |

Three facts, each closing its own hypothesis:

1. **`notifies` = 200 exactly** — one per measurement. The notifier is attached to a real input (a
   group's alias is the same object) and always fires; the "the signal is not sent" version is out.
2. **Not one tail measurement was served by a timeout** — in any run. The "the thread sleeps out the
   backoff step" version is out: the backoff step takes no part in the tail at all. (A timeout does
   occasionally serve a measurement — two cases in run 2 — but always a fast one: an expired step that
   coincided with a value arriving.)
3. **`max_wake_ns` matches `max_ns`** to within 1.5–1.8 µs. The whole delay of the worst measurement
   lies **between `notify()` returning and `wait_for` returning**; delivery and `iterate` account for a
   microsecond and a half. There is nothing further to measure inside the platform — there is no more
   time in there.

### The control experiment: the same thing with not a line of the platform

A separate program: one thread waits on a condition variable with the same backoff (1 ms → 10 ms), a
second wakes it after a 25 ms pause, and a third spins an idle loop — the benchmark's arrangement, but
with no runner, no ports and no modules. Two waiting variants, three runs each:

| variant | p50 | p99 | max |
|---|---|---|---|
| `condition_variable_any` + `stop_token`, as in the runner | 10.7 / 13.3 / 28.8 µs | 1.18 / 1.88 / 2.19 ms | 2.45 / 4.54 / **7.25 ms** |
| a bare `condition_variable` | 3.8 / 3.9 / 5.1 µs | 0.08 / 0.20 / 0.40 ms | 1.00 / 2.06 / **4.96 ms** |

A tail in milliseconds is there **for the bare condition variable too**. So it is created neither by
the runner nor by the `stop_token` overload, and belongs to the pair "the Windows scheduler plus waking
a thread whose core has had 25 ms of silence to go into a deep idle". That is a property of the system,
and for anyone choosing a platform for reacting to an external event it is the real worst case: **a
median of single-digit microseconds, with a rare excursion up to roughly `idle_sleep_cap`**.

A side observation, unrelated to the tail: the `condition_variable_any` overload with a `stop_token`
costs **about three times more than the bare one at the median** (10–29 µs against 4–5). The runner
needs it — without a token there is nothing to wake the thread with on a stop — and changing it for the
sake of microseconds at the median, at the risk of a missed wake-up on `stop`, is not worth it.
Recorded so as not to reopen the question without cause.

### How to repeat this

The instrumentation was temporary and did not stay in the tree. To repeat: counters for `notify()` and
for the returns of `wait_for` in `pipeline_runner::thread_signal` behind an `#ifdef`; classification of
a measurement in `on_demand_wakeup` by which counter moved in the window between `armed` being raised
and the value arriving; a Release build with `-DATP_BUILD_BENCHMARKS=ON`. The control program does not
involve the platform at all and is written from scratch in ten minutes — and it is the decisive half of
the experiment.

The caveat is the same as everywhere in this file: the machine is a working one, with 32 logical cores.
The absolute values are an order of magnitude; the conclusions here rest on **ratios within one run**
(which counter moved, where the time lies) and on the reproducibility of the sign across three
independent experiments.
