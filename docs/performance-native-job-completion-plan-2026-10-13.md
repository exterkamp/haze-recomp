# Native job-completion wait replacement

Tracked in [#39](https://github.com/zdiemer/haze-recomp/issues/39), under #32.

The current carrier wait diagnostic records about 5.315 ms/frame on guest main
thread 1 in 330-us `sys_timer_usleep` calls at LR `0068EFF0`, with 2.581 ms/frame
overlapping the renderer's period before its first draw. The longest recorded
call is 16.676 ms. These are elapsed service scopes, not CPU utilization or
automatically removable time. See the shader-preparation report for the full
measurement method and limitations.

Source inspection identifies the prior caller path in `00682538`: check job
completion through `00682510`, delay through `0068EFC0`, service pending work
through `00722ED8` / `00723198`, then retry. This work servicing must continue
even while waiting. Completion includes the game work record and queue draining,
so removing the predicate or pretending a job completed would be incorrect.

The first experiment should replace only this completion-loop delay, protected
by exact game-code and call-site guards. Compare the original 330-us delay with
a bounded native spin/yield policy while retaining the original completion
predicate and pending-work service. Measure CPU consumption, ordinary present
distributions, loop iterations and any worker starvation; do not extrapolate
the entire sleep sum into an FPS gain. Prefer a native event after auditing all
completion producers and publication/wakeup ordering. Generic game delays and
audio timing are outside this replacement boundary.

The first guarded prototype is now implemented, with performance validation
pending and the release default unchanged. Keep it on the experimental branch
and use focused commits. Required
gates include both camera test orders, the squad conversation, scene transitions,
shutdown and a long-run check for missed completion or lost work. Unlocked
gameplay timing and VSync behavior remain required; neither stable 144 FPS nor
consistent pacing is currently established.

## First prototype

`patch_job_wait.py` audits hashes of the completion loop, conditional delay
helper and float-duration syscall wrapper. It changes only two boundaries in
a build-local source copy, composing after the existing guest-pacer patch.
The completion predicate, queue/work servicing, and original duration
conversion and conditional call remain intact.

`haze_job_wait_call` establishes an exception-safe thread-local scope only for
guest thread 1, the audited TOC/call site and an in-range job index. Inside
that scope, the syscall replacement requires the original sleep LR, syscall
number 141 and exactly 330 us. Other calls take the original syscall path.
The native policy performs 32 processor pauses and yields to the scheduler
every 64 matching delays, then returns success. Every iteration still retries
the real completion predicate and services pending work. It writes no job
completion state, clocks or queue contents. This is bounded per-retry polling,
not an event-based wait and not a deadline guarantee under OS scheduling.

Runtime inspection clarified that the original Windows sub-millisecond
`sys_timer_usleep` already loops over `SwitchToThread` and QPC until its
requested deadline. The experiment removes that fixed deadline between
completion checks; it does not replace a coarse Windows Sleep call.

Internal `HAZE_NATIVE_JOB_WAIT=1` / `HAZE_JOB_WAIT_FILE` select the experiment.
The default stays off; no user-facing graphics/control setting is added.
Mode events and cumulative matching-delay/yield counts support comparison.
Guard tests prove the patch can be reversed to the exact original source,
composes with the pacer patch and rejects changed/repeated inputs. A native
fixture checks scoped handling, thread/TOC/call/job/duration rejection, syscall
result and exception cleanup. The full native regression gate passes after
fixing the unrelated Windows `near` macro collision in the camera test.
