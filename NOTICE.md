# RorkHook Provenance

RorkHook is a first-party, Rork-owned runtime patching toolkit. It is a
clean-room reimplementation: the source here was written for Rork and is not a
copy of any third-party code.

## Inspiration / Prior Art

The API shape and the techniques - dyld-shared-cache local-symbol lookup, the
`mach_vm_protect` trap bypass, the arm64e TPRO write window, fishhook-style GOT
rebinding, and the MOVK/BR absolute-jump detour - follow the approach of
litehook by Lars Froder (opa334), distributed under the MIT License:

https://github.com/opa334/litehook

No litehook source is compiled into RorkHook. The implementations, structure,
naming, safety checks, and tests are Rork's own.

Because no third-party source is included, RorkHook ships without a bundled
third-party license. This notice records the lineage of the ideas as a courtesy
and for engineering traceability. If any litehook source is ever vendored in,
add its MIT license alongside this file and update this notice.
