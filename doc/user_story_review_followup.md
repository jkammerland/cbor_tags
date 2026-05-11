# User Story: Close Remaining Review Debt In Decoder, Ranges, And Docs

## Story

As a maintainer of `cbor_tags`,
I want to resolve the remaining high-value issues identified in the review pass,
so that decoding is safer, non-contiguous buffer behavior is well-defined, and the public API/docs no longer advertise unsupported or misleading behavior.

## Problem Summary

This story was created from the review pass before the follow-up branch was
implemented. It records the intended scope and acceptance criteria for that
work, not a live list of issues that are still open at HEAD.

At the time the story was written, the review pass found these issues:

- Numeric decode still lacks range validation for signed integers and enums.
- Non-contiguous decoding still relies on `seek(-1)` and random-access assumptions that are unsafe for forward-only iterators.
- View-based decode APIs still expose buffer-backed lifetime hazards without clear documentation.
- UTF-8 validation is still not implemented even though `invalid_utf8_sequence` exists in the public status enum.
- Public range-view placeholder types are still exposed even though they are not implemented.
- Docs still point at missing headers or unfinished pages.

Some previously reported items are already fixed and are intentionally out of scope for this story:

- Empty `std::basic_string_view<std::byte>` decode underflow / reader advance bug.
- `as_text_any` / `as_bstr_any` truncation handling.
- Fixed-size output buffer overflow on encode.
- Fixed-size array/span decode length mismatch overflow.

## Current Branch Status

The current branch addresses the original review debt in the intended low-churn
way:

- Signed and unsigned integer decodes intentionally slice into the target type.
- Enum decode follows the integer slicing policy through the enum underlying type; unnamed values remain accepted by policy.
- Forward-only non-contiguous CBOR buffers are rejected by the buffer concept.
- Non-contiguous array/map/tag range-view placeholders were removed from the public variant surface.
- Range/view lifetime requirements and limitations are documented.
- CDDL docs now point at `cbor_tags/extensions/cbor_visualization.h`.
- Core text decode is documented as byte-preserving and not UTF-8-validating.

Retry-after-incomplete semantics are not part of the primary decoder contract.
They are intentionally moved to `doc/user_story_resumable_codec.md`, where a
separate resumable decode/encode entry point can own checkpoint state, rollback
policy, and iterator invalidation behavior.

## In Scope

- Decoder correctness and safety fixes for remaining open review findings.
- Public API cleanup where unsupported types are currently exposed.
- Documentation updates needed to match actual supported behavior.
- Regression tests for each bug fixed by this story.

## Out Of Scope

- New resumable streaming/coroutine APIs.
- Retry-after-incomplete semantics for the primary decoder path.
- Performance-only refactors.
- Broader redesign of tags, variants, or reflection.

## Original Acceptance Criteria

1. Signed integer decode behavior is documented. Current policy intentionally slices instead of rejecting overflow or out-of-range negative values.
2. Enum decode behavior is documented. Current policy follows the underlying integer slicing behavior and does not validate enumerator membership.
3. Non-contiguous decode no longer depends on `seek(-1)` for forward-only iterators, or the buffer concept is tightened so such buffers are rejected at compile time.
4. Any required non-contiguous random-access helper is implemented, or code paths that depend on it are removed/guarded.
5. Text-string decode either validates UTF-8 and returns `status_code::invalid_utf8_sequence`, or the public API/docs clearly state that UTF-8 is not validated.
6. Public placeholder range-view types that are not implemented are removed from the advertised public variant surface, or they are fully implemented and covered by tests.
7. Documentation explains lifetime requirements for `std::string_view`, `std::span<const std::byte>`, `bstr_view`, and `tstr_view` decode results.
8. `doc/cddl_handling.md` no longer references a missing `cbor_tags/cbor_cddl.h` header.
9. `doc/range_handling.md` is no longer an empty TODO and describes currently supported range behavior and limitations.
10. New tests cover:
   - signed overflow / out-of-range negative decode slicing
   - enum decode edge cases
   - non-contiguous forward-iterator or rewind behavior
   - UTF-8 validation behavior
   - range-view/public API cleanup expectations

## Suggested Technical Notes

- Likely touch points:
  - `include/cbor_tags/cbor_decoder.h`
  - `include/cbor_tags/cbor_detail.h`
  - `include/cbor_tags/cbor.h`
  - `include/cbor_tags/extensions/cbor_visualization.h`
  - `doc/cddl_handling.md`
  - `doc/range_handling.md`
  - targeted tests under `test/`
- Review source:
  - This story summarizes the local review notes generated during the review pass. Those notes are not required repo artifacts.

## Original Definition Of Done

This was the completion target for the original review-debt story. It does not
claim that broader retry-after-incomplete semantics are complete.

- All acceptance criteria are met.
- `ctest` passes.
- New tests fail before the fix and pass after it.
- User-facing docs match actual behavior in the codebase.

## Follow-Up Candidates

- Implement `doc/user_story_resumable_codec.md` before adding retry/resume behavior.
- Decide whether diagnostic visualization should support bidirectional non-contiguous buffers or be constrained to contiguous/random-access buffers.
- Align enum status text with the representability-based enum policy.
