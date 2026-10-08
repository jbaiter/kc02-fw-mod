# Track 2b — second-opinion pass over the low-confidence names

The 164 proposals that Track 2 graded `guess` were re-examined by a stronger model
(`openai-codex/gpt-6.1-sol`), each packet carrying the first-pass proposal, the machine-extracted evidence
and the decompiled body, with an explicit instruction to be skeptical and prefer `keep_unk` over a weak name.

## Results (all 164 answered, 0 malformed, 0 name collisions)

| verdict | count |
|---|---:|
| `confirm` (body/evidence backs the first-pass name) | 26 |
| `revise` (better-supported name supplied) | 60 |
| `keep_unk` (evidence genuinely insufficient) | 78 |

**84 names applied** (one duplicate name dropped: `audio_check_header_match`).
Project total now **2314 functions, 2234 named = 96.5 %, 80 unnamed**.

The 78 `keep_unk` answers are a *result*, not a failure: they are the functions whose role cannot be
established from the available evidence (`docs/keep_unk_final.tsv`, each with the reason). Examples: a body
that is just `return 0`, a two-instruction wrapper whose caller context is unresolved, or a dispatcher whose
callee is itself unnamed. Re-deriving those needs caller-chain analysis or hardware observation.

## Operational note (for future fan-outs)

`gpt-6.1-sol` handles ~16 functions per child comfortably (≈2 min) but **hung on a 33-function batch**; that
single stuck lane aborted the whole workflow after ~30 min (the four sibling lanes had already written their
answers, so nothing was lost — the parent recovered 131 verdicts from the answer files). Lesson: cap
second-opinion batches at 15-20 functions per lane, and instruct children to *append results to their output
file as they go* and repeat them in the final answer, so a sibling's failure cannot cost data.
