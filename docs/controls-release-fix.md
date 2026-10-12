# Released keyboard taps

The PC mapper previously extended every message key press to at least 120 ms,
including after key-up. A Space tap could therefore remain asserted in a new
menu after the user released it. The regression test reproduces the extended
press in the previous mapper and fails on the first following released report.

The mapper now buffers a missed tap until one host report samples it. A key
sampled while held releases on key-up, and OS repeats do not queue an extra
tap. The SDK retains the host report between its pad polls. Multiple bindings
for one key share the same per-report state; genuine holds, independent taps,
mouse input and focus-loss clearing remain supported. This removes the
artificial 120 ms extension; game-defined repetition during an actual hold
is unchanged.

The complete `local/space-fix-build-tests.log` gate passed, including focused
released-tap, held/repeated/released key, long hold, multiple binding and
separate tap cases in the actual port mapper, plus camera and runtime/renderer
checks. Interactive menu feedback is still needed; this is not a claim that
every possible cause of repeated game actions has been ruled out.
