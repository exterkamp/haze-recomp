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

This is a proposed experiment, not implemented or enabled. Keep it on the
experimental branch, track it separately and use focused commits. Required
gates include both camera test orders, the squad conversation, scene transitions,
shutdown and a long-run check for missed completion or lost work. Unlocked
gameplay timing and VSync behavior remain required; neither stable 144 FPS nor
consistent pacing is currently established.
